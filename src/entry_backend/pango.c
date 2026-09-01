#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <pango/pango.h>
#include "../entry.h"
#include "../log.h"
#include "../nelem.h"
#include "../unicode.h"
#include "../xmalloc.h"

#undef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#undef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static void rounded_rectangle(cairo_t *cr, uint32_t width, uint32_t height, uint32_t r)
{
	cairo_new_path(cr);

	/* Top-left */
	cairo_arc(cr, r, r, r, -M_PI, -M_PI_2);

	/* Top-right */
	cairo_arc(cr, width - r, r, r, -M_PI_2, 0);

	/* Bottom-right */
	cairo_arc(cr, width - r, height - r, r, 0, M_PI_2);

	/* Bottom-left */
	cairo_arc(cr, r, height - r, r, M_PI_2, M_PI);

	cairo_close_path(cr);
}

static void clip_result_text(cairo_t *cr, struct entry *entry, double width)
{
	cairo_matrix_t mat;
	cairo_get_matrix(cr, &mat);
	cairo_rectangle(cr, 0, entry->clip_y - mat.y0, MAX(width, 0), entry->clip_height);
	cairo_clip(cr);
}

static double ellipsis_width(PangoLayout *layout)
{
	PangoLayout *ellipsis = pango_layout_copy(layout);
	pango_layout_set_text(ellipsis, "…", -1);
	PangoRectangle logical_rect;
	pango_layout_get_pixel_extents(ellipsis, NULL, &logical_rect);
	double width = logical_rect.x + logical_rect.width;
	g_object_unref(ellipsis);
	return width;
}

static void render_ellipsis(cairo_t *cr, PangoLayout *layout, double available_width, double width)
{
	cairo_save(cr);
	cairo_translate(cr, MAX(available_width - width, 0), 0);
	pango_layout_set_text(layout, "…", -1);
	pango_cairo_update_layout(cr, layout);
	pango_cairo_show_layout(cr, layout);
	cairo_restore(cr);
}

static void render_text_themed(
		cairo_t *cr,
		struct entry *entry,
		const char *text,
		const struct text_theme *theme,
		PangoRectangle *ink_rect,
		PangoRectangle *logical_rect,
		double full_width,
		double inner_left,
		double inner_right)
{
	PangoLayout *layout = entry->pango.layout;
	pango_layout_set_attributes(layout, NULL);
	struct color color = theme->foreground_color;
	cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

	pango_layout_set_text(layout, text, -1);
	pango_cairo_update_layout(cr, layout);
	/* Preserve the existing two-pass background rendering unless the new
	 * full-width geometry is active. */
	if (full_width <= 0) {
		pango_cairo_show_layout(cr, layout);
	}
	pango_layout_get_pixel_extents(layout, ink_rect, logical_rect);
	double available_width = full_width - inner_left - inner_right;
	bool ellipsized = full_width > 0 && logical_rect->x + logical_rect->width > available_width;
	double end_marker_width = ellipsized ? ellipsis_width(layout) : 0;
	double text_clip_width = available_width - end_marker_width;
	if (theme->background_color.a == 0) {
		if (full_width > 0) {
			cairo_save(cr);
			clip_result_text(cr, entry, ellipsized ? text_clip_width : available_width);
			pango_cairo_show_layout(cr, layout);
			cairo_restore(cr);
			if (ellipsized) {
				render_ellipsis(cr, layout, available_width, end_marker_width);
			}
		}
		return;
	}

	struct directional padding = theme->padding;

	cairo_matrix_t mat;
	cairo_get_matrix(cr, &mat);
	int32_t base_x = mat.x0 - entry->clip_x + ink_rect->x;
	int32_t base_y = mat.y0 - entry->clip_y;

	double padding_left = padding.left;
	double padding_right = padding.right;
	double padding_top = padding.top;
	double padding_bottom = padding.bottom;

	if (padding_left < 0) {
		padding_left = base_x;
	}
	if (padding_right < 0) {
		padding_right = entry->clip_width - ink_rect->width - base_x;
	}
	if (padding_top < 0) {
		padding_top = base_y;
	}
	if (padding_bottom < 0) {
		padding_bottom = entry->clip_height - logical_rect->height - base_y;
	}

	cairo_save(cr);
	color = theme->background_color;
	cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
	if (full_width > 0) {
		cairo_translate(cr, -inner_left, -padding_top);
		rounded_rectangle(
				cr,
				ceil(full_width),
				ceil(logical_rect->height + padding_top + padding_bottom),
				theme->background_corner_radius);
	} else {
		cairo_translate(
				cr,
				-padding_left + ink_rect->x,
				-padding_top);
		rounded_rectangle(
				cr,
				ceil(ink_rect->width + padding_left + padding_right),
				ceil(logical_rect->height + padding_top + padding_bottom),
				theme->background_corner_radius);
	}
	cairo_fill(cr);
	cairo_restore(cr);

	color = theme->foreground_color;
	cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
	if (full_width > 0) {
		cairo_save(cr);
		clip_result_text(cr, entry, ellipsized ? text_clip_width : available_width);
		pango_cairo_show_layout(cr, layout);
		cairo_restore(cr);
		if (ellipsized) {
			render_ellipsis(cr, layout, available_width, end_marker_width);
		}
	} else {
		pango_cairo_show_layout(cr, layout);
	}
}

