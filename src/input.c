#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <unistd.h>
#include "input.h"
#include "log.h"
#include "nelem.h"
#include "tofi.h"
#include "unicode.h"


static uint32_t keysym_to_key(xkb_keysym_t sym);
static void add_character(struct tofi *tofi, xkb_keycode_t keycode);
static void delete_previous_character(struct tofi *tofi);
static void delete_next_character(struct tofi *tofi);
static void delete_previous_word(struct tofi *tofi);
static void delete_next_word(struct tofi *tofi);
static void clear_input(struct tofi *tofi);
static void paste(struct tofi *tofi);
static void copy_selection(struct tofi *tofi);
static void cut_selection(struct tofi *tofi);
static void move_cursor(struct tofi *tofi, bool forward, bool by_word, bool select);
static void move_cursor_to_boundary(struct tofi *tofi, bool to_end, bool select);
static bool has_text_selection(const struct entry *entry);
static bool collapse_text_selection(struct entry *entry, bool forward);
static void delete_text_range(struct entry *entry, uint32_t start, uint32_t end);
static uint32_t previous_word_start(const struct entry *entry, uint32_t position);
static uint32_t next_word_start(const struct entry *entry, uint32_t position);
static void select_previous_result(struct tofi *tofi);
static void select_next_result(struct tofi *tofi);
static void select_previous_page(struct tofi *tofi);
static void select_next_page(struct tofi *tofi);
static void next_cursor_or_result(struct tofi *tofi);
static void previous_cursor_or_result(struct tofi *tofi);
static void reset_result_selection(struct tofi *tofi);

void input_handle_keypress(struct tofi *tofi, xkb_keycode_t keycode)
{
	if (tofi->xkb_state == NULL) {
		return;
	}

	bool ctrl = xkb_state_mod_name_is_active(
			tofi->xkb_state,
			XKB_MOD_NAME_CTRL,
			XKB_STATE_MODS_EFFECTIVE);
	bool alt = xkb_state_mod_name_is_active(
			tofi->xkb_state,
			XKB_MOD_NAME_ALT,
			XKB_STATE_MODS_EFFECTIVE);
	bool shift = xkb_state_mod_name_is_active(
			tofi->xkb_state,
			XKB_MOD_NAME_SHIFT,
			XKB_STATE_MODS_EFFECTIVE);

	uint32_t ch = xkb_state_key_get_utf32(tofi->xkb_state, keycode);
	struct entry *entry = &tofi->window.entry;

	/*
	 * Use physical key code for shortcuts by default, ignoring layout
	 * changes. Linux keycodes are 8 less than XKB keycodes.
	 */
	uint32_t key = keycode - 8;
	if (!tofi->physical_keybindings) {
		xkb_keysym_t sym = xkb_state_key_get_one_sym(tofi->xkb_state, keycode);
		key = keysym_to_key(sym);
	}

	/*
	 * Alt does not affect which character is selected, so we have to check
	 * for it explicitly.
	 */
	if (utf32_isprint(ch) && !ctrl && !alt) {
		add_character(tofi, keycode);
	} else if (key == KEY_A && ctrl) {
		entry->selection_anchor = 0;
		entry->cursor_position = entry->input_utf32_length;
	} else if (((key == KEY_C && ctrl) || (key == KEY_INSERT && ctrl))
			&& has_text_selection(entry)) {
		copy_selection(tofi);
	} else if (((key == KEY_X && ctrl) || (key == KEY_DELETE && shift))
			&& has_text_selection(entry)) {
		cut_selection(tofi);
	} else if ((key == KEY_BACKSPACE || key == KEY_W) && ctrl) {
		delete_previous_word(tofi);
	} else if (key == KEY_DELETE && ctrl) {
		delete_next_word(tofi);
	} else if (key == KEY_BACKSPACE
			|| (key == KEY_H && ctrl)) {
		delete_previous_character(tofi);
	} else if (key == KEY_DELETE && !shift) {
		delete_next_character(tofi);
	} else if (key == KEY_U && ctrl) {
		clear_input(tofi);
	} else if ((key == KEY_V && ctrl) || (key == KEY_INSERT && shift)) {
		paste(tofi);
	} else if (key == KEY_HOME || key == KEY_END) {
		move_cursor_to_boundary(tofi, key == KEY_END, shift);
	} else if ((key == KEY_LEFT || key == KEY_RIGHT) && (ctrl || shift)) {
		move_cursor(tofi, key == KEY_RIGHT, ctrl, shift);
	} else if (key == KEY_LEFT) {
		if (!collapse_text_selection(entry, false)) {
			previous_cursor_or_result(tofi);
		}
	} else if (key == KEY_RIGHT) {
		if (!collapse_text_selection(entry, true)) {
			next_cursor_or_result(tofi);
		}
	} else if (key == KEY_UP
			|| key == KEY_LEFT
			|| (key == KEY_TAB && shift)
			|| (key == KEY_H && alt)
			|| ((key == KEY_K || key == KEY_P || key == KEY_B) && (ctrl || alt))) {
		select_previous_result(tofi);
	} else if (key == KEY_DOWN
			|| key == KEY_RIGHT
			|| key == KEY_TAB
			|| (key == KEY_L && alt)
			|| ((key == KEY_J || key == KEY_N || key == KEY_F) && (ctrl || alt))) {
		select_next_result(tofi);
	} else if (key == KEY_PAGEUP) {
		select_previous_page(tofi);
	} else if (key == KEY_PAGEDOWN) {
		select_next_page(tofi);
	} else if (key == KEY_ESC
			|| ((key == KEY_C || key == KEY_LEFTBRACE || key == KEY_G) && ctrl)) {
		tofi->closed = true;
		return;
	} else if (key == KEY_ENTER
			|| key == KEY_KPENTER
			|| (key == KEY_M && ctrl)) {
		tofi->submit = true;
		return;
	}

	if (tofi->auto_accept_single && entry->results.count == 1) {
		tofi->submit = true;
	}

	tofi->window.surface.redraw = true;
}

