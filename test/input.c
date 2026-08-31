#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include "input.h"
#include "tap.h"
#include "tofi.h"
#include "unicode.h"

static void set_input(struct tofi *tofi, const char *text)
{
	struct entry *entry = &tofi->window.entry;
	strcpy(entry->input_utf8, text);
	entry->input_utf8_length = strlen(text);
	entry->input_utf32_length = 0;
	for (const char *ch = text; *ch != '\0'; ch = utf8_next_char(ch)) {
		entry->input_utf32[entry->input_utf32_length++] = utf8_to_utf32(ch);
	}
	entry->input_utf32[entry->input_utf32_length] = U'\0';
	entry->cursor_position = entry->input_utf32_length;
	entry->selection_anchor = entry->cursor_position;
}

static void modifier(struct tofi *tofi, uint32_t key, enum xkb_key_direction direction)
{
	xkb_state_update_key(tofi->xkb_state, key + 8, direction);
}

static void press(struct tofi *tofi, uint32_t key)
{
	input_handle_keypress(tofi, key + 8);
}

int main(void)
{
	tap_version(14);
	struct tofi tofi = {
		.physical_keybindings = true,
		.window.entry.cursor_theme.show = true,
	};
	tofi.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	tofi.xkb_keymap = xkb_keymap_new_from_names(
			tofi.xkb_context,
			NULL,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
	tofi.xkb_state = xkb_state_new(tofi.xkb_keymap);

	set_input(&tofi, "one two");
	modifier(&tofi, KEY_LEFTCTRL, XKB_KEY_DOWN);
	press(&tofi, KEY_A);
	tap_is(tofi.window.entry.selection_anchor, 0, "Ctrl-a anchors at start");
	tap_is(tofi.window.entry.cursor_position, 7, "Ctrl-a moves cursor to end");

	modifier(&tofi, KEY_LEFTSHIFT, XKB_KEY_DOWN);
	press(&tofi, KEY_LEFT);
	tap_is(tofi.window.entry.cursor_position, 4, "Ctrl-Shift-Left selects by word");
	tap_is(tofi.window.entry.selection_anchor, 0, "Shift movement preserves anchor");
	modifier(&tofi, KEY_LEFTSHIFT, XKB_KEY_UP);
	press(&tofi, KEY_A);
	modifier(&tofi, KEY_LEFTCTRL, XKB_KEY_UP);

	press(&tofi, KEY_X);
	tap_is(strcmp(tofi.window.entry.input_utf8, "x"), 0, "Typing replaces selected text");
	tap_is(tofi.window.entry.cursor_position, 1, "Replacement leaves cursor after text");
	tap_is(tofi.window.entry.selection_anchor, 1, "Replacement clears selection");

	set_input(&tofi, "one  two");
	modifier(&tofi, KEY_LEFTCTRL, XKB_KEY_DOWN);
	press(&tofi, KEY_LEFT);
	tap_is(tofi.window.entry.cursor_position, 5, "Ctrl-Left moves to previous word");
	press(&tofi, KEY_LEFT);
	tap_is(tofi.window.entry.cursor_position, 0, "Ctrl-Left skips repeated whitespace");
	press(&tofi, KEY_RIGHT);
	tap_is(tofi.window.entry.cursor_position, 5, "Ctrl-Right moves to next word");
	modifier(&tofi, KEY_LEFTCTRL, XKB_KEY_UP);

	set_input(&tofi, "abc");
	modifier(&tofi, KEY_LEFTSHIFT, XKB_KEY_DOWN);
	press(&tofi, KEY_LEFT);
	modifier(&tofi, KEY_LEFTSHIFT, XKB_KEY_UP);
	press(&tofi, KEY_BACKSPACE);
	tap_is(strcmp(tofi.window.entry.input_utf8, "ab"), 0, "Backspace deletes selected text");
	tap_is(tofi.window.entry.cursor_position, 2, "Selection deletion positions cursor");

	set_input(&tofi, "abc");
	press(&tofi, KEY_HOME);
	tap_is(tofi.window.entry.cursor_position, 0, "Home moves cursor to start");
	modifier(&tofi, KEY_LEFTSHIFT, XKB_KEY_DOWN);
	press(&tofi, KEY_END);
	modifier(&tofi, KEY_LEFTSHIFT, XKB_KEY_UP);
	tap_is(tofi.window.entry.selection_anchor, 0, "Shift-End preserves selection anchor");
	tap_is(tofi.window.entry.cursor_position, 3, "Shift-End selects to end");
	press(&tofi, KEY_DELETE);
	tap_is(strcmp(tofi.window.entry.input_utf8, ""), 0, "Delete removes selected text");

	set_input(&tofi, "one two");
	press(&tofi, KEY_HOME);
	modifier(&tofi, KEY_LEFTCTRL, XKB_KEY_DOWN);
	press(&tofi, KEY_DELETE);
	modifier(&tofi, KEY_LEFTCTRL, XKB_KEY_UP);
	tap_is(strcmp(tofi.window.entry.input_utf8, "two"), 0, "Ctrl-Delete deletes next word");
	press(&tofi, KEY_DELETE);
	tap_is(strcmp(tofi.window.entry.input_utf8, "wo"), 0, "Delete deletes next character");

	xkb_state_unref(tofi.xkb_state);
	xkb_keymap_unref(tofi.xkb_keymap);
	xkb_context_unref(tofi.xkb_context);
	tap_plan();
	return EXIT_SUCCESS;
}