static const char *utf8_at(const char *text, uint32_t position)
{
	for (uint32_t i = 0; i < position; i++) {
		text = utf8_next_char(text);
	}
	return text;
}

static void set_input_selection_attributes(
		PangoLayout *layout,
		const char *text,
		uint32_t cursor_position,
		uint32_t selection_anchor,
		const struct cursor_theme *cursor_theme)
{
	pango_layout_set_attributes(layout, NULL);
	if (cursor_position == selection_anchor) {
		return;
	}

	uint32_t start = MIN(cursor_position, selection_anchor);
	uint32_t end = MAX(cursor_position, selection_anchor);
	uint32_t start_index = utf8_at(text, start) - text;
	uint32_t end_index = utf8_at(text, end) - text;

	PangoAttrList *attributes = pango_attr_list_new();
	PangoAttribute *background = pango_attr_background_new(
			cursor_theme->color.r * UINT16_MAX,
			cursor_theme->color.g * UINT16_MAX,
			cursor_theme->color.b * UINT16_MAX);
	PangoAttribute *background_alpha = pango_attr_background_alpha_new(
			cursor_theme->color.a * 0.5 * UINT16_MAX);
	PangoAttribute *foreground = pango_attr_foreground_new(
			cursor_theme->text_color.r * UINT16_MAX,
			cursor_theme->text_color.g * UINT16_MAX,
			cursor_theme->text_color.b * UINT16_MAX);
	background->start_index = background_alpha->start_index
		= foreground->start_index = start_index;
	background->end_index = background_alpha->end_index
		= foreground->end_index = end_index;
	pango_attr_list_insert(attributes, background);
	pango_attr_list_insert(attributes, background_alpha);
	pango_attr_list_insert(attributes, foreground);
	pango_layout_set_attributes(layout, attributes);
	pango_attr_list_unref(attributes);
}