static uint32_t keysym_to_key(xkb_keysym_t sym)
{
	switch (sym) {
		case XKB_KEY_BackSpace:
			return KEY_BACKSPACE;
		case XKB_KEY_Delete:
			return KEY_DELETE;
		case XKB_KEY_Insert:
			return KEY_INSERT;
		case XKB_KEY_a:
			return KEY_A;
		case XKB_KEY_w:
			return KEY_W;
		case XKB_KEY_u:
			return KEY_U;
		case XKB_KEY_v:
			return KEY_V;
		case XKB_KEY_Left:
			return KEY_LEFT;
		case XKB_KEY_Right:
			return KEY_RIGHT;
		case XKB_KEY_Up:
			return KEY_UP;
		case XKB_KEY_ISO_Left_Tab:
			return KEY_TAB;
		case XKB_KEY_h:
			return KEY_H;
		case XKB_KEY_k:
			return KEY_K;
		case XKB_KEY_p:
			return KEY_P;
		case XKB_KEY_Down:
			return KEY_DOWN;
		case XKB_KEY_Tab:
			return KEY_TAB;
		case XKB_KEY_l:
			return KEY_L;
		case XKB_KEY_j:
			return KEY_J;
		case XKB_KEY_n:
			return KEY_N;
		case XKB_KEY_Home:
			return KEY_HOME;
		case XKB_KEY_End:
			return KEY_END;
		case XKB_KEY_Page_Up:
			return KEY_PAGEUP;
		case XKB_KEY_Page_Down:
			return KEY_PAGEDOWN;
		case XKB_KEY_Escape:
			return KEY_ESC;
		case XKB_KEY_c:
			return KEY_C;
		case XKB_KEY_x:
			return KEY_X;
		case XKB_KEY_g:
			return KEY_G;
		case XKB_KEY_bracketleft:
			return KEY_LEFTBRACE;
		case XKB_KEY_Return:
			return KEY_ENTER;
		case XKB_KEY_KP_Enter:
			return KEY_KPENTER;
		case XKB_KEY_m:
			return KEY_M;
	}
	return (uint32_t)-1;
}

static void reset_result_selection(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;
	entry->selection = 0;
	entry->first_result = 0;
}

static void add_character(struct tofi *tofi, xkb_keycode_t keycode)
{
	struct entry *entry = &tofi->window.entry;

	if (entry->input_utf32_length >= N_ELEM(entry->input_utf32) - 1
			&& !has_text_selection(entry)) {
		/* No more room for input */
		return;
	}

	bool replaced_selection = input_delete_selection(entry);
	char buf[5]; /* 4 UTF-8 bytes plus null terminator. */
	int len = xkb_state_key_get_utf8(
			tofi->xkb_state,
			keycode,
			buf,
			sizeof(buf));
	if (entry->cursor_position == entry->input_utf32_length && !replaced_selection) {
		entry->input_utf32[entry->input_utf32_length] = utf8_to_utf32(buf);
		entry->input_utf32_length++;
		entry->input_utf32[entry->input_utf32_length] = U'\0';
		memcpy(&entry->input_utf8[entry->input_utf8_length],
				buf,
				N_ELEM(buf));
		entry->input_utf8_length += len;

		if (entry->mode == TOFI_MODE_DRUN) {
			struct string_ref_vec results = desktop_vec_filter(&entry->apps, entry->input_utf8, tofi->matching_algorithm);
			string_ref_vec_destroy(&entry->results);
			entry->results = results;
		} else {
			struct string_ref_vec tmp = entry->results;
			entry->results = string_ref_vec_filter(&entry->results, entry->input_utf8, tofi->matching_algorithm);
			string_ref_vec_destroy(&tmp);
		}

		reset_result_selection(tofi);
	} else {
		for (size_t i = entry->input_utf32_length; i > entry->cursor_position; i--) {
			entry->input_utf32[i] = entry->input_utf32[i - 1];
		}
		entry->input_utf32[entry->cursor_position] = utf8_to_utf32(buf);
		entry->input_utf32_length++;
		entry->input_utf32[entry->input_utf32_length] = U'\0';

		input_refresh_results(tofi);
	}

	entry->cursor_position++;
	entry->selection_anchor = entry->cursor_position;
}

