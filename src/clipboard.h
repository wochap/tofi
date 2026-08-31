#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <wayland-client.h>

struct clipboard {
	struct wl_data_offer *wl_data_offer;
	struct wl_data_source *wl_data_source;
	const char *mime_type;
	char *copied_text;
	int fd;
};

void clipboard_finish_paste(struct clipboard *clipboard);
void clipboard_reset(struct clipboard *clipboard);
void clipboard_copy(
		struct clipboard *clipboard,
		struct wl_data_device_manager *manager,
		struct wl_data_device *device,
		uint32_t serial,
		const char *text);
void clipboard_destroy(struct clipboard *clipboard);

#endif /* CLIPBOARD_H */