static void render_input(
		cairo_t *cr,
		PangoLayout *layout,
		const char *text,
		uint32_t text_length,
		const struct text_theme *theme,
		uint32_t cursor_position,
		uint32_t selection_anchor,
		const struct cursor_theme *cursor_theme,
		PangoRectangle *ink_rect,
		PangoRectangle *logical_rect)
{
	struct directional padding = theme->padding;
	struct color color = theme->foreground_color;
	cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

	pango_layout_set_text(layout, text, -1);
	set_input_selection_attributes(
			layout,
			text,
			cursor_position,
			selection_anchor,
			cursor_theme);
	pango_cairo_update_layout(cr, layout);
	pango_cairo_show_layout(cr, layout);

	pango_layout_get_pixel_extents(layout, ink_rect, logical_rect);

	double extra_cursor_advance = 0;
	if (cursor_position == text_length && cursor_theme->show) {
		switch (cursor_theme->style) {
			case CURSOR_STYLE_BAR:
				extra_cursor_advance = cursor_theme->thickness;
				break;
			case CURSOR_STYLE_BLOCK:
				extra_cursor_advance = cursor_theme->em_width;
				break;
			case CURSOR_STYLE_UNDERSCORE:
				extra_cursor_advance = cursor_theme->em_width;
				break;
		}
		extra_cursor_advance += logical_rect->width
			- logical_rect->x
			- ink_rect->width;
	}

	if (theme->background_color.a != 0) {
		cairo_save(cr);
		color = theme->background_color;
		cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
		cairo_translate(
				cr,
				floor(-padding.left + ink_rect->x),
				-padding.top);
		rounded_rectangle(
				cr,
				ceil(ink_rect->width + extra_cursor_advance + padding.left + padding.right),
				ceil(logical_rect->height + padding.top + padding.bottom),
				theme->background_corner_radius
				);
		cairo_fill(cr);
		cairo_restore(cr);

		color = theme->foreground_color;
		cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

		pango_cairo_show_layout(cr, layout);
	}

	if (!cursor_theme->show) {
		/* No cursor to draw, we're done. */
		return;
	}

	double cursor_x;
	double cursor_width;

	if (cursor_position == text_length) {
		cursor_x = logical_rect->width + logical_rect->x;
		cursor_width = cursor_theme->em_width;
	} else {
		/*
		 * Pango wants a byte index rather than a character index for
		 * the cursor position, so we have to calculate that here.
		 */
		const char *tmp = utf8_at(text, cursor_position);
		uint32_t start_byte_index = tmp - text;
		uint32_t end_byte_index = utf8_next_char(tmp) - text;
		PangoRectangle start_pos;
		PangoRectangle end_pos;
		pango_layout_get_cursor_pos(layout, start_byte_index, &start_pos, NULL);
		pango_layout_get_cursor_pos(layout, end_byte_index, &end_pos, NULL);
		cursor_x = (double)start_pos.x / PANGO_SCALE;
		cursor_width = (double)(end_pos.x - start_pos.x) / PANGO_SCALE;;
	}

	cairo_save(cr);
	color = cursor_theme->color;
	cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
	cairo_translate(cr, cursor_x, 0);
	switch (cursor_theme->style) {
		case CURSOR_STYLE_BAR:
			rounded_rectangle(cr, cursor_theme->thickness, logical_rect->height, cursor_theme->corner_radius);
			cairo_fill(cr);
			break;
		case CURSOR_STYLE_BLOCK:
			rounded_rectangle(cr, cursor_width, logical_rect->height, cursor_theme->corner_radius);
			cairo_fill_preserve(cr);
			cairo_clip(cr);
			cairo_translate(cr, -cursor_x, 0);
			color = cursor_theme->text_color;
			cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
			pango_cairo_show_layout(cr, layout);
			break;
		case CURSOR_STYLE_UNDERSCORE:
			cairo_translate(cr, 0, cursor_theme->underline_depth);
			rounded_rectangle(cr, cursor_width, cursor_theme->thickness, cursor_theme->corner_radius);
			cairo_fill(cr);
			break;
	}

	logical_rect->width += extra_cursor_advance;
	cairo_restore(cr);
}

void entry_backend_pango_init(struct entry *entry, uint32_t *width, uint32_t *height)
{
	cairo_t *cr = entry->cairo[0].cr;

	/* Setup Pango. */
	log_debug("Creating Pango context.\n");
	PangoContext *context = pango_cairo_create_context(cr);

	log_debug("Creating Pango font description.\n");
	PangoFontDescription *font_description =
		pango_font_description_from_string(entry->font_name);
	pango_font_description_set_size(
			font_description,
			entry->font_size * PANGO_SCALE);
	if (entry->font_variations[0] != 0) {
		pango_font_description_set_variations(
				font_description,
				entry->font_variations);
	}
	pango_context_set_font_description(context, font_description);

	entry->pango.layout = pango_layout_new(context);

	if (entry->font_features[0] != 0) {
		log_debug("Setting font features.\n");
		PangoAttribute *attr = pango_attr_font_features_new(entry->font_features);
		PangoAttrList *attr_list = pango_attr_list_new();
		pango_attr_list_insert(attr_list, attr);
		pango_layout_set_attributes(entry->pango.layout, attr_list);
	}

	log_debug("Loading Pango font.\n");
	PangoFontMap *map = pango_cairo_font_map_get_default();
	PangoFont *font = pango_font_map_load_font(map, context, font_description);
	PangoFontMetrics *metrics = pango_font_get_metrics(font, NULL);
	hb_font_t *hb_font = pango_font_get_hb_font(font);

	uint32_t m_codepoint;
	if (hb_font_get_glyph_from_name(hb_font, "m", -1, &m_codepoint)) {
		entry->cursor_theme.em_width = (double)hb_font_get_glyph_h_advance(hb_font, m_codepoint) / PANGO_SCALE;
	} else {
		entry->cursor_theme.em_width = (double)pango_font_metrics_get_approximate_char_width(metrics) / PANGO_SCALE;
	}

	entry->cursor_theme.underline_depth = (double)
		(
		 pango_font_metrics_get_ascent(metrics)
		 - pango_font_metrics_get_underline_position(metrics)
		) / PANGO_SCALE;
	if (entry->cursor_theme.style == CURSOR_STYLE_UNDERSCORE && !entry->cursor_theme.thickness_specified) {
		entry->cursor_theme.thickness = pango_font_metrics_get_underline_thickness(metrics) / PANGO_SCALE;
	}

	pango_font_metrics_unref(metrics);
	g_object_unref(font);
	log_debug("Loaded.\n");

	pango_font_description_free(font_description);

	entry->pango.context = context;
}

