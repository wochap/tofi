#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "clipboard.h"
#include "log.h"
#include "xmalloc.h"

static const char mime_type_text_plain_utf8[] = "text/plain;charset=utf-8";
static const char mime_type_text_plain[] = "text/plain";

static void wl_data_source_target(
		void *data,
		struct wl_data_source *source,
		const char *mime_type)
{
}

static void wl_data_source_send(
		void *data,
		struct wl_data_source *source,
		const char *mime_type,
		int32_t fd)
{
	struct clipboard *clipboard = data;
	const char *text = clipboard->copied_text == NULL ? "" : clipboard->copied_text;
	size_t remaining = strlen(text);

	while (remaining > 0) {
		ssize_t written = write(fd, text, remaining);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			log_error("Failed to write clipboard data: %s\n", strerror(errno));
			break;
		}
		text += written;
		remaining -= written;
	}
	close(fd);
}

static void wl_data_source_cancelled(
		void *data,
		struct wl_data_source *source)
{
	struct clipboard *clipboard = data;
	wl_data_source_destroy(source);
	if (clipboard->wl_data_source == source) {
		clipboard->wl_data_source = NULL;
		free(clipboard->copied_text);
		clipboard->copied_text = NULL;
	}
}

static const struct wl_data_source_listener wl_data_source_listener = {
	.target = wl_data_source_target,
	.send = wl_data_source_send,
	.cancelled = wl_data_source_cancelled,
};

static void clipboard_destroy_source(struct clipboard *clipboard)
{
	if (clipboard->wl_data_source != NULL) {
		wl_data_source_destroy(clipboard->wl_data_source);
		clipboard->wl_data_source = NULL;
	}
	free(clipboard->copied_text);
	clipboard->copied_text = NULL;
}

void clipboard_finish_paste(struct clipboard *clipboard)
{
	if (clipboard->fd > 0) {
		close(clipboard->fd);
		clipboard->fd = 0;
	}
}

void clipboard_reset(struct clipboard *clipboard)
{
	if (clipboard->wl_data_offer != NULL) {
		wl_data_offer_destroy(clipboard->wl_data_offer);
		clipboard->wl_data_offer = NULL;
	}
	clipboard_finish_paste(clipboard);
	clipboard->mime_type = NULL;
}

void clipboard_copy(
		struct clipboard *clipboard,
		struct wl_data_device_manager *manager,
		struct wl_data_device *device,
		uint32_t serial,
		const char *text)
{
	clipboard_destroy_source(clipboard);
	clipboard->copied_text = xstrdup(text);
	clipboard->wl_data_source = wl_data_device_manager_create_data_source(manager);
	wl_data_source_add_listener(
			clipboard->wl_data_source,
			&wl_data_source_listener,
			clipboard);
	wl_data_source_offer(clipboard->wl_data_source, mime_type_text_plain_utf8);
	wl_data_source_offer(clipboard->wl_data_source, mime_type_text_plain);
	wl_data_device_set_selection(device, clipboard->wl_data_source, serial);
}

void clipboard_destroy(struct clipboard *clipboard)
{
	clipboard_reset(clipboard);
	clipboard_destroy_source(clipboard);
}
