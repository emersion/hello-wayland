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

static bool configured = false;
static uint32_t initial_configure_serial = 0;
static bool running = true;

static struct wl_shm *shm = NULL;
static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;

static void *shm_data = NULL;
static struct wl_surface *surface = NULL;
static struct xdg_toplevel *xdg_toplevel = NULL;

static void xdg_wm_base_handle_ping(void *data,
		struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	// The compositor will send us a ping event to check that we're responsive.
	// We need to send back a pong request immediately.
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_handle_ping,
};

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	if (configured) {
		// If this isn't the first configure event we've received, we already
		// have a buffer attached, so no need to do anything. Commit the
		// surface to apply the configure acknowledgement.
		xdg_surface_ack_configure(xdg_surface, serial);
		wl_surface_commit(surface);
	} else {
		// Store the configure serial to apply it later, when we perform the
		// initial commit.
		initial_configure_serial = serial;
		configured = true;
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	// This event is sent before xdg_surface.configure. It specifies the
	// compositor's desired size and advertises active states for the toplevel.
	// A resizable client would store these, and resize itself when receiving
	// the xdg_surface.configure event.
}

static void xdg_toplevel_handle_close(void *data,
		struct xdg_toplevel *xdg_toplevel) {
	// Stop running if the user requests to close the toplevel
	running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

static void pointer_handle_enter(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface, int32_t x, int32_t y) {
	// This space intentionally left blank
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface) {
	// This space intentionally left blank
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer,
		uint32_t time_ms, int32_t x, int32_t y) {
	// This space intentionally left blank
}

static void pointer_handle_button(void *data, struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct wl_seat *seat = data;

	// If the user presses the left pointer button, start an interactive move
	// of the toplevel
	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		xdg_toplevel_move(xdg_toplevel, seat, serial);
	}
}

static void pointer_handle_axis(void *data, struct wl_pointer *pointer,
		uint32_t time_ms, uint32_t axis, wl_fixed_t value) {
	// This space intentionally left blank
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
		uint32_t capabilities) {
	// If the wl_seat has the pointer capability, start listening to pointer
	// events
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		struct wl_pointer *pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, seat);
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct wl_seat *seat =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);
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

static struct wl_buffer *create_buffer(void) {
	int stride = width * 4;
	int size = stride * height;
	int offset = 0;

	// Allocate a shared memory file with the right size
	int fd = create_shm_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
		return NULL;
	}

	// Map the shared memory file
	shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
	if (shm_data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	// Create a wl_buffer from our shared memory file descriptor
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, offset, width, height,
		stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);

	// Now that we've mapped the file and created the wl_buffer, we no longer
	// need to keep file descriptor opened
	close(fd);

	// Copy pixels into our shared memory file (MagickImage is from cat.h)
	memcpy(shm_data, MagickImage, size);

	// We don't need the mapped memory anymore
	munmap(shm_data, size);

	return buffer;
}

int main(int argc, char *argv[]) {
	// Connect to the Wayland compositor
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return EXIT_FAILURE;
	}

	// Obtain the wl_registry and fetch the list of globals
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	if (wl_display_roundtrip(display) == -1) {
		return EXIT_FAILURE;
	}

	// Check that all globals we require are available
	if (shm == NULL || compositor == NULL || xdg_wm_base == NULL) {
		fprintf(stderr, "no wl_shm, wl_compositor or xdg_wm_base support\n");
		return EXIT_FAILURE;
	}

	// Create a wl_surface, a xdg_surface and a xdg_toplevel
	surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg_surface =
		xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	// Perform the initial commit and wait for the first configure event
	wl_surface_commit(surface);
	while (!configured) {
		if (wl_display_dispatch(display) == -1) {
			return EXIT_FAILURE;
		}
	}

	// Create a wl_buffer, attach it to the surface and commit the surface
	struct wl_buffer *buffer = create_buffer();
	if (buffer == NULL) {
		return EXIT_FAILURE;
	}

	wl_surface_attach(surface, buffer, 0, 0);
	xdg_surface_ack_configure(xdg_surface, initial_configure_serial);
	wl_surface_commit(surface);

	// Continue dispatching events until the user closes the toplevel
	while (wl_display_dispatch(display) != -1 && running) {
		// This space intentionally left blank
	}

	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(surface);
	wl_buffer_destroy(buffer);

	return EXIT_SUCCESS;
}