void input_refresh_results(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;

	size_t bytes_written = 0;
	for (size_t i = 0; i < entry->input_utf32_length; i++) {
		bytes_written += utf32_to_utf8(
				entry->input_utf32[i],
				&entry->input_utf8[bytes_written]);
	}
	entry->input_utf8[bytes_written] = '\0';
	entry->input_utf8_length = bytes_written;
	string_ref_vec_destroy(&entry->results);
	if (entry->mode == TOFI_MODE_DRUN) {
		entry->results = desktop_vec_filter(&entry->apps, entry->input_utf8, tofi->matching_algorithm);
	} else {
		entry->results = string_ref_vec_filter(&entry->commands, entry->input_utf8, tofi->matching_algorithm);
	}

	reset_result_selection(tofi);
}

static void delete_previous_character(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;
	if (input_delete_selection(entry)) {
		input_refresh_results(tofi);
		return;
	}

	if (entry->cursor_position == 0) {
		return;
	}

	delete_text_range(entry, entry->cursor_position - 1, entry->cursor_position);
	input_refresh_results(tofi);
}

static void delete_next_character(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;
	if (input_delete_selection(entry)) {
		input_refresh_results(tofi);
		return;
	}

	if (entry->cursor_position == entry->input_utf32_length) {
		return;
	}

	delete_text_range(entry, entry->cursor_position, entry->cursor_position + 1);
	input_refresh_results(tofi);
}

static void delete_previous_word(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;
	if (input_delete_selection(entry)) {
		input_refresh_results(tofi);
		return;
	}

	if (entry->cursor_position == 0) {
		/* No input to delete. */
		return;
	}

	uint32_t start = previous_word_start(entry, entry->cursor_position);
	delete_text_range(entry, start, entry->cursor_position);
	input_refresh_results(tofi);
}

static void delete_next_word(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;
	if (input_delete_selection(entry)) {
		input_refresh_results(tofi);
		return;
	}

	uint32_t end = next_word_start(entry, entry->cursor_position);
	if (end == entry->cursor_position) {
		return;
	}

	delete_text_range(entry, entry->cursor_position, end);
	input_refresh_results(tofi);
}

static void clear_input(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;

	entry->cursor_position = 0;
	entry->selection_anchor = 0;
	entry->input_utf32_length = 0;
	entry->input_utf32[0] = U'\0';

	input_refresh_results(tofi);
}

static void paste(struct tofi *tofi)
{
	if (tofi->clipboard.wl_data_offer == NULL || tofi->clipboard.mime_type == NULL) {
		return;
	}

	/*
	 * Create a pipe, and give the write end to the compositor to give to
	 * the clipboard manager.
	 */
	errno = 0;
	int fildes[2];
	if (pipe2(fildes, O_CLOEXEC | O_NONBLOCK) == -1) {
		log_error("Failed to open pipe for clipboard: %s\n", strerror(errno));
		return;
	}
	wl_data_offer_receive(tofi->clipboard.wl_data_offer, tofi->clipboard.mime_type, fildes[1]);
	close(fildes[1]);

	/* Keep the read end for reading in the main loop. */
	tofi->clipboard.fd = fildes[0];
}

static bool has_text_selection(const struct entry *entry)
{
	return entry->cursor_position != entry->selection_anchor;
}

static bool collapse_text_selection(struct entry *entry, bool forward)
{
	if (!has_text_selection(entry)) {
		return false;
	}

	entry->cursor_position = forward
		? MAX(entry->cursor_position, entry->selection_anchor)
		: MIN(entry->cursor_position, entry->selection_anchor);
	entry->selection_anchor = entry->cursor_position;
	return true;
}

static void delete_text_range(struct entry *entry, uint32_t start, uint32_t end)
{
	memmove(
			&entry->input_utf32[start],
			&entry->input_utf32[end],
			(entry->input_utf32_length - end + 1) * sizeof(entry->input_utf32[0]));
	entry->input_utf32_length -= end - start;
	entry->cursor_position = start;
	entry->selection_anchor = start;
}

