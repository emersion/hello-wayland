#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <linux/input-event-codes.h>

#include "cat.h"
#include "shm.h"
#include "xdg-shell-client-protocol.h"

static const int width = 128;
static const int height = 128;

static struct wl_display *display = NULL;
static bool running = true;

static struct wl_shm *shm = NULL;
static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;
static struct wl_seat *seat = NULL;
static struct wl_data_device_manager *data_device_manager = NULL;

static void *shm_data = NULL;
static struct wl_surface *surface = NULL;
static struct xdg_toplevel *xdg_toplevel = NULL;
static struct wl_data_device *data_device = NULL;

static uint32_t keyboard_enter_serial = 0;

static void noop() {
	// This space intentionally left blank
}

static const char text[] = "**Hello Wayland clipboard!**";
static const char html[] = "<strong>Hello Wayland clipboard!</strong>";

static void data_source_handle_send(void *data, struct wl_data_source *source,
		const char *mime_type, int fd) {
	// An application wants to paste the clipboard contents
	if (strcmp(mime_type, "text/plain") == 0) {
		write(fd, text, strlen(text));
	} else if (strcmp(mime_type, "text/html") == 0) {
		write(fd, html, strlen(html));
	} else {
		fprintf(stderr,
			"Destination client requested unsupported MIME type: %s\n",
			mime_type);
	}
	close(fd);
}

static void data_source_handle_cancelled(void *data,
		struct wl_data_source *source) {
	// An application has replaced the clipboard contents
	wl_data_source_destroy(source);
}

static const struct wl_data_source_listener data_source_listener = {
	.send = data_source_handle_send,
	.cancelled = data_source_handle_cancelled,
};

static void data_offer_handle_offer(void *data, struct wl_data_offer *offer,
		const char *mime_type) {
	printf("Clipboard supports MIME type: %s\n", mime_type);
}

static const struct wl_data_offer_listener data_offer_listener = {
	.offer = data_offer_handle_offer,
};

static void data_device_handle_data_offer(void *data,
		struct wl_data_device *data_device, struct wl_data_offer *offer) {
	wl_data_offer_add_listener(offer, &data_offer_listener, NULL);
}

static void data_device_handle_selection(void *data,
		struct wl_data_device *data_device, struct wl_data_offer *offer) {
	// An application has set the clipboard contents
	if (offer == NULL) {
		printf("Clipboard is empty\n");
		return;
	}

	int fds[2];
	pipe(fds);
	wl_data_offer_receive(offer, "text/plain", fds[1]);
	close(fds[1]);

	wl_display_roundtrip(display);

	// Read the clipboard contents and print it to the standard output.
	printf("Clipboard data:\n");
	while (true) {
		char buf[1024];
		ssize_t n = read(fds[0], buf, sizeof(buf));
		if (n <= 0) {
			break;
		}
		fwrite(buf, 1, n, stdout);
	}
	printf("\n");
	close(fds[0]);

	wl_data_offer_destroy(offer);
}

static const struct wl_data_device_listener data_device_listener = {
	.data_offer = data_device_handle_data_offer,
	.selection = data_device_handle_selection,
};

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_surface_ack_configure(xdg_surface, serial);
	wl_surface_commit(surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_close(void *data,
		struct xdg_toplevel *xdg_toplevel) {
	running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = noop,
	.close = xdg_toplevel_handle_close,
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
		uint32_t format, int fd, uint32_t size) {
	close(fd);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	keyboard_enter_serial = serial;
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	if (state != WL_KEYBOARD_KEY_STATE_RELEASED) {
		return;
	}

	struct wl_data_source *source =
		wl_data_device_manager_create_data_source(data_device_manager);
	// Setup a listener to receive wl_data_source events, more on this below
	wl_data_source_add_listener(source, &data_source_listener, NULL);
	// Advertise a few MIME types
	wl_data_source_offer(source, "text/plain");
	wl_data_source_offer(source, "text/html");
	wl_data_device_set_selection(data_device, source, keyboard_enter_serial);
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = keyboard_handle_enter,
	.leave = noop,
	.key = keyboard_handle_key,
	.modifiers = noop,
	.repeat_info = noop,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
		uint32_t capabilities) {
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		struct wl_keyboard *keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(keyboard, &keyboard_listener, seat);
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0 && seat == NULL) {
		seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	} else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		data_device_manager = wl_registry_bind(registry, name,
			&wl_data_device_manager_interface, 3);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// Who cares
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static struct wl_buffer *create_buffer() {
	int stride = width * 4;
	int size = stride * height;

	int fd = create_shm_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
		return NULL;
	}

	shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm_data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
		stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);

	// MagickImage is from cat.h
	memcpy(shm_data, MagickImage, size);
	return buffer;
}

int main(int argc, char *argv[]) {
	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (shm == NULL || compositor == NULL || xdg_wm_base == NULL
			|| seat == NULL || data_device_manager == NULL) {
		fprintf(stderr, "no wl_shm, wl_compositor, xdg_wm_base, wl_seat or "
			"wl_data_device_manager support\n");
		return EXIT_FAILURE;
	}

	data_device =
		wl_data_device_manager_get_data_device(data_device_manager, seat);
	wl_data_device_add_listener(data_device, &data_device_listener, NULL);

	struct wl_buffer *buffer = create_buffer();
	if (buffer == NULL) {
		return EXIT_FAILURE;
	}

	surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg_surface =
		xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	wl_surface_commit(surface);
	wl_display_roundtrip(display);

	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);

	while (wl_display_dispatch(display) != -1 && running) {
		// This space intentionally left blank
	}

	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(surface);
	wl_buffer_destroy(buffer);

	return EXIT_SUCCESS;
}