void entry_backend_pango_destroy(struct entry *entry)
{
	g_object_unref(entry->pango.layout);
	g_object_unref(entry->pango.context);
}

static bool size_overflows(struct entry *entry, uint32_t width, uint32_t height)
{
	cairo_t *cr = entry->cairo[entry->index].cr;
	cairo_matrix_t mat;
	cairo_get_matrix(cr, &mat);
	if (entry->horizontal) {
		if (mat.x0 + width > entry->clip_x + entry->clip_width) {
			return true;
		}
	} else {
		if (mat.y0 + height > entry->clip_y + entry->clip_height) {
			return true;
		}
	}
	return false;
}

/*
 * This is pretty much a direct translation of the corresponding function in
 * the harfbuzz backend. As that's the one that I care about most, there are
 * more explanatory comments than there are here, so go look at that if you
 * want to understand how tofi's text rendering works.
 */
void entry_backend_pango_update(struct entry *entry)
{
	cairo_t *cr = entry->cairo[entry->index].cr;
	PangoLayout *layout = entry->pango.layout;

	cairo_save(cr);

	/* Render the prompt */
	PangoRectangle ink_rect;
	PangoRectangle logical_rect;
	render_text_themed(cr, entry, entry->prompt_text, &entry->prompt_theme,
			&ink_rect, &logical_rect, 0, 0, 0);

	cairo_translate(cr, logical_rect.width + logical_rect.x, 0);
	cairo_translate(cr, entry->prompt_padding, 0);

	/* Render the entry text */
	if (entry->input_utf8_length == 0) {
		render_input(
				cr,
				layout,
				entry->placeholder_text,
				utf8_strlen(entry->placeholder_text),
				&entry->placeholder_theme,
				0,
				0,
				&entry->cursor_theme,
				&ink_rect,
				&logical_rect);
	} else if (entry->hide_input) {
		size_t nchars = entry->input_utf32_length;
		size_t char_size = entry->hidden_character_utf8_length;
		char *buf = xmalloc(1 + nchars * char_size);
		for (size_t i = 0; i < nchars; i++) {
			for (size_t j = 0; j < char_size; j++) {
				buf[i * char_size + j] = entry->hidden_character_utf8[j];
			}
		}
		buf[char_size * nchars] = '\0';

		render_input(
				cr,
				layout,
				buf,
				entry->input_utf32_length,
				&entry->input_theme,
				entry->cursor_position,
				entry->selection_anchor,
				&entry->cursor_theme,
				&ink_rect,
				&logical_rect);
		free(buf);
	} else {
		render_input(
				cr,
				layout,
				entry->input_utf8,
				entry->input_utf32_length,
				&entry->input_theme,
				entry->cursor_position,
				entry->selection_anchor,
				&entry->cursor_theme,
				&ink_rect,
				&logical_rect);
	}
	logical_rect.width = MAX(logical_rect.width, (int)entry->input_width);

	struct color color = entry->foreground_color;
	cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

	uint32_t num_results;
	if (entry->num_results == 0) {
		num_results = entry->results.count;
	} else {
		num_results = MIN(entry->num_results, entry->results.count);
	}
	/* Input selection attributes belong only to the input layout. */
	pango_layout_set_attributes(layout, NULL);
	bool custom_row_geometry = !entry->horizontal
		&& (entry->result_x_padding_specified || entry->result_full_width);
	double row_width = 0;
	double row_inner_left = 0;
	double row_inner_right = 0;
	if (custom_row_geometry) {
		double row_padding = MIN(entry->result_x_padding, entry->clip_width / 2);
		row_width = entry->clip_width - 2 * row_padding;
		if (entry->result_full_width) {
			row_inner_left = MAX(0, MAX(
					entry->default_result_theme.padding.left,
					MAX(entry->alternate_result_theme.padding.left,
						entry->selection_theme.padding.left)));
			row_inner_right = MAX(0, MAX(
					entry->default_result_theme.padding.right,
					MAX(entry->alternate_result_theme.padding.right,
						entry->selection_theme.padding.right)));
		}
		cairo_matrix_t mat;
		cairo_get_matrix(cr, &mat);
		cairo_translate(cr, entry->clip_x + row_padding - mat.x0, 0);
	}
	/* Render our results */
	size_t i;
	for (i = 0; i < num_results; i++) {
		if (entry->horizontal) {
			cairo_translate(cr, logical_rect.x + logical_rect.width + entry->result_spacing, 0);
		} else {
			cairo_translate(cr, 0, logical_rect.height + entry->result_spacing);
		}
		if (entry->num_results == 0) {
			uint32_t height = entry->horizontal ? 0 : logical_rect.height;
			if (size_overflows(entry, 0, height)) {
				break;
			}
		} else if (i >= entry->num_results) {
			break;
		}
		size_t index = i + entry->first_result;
		/*
		 * We may be on the last page, which could have fewer results
		 * than expected, so check and break if necessary.
		 */
		if (index >= entry->results.count) {
			break;
		}

		const char *str;
		if (i < entry->results.count) {
			str = entry->results.buf[index].string;
		} else {
			str = "";
		}
		const struct text_theme *theme;
		if (i == entry->selection) {
			theme = &entry->selection_theme;
		} else if (index % 2) {
			theme = &entry->alternate_result_theme;
		} else {
			theme = &entry->default_result_theme;
		}

		double inner_left = 0;
		double inner_right = 0;
		if (custom_row_geometry) {
			cairo_save(cr);
			cairo_matrix_t mat;
			cairo_get_matrix(cr, &mat);
			cairo_rectangle(cr, 0, entry->clip_y - mat.y0, row_width, entry->clip_height);
			cairo_clip(cr);
			if (entry->result_full_width) {
				inner_left = row_inner_left;
				inner_right = row_inner_right;
				cairo_translate(cr, inner_left, 0);
			}
		}

		if (i != entry->selection || (entry->selection_highlight_color.a == 0)) {

			if (entry->num_results > 0) {
				render_text_themed(cr, entry, str, theme, &ink_rect, &logical_rect,
						entry->result_full_width ? row_width : 0, inner_left, inner_right);
			} else if (!entry->horizontal) {
				render_text_themed(cr, entry, str, theme, &ink_rect, &logical_rect,
						entry->result_full_width ? row_width : 0, inner_left, inner_right);
			} else {
				cairo_push_group(cr);
				render_text_themed(cr, entry, str, theme, &ink_rect, &logical_rect, 0, 0, 0);

				cairo_pattern_t *group = cairo_pop_group(cr);
				if (size_overflows(entry, logical_rect.width, 0)) {
					entry->num_results_drawn = i;
					cairo_pattern_destroy(group);
					break;
				} else {
					cairo_save(cr);
					cairo_set_source(cr, group);
					cairo_paint(cr);
					cairo_restore(cr);
					cairo_pattern_destroy(group);
				}
			}
		} else {
			ssize_t prematch_len = -1;
			ssize_t postmatch_len = -1;
			size_t match_len = entry->input_utf8_length;
			PangoRectangle ink_subrect;
			PangoRectangle logical_subrect;
			if (entry->input_utf8_length > 0 && entry->selection_highlight_color.a != 0) {
				char *match_pos = utf8_strcasestr(str, entry->input_utf8);
				if (match_pos != NULL) {
					prematch_len = (match_pos - str);
					postmatch_len = strlen(str) - prematch_len - match_len;
					if (postmatch_len <= 0) {
						postmatch_len = -1;
					}
				}
			}

			bool full_selection_background = entry->result_full_width
				&& custom_row_geometry
				&& entry->selection_theme.background_color.a != 0;
			double selection_available_width = row_width - inner_left - inner_right;
			bool selection_ellipsized = false;
			double selection_ellipsis_width = 0;
			for (int pass = 0; pass < 2; pass++) {
				if (full_selection_background && pass == 0) {
					cairo_push_group(cr);
				}
				cairo_save(cr);
				if (full_selection_background && pass == 1) {
					selection_ellipsized = logical_rect.width > selection_available_width;
					selection_ellipsis_width = selection_ellipsized ? ellipsis_width(layout) : 0;
					clip_result_text(cr, entry, selection_available_width - selection_ellipsis_width);
				}
				color = entry->selection_theme.foreground_color;
				cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

				pango_layout_set_text(layout, str, prematch_len);
				pango_cairo_update_layout(cr, layout);
				pango_cairo_show_layout(cr, layout);
				pango_layout_get_pixel_extents(entry->pango.layout, &ink_subrect, &logical_subrect);
				ink_rect = ink_subrect;
				logical_rect = logical_subrect;

				if (prematch_len != -1) {
					cairo_translate(cr, logical_subrect.x + logical_subrect.width, 0);
					color = entry->selection_highlight_color;
					cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
					pango_layout_set_text(layout, &str[prematch_len], match_len);
					pango_cairo_update_layout(cr, layout);
					pango_cairo_show_layout(cr, layout);
					pango_layout_get_pixel_extents(entry->pango.layout, &ink_subrect, &logical_subrect);
					if (prematch_len == 0) {
						ink_rect = ink_subrect;
						logical_rect = logical_subrect;
					} else {
						ink_rect.width = logical_rect.width
							- ink_rect.x
							+ ink_subrect.x
							+ ink_subrect.width;
						logical_rect.width += logical_subrect.x + logical_subrect.width;
					}
				}

				if (postmatch_len != -1) {
					cairo_translate(cr, logical_subrect.x + logical_subrect.width, 0);
					color = entry->selection_theme.foreground_color;
					cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
					pango_layout_set_text(layout, &str[prematch_len + match_len], -1);
					pango_cairo_update_layout(cr, layout);
					pango_cairo_show_layout(cr, layout);
					pango_layout_get_pixel_extents(entry->pango.layout, &ink_subrect, &logical_subrect);
					ink_rect.width = logical_rect.width
						- ink_rect.x
						+ ink_subrect.x
						+ ink_subrect.width;
					logical_rect.width += logical_subrect.x + logical_subrect.width;

				}

				cairo_restore(cr);
				if (full_selection_background && pass == 1 && selection_ellipsized) {
					color = entry->selection_theme.foreground_color;
					cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
					render_ellipsis(
							cr, layout, selection_available_width, selection_ellipsis_width);
				}
				if (full_selection_background && pass == 0) {
					cairo_pattern_t *group = cairo_pop_group(cr);
					cairo_pattern_destroy(group);
				}

				if (entry->selection_theme.background_color.a == 0) {
					break;
				} else if (pass == 0) {
					struct directional padding = entry->selection_theme.padding;
					cairo_save(cr);
					color = entry->selection_theme.background_color;
					cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
					if (entry->result_full_width && custom_row_geometry) {
						cairo_translate(cr, -inner_left, -padding.top);
						rounded_rectangle(cr, ceil(row_width),
								ceil(logical_rect.height + padding.top + padding.bottom),
								entry->selection_theme.background_corner_radius);
					} else {
						cairo_translate(cr, floor(-padding.left + ink_rect.x), -padding.top);
						rounded_rectangle(cr,
								ceil(ink_rect.width + padding.left + padding.right),
								ceil(logical_rect.height + padding.top + padding.bottom),
								entry->selection_theme.background_corner_radius);
					}
					cairo_fill(cr);
					cairo_restore(cr);
				}
			}
		}
		if (custom_row_geometry) {
			cairo_restore(cr);
		}
	}
	entry->num_results_drawn = i;
	log_debug("Drew %zu results.\n", i);

	cairo_restore(cr);
}