bool input_delete_selection(struct entry *entry)
{
	if (!has_text_selection(entry)) {
		return false;
	}

	uint32_t start = MIN(entry->cursor_position, entry->selection_anchor);
	uint32_t end = MAX(entry->cursor_position, entry->selection_anchor);
	delete_text_range(entry, start, end);
	return true;
}

static uint32_t previous_word_start(const struct entry *entry, uint32_t position)
{
	while (position > 0 && utf32_isspace(entry->input_utf32[position - 1])) {
		position--;
	}
	while (position > 0 && !utf32_isspace(entry->input_utf32[position - 1])) {
		position--;
	}
	return position;
}

static uint32_t next_word_start(const struct entry *entry, uint32_t position)
{
	while (position < entry->input_utf32_length
			&& !utf32_isspace(entry->input_utf32[position])) {
		position++;
	}
	while (position < entry->input_utf32_length
			&& utf32_isspace(entry->input_utf32[position])) {
		position++;
	}
	return position;
}

static void move_cursor(struct tofi *tofi, bool forward, bool by_word, bool select)
{
	struct entry *entry = &tofi->window.entry;
	if (!select && collapse_text_selection(entry, forward)) {
		return;
	}

	if (by_word) {
		entry->cursor_position = forward
			? next_word_start(entry, entry->cursor_position)
			: previous_word_start(entry, entry->cursor_position);
	} else if (forward && entry->cursor_position < entry->input_utf32_length) {
		entry->cursor_position++;
	} else if (!forward && entry->cursor_position > 0) {
		entry->cursor_position--;
	}
	if (!select) {
		entry->selection_anchor = entry->cursor_position;
	}
}

static void move_cursor_to_boundary(struct tofi *tofi, bool to_end, bool select)
{
	struct entry *entry = &tofi->window.entry;
	entry->cursor_position = to_end ? entry->input_utf32_length : 0;
	if (!select) {
		entry->selection_anchor = entry->cursor_position;
	}
}

static void copy_selection(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;
	uint32_t start = MIN(entry->cursor_position, entry->selection_anchor);
	uint32_t end = MAX(entry->cursor_position, entry->selection_anchor);
	char text[4 * MAX_INPUT_LENGTH + 1];
	size_t bytes = 0;
	for (uint32_t i = start; i < end; i++) {
		bytes += utf32_to_utf8(entry->input_utf32[i], &text[bytes]);
	}
	text[bytes] = '\0';
	clipboard_copy(
			&tofi->clipboard,
			tofi->wl_data_device_manager,
			tofi->wl_data_device,
			tofi->keyboard_serial,
			text);
}

static void cut_selection(struct tofi *tofi)
{
	copy_selection(tofi);
	input_delete_selection(&tofi->window.entry);
	input_refresh_results(tofi);
}

static void select_previous_result(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;

	if (entry->selection > 0) {
		entry->selection--;
		return;
	}

	uint32_t nsel = MAX(MIN(entry->num_results_drawn, entry->results.count), 1);

	if (entry->first_result > nsel) {
		entry->first_result -= entry->last_num_results_drawn;
		entry->selection = entry->last_num_results_drawn - 1;
	} else if (entry->first_result > 0) {
		entry->selection = entry->first_result - 1;
		entry->first_result = 0;
	}
}

static void select_next_result(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;

	uint32_t nsel = MAX(MIN(entry->num_results_drawn, entry->results.count), 1);

	entry->selection++;
	if (entry->selection >= nsel) {
		entry->selection -= nsel;
		if (entry->results.count > 0) {
			entry->first_result += nsel;
			entry->first_result %= entry->results.count;
		} else {
			entry->first_result = 0;
		}
		entry->last_num_results_drawn = entry->num_results_drawn;
	}
}

static void previous_cursor_or_result(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;

	if (entry->cursor_theme.show
			&& entry->selection == 0
			&& entry->cursor_position > 0) {
		entry->cursor_position--;
		entry->selection_anchor = entry->cursor_position;
	} else {
		select_previous_result(tofi);
	}
}

static void next_cursor_or_result(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;

	if (entry->cursor_theme.show
			&& entry->cursor_position < entry->input_utf32_length) {
		entry->cursor_position++;
		entry->selection_anchor = entry->cursor_position;
	} else {
		select_next_result(tofi);
	}
}

static void select_previous_page(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;

	if (entry->first_result >= entry->last_num_results_drawn) {
		entry->first_result -= entry->last_num_results_drawn;
	} else {
		entry->first_result = 0;
	}
	entry->selection = 0;
	entry->last_num_results_drawn = entry->num_results_drawn;
}

static void select_next_page(struct tofi *tofi)
{
	struct entry *entry = &tofi->window.entry;

	entry->first_result += entry->num_results_drawn;
	if (entry->first_result >= entry->results.count) {
		entry->first_result = 0;
	}
	entry->selection = 0;
	entry->last_num_results_drawn = entry->num_results_drawn;
}
