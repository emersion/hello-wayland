#define _POSIX_C_SOURCE 199309L
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <wayland-egl.h>
#ifdef __linux__
#include <linux/input-event-codes.h>
#elif __FreeBSD__
#include <dev/evdev/input-event-codes.h>
#endif

#include "xdg-shell-client-protocol.h"

static const int width = 128;
static const int height = 128;

static bool running = true;

static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;

static struct wl_surface *surface = NULL;
static struct xdg_toplevel *xdg_toplevel = NULL;

static EGLDisplay egl_display = NULL;
static EGLContext egl_context = NULL;
static EGLSurface egl_surface = NULL;

static struct timespec last_frame = {0};
static float color[3] = {0};
static size_t dec = 0;

static void noop() {
	// This space intentionally left blank
}

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_surface_ack_configure(xdg_surface, serial);
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

static void pointer_handle_button(void *data, struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct wl_seat *seat = data;

	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		xdg_toplevel_move(xdg_toplevel, seat, serial);
	}
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = noop,
	.leave = noop,
	.motion = noop,
	.button = pointer_handle_button,
	.axis = noop,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
		uint32_t capabilities) {
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		struct wl_pointer *pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, seat);
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
};

static void render(void);

static void frame_handle_done(void *data, struct wl_callback *callback,
		uint32_t time) {
	wl_callback_destroy(callback);
	render();
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_handle_done,
};

static void render(void) {
	// Update color
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	long ms = (ts.tv_sec - last_frame.tv_sec) * 1000 +
		(ts.tv_nsec - last_frame.tv_nsec) / 1000000;
	size_t inc = (dec + 1) % 3;
	color[inc] += ms / 2000.0f;
	color[dec] -= ms / 2000.0f;
	if (color[dec] < 0.0f) {
		color[inc] = 1.0f;
		color[dec] = 0.0f;
		dec = inc;
	}
	last_frame = ts;

	// And draw a new frame
	if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
		fprintf(stderr, "eglMakeCurrent failed\n");
		exit(EXIT_FAILURE);
	}

	glClearColor(color[0], color[1], color[2], 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	// By default, eglSwapBuffers blocks until we receive the next frame event.
	// This is undesirable since it makes it impossible to process other events
	// (such as input events) while waiting for the next frame event. Setting
	// the swap interval to zero and managing frame events manually prevents
	// this behavior.
	eglSwapInterval(egl_display, 0);

	// Register a frame callback to know when we need to draw the next frame
	struct wl_callback *callback = wl_surface_frame(surface);
	wl_callback_add_listener(callback, &frame_listener, NULL);

	// This will attach a new buffer and commit the surface
	if (!eglSwapBuffers(egl_display, egl_surface)) {
		fprintf(stderr, "eglSwapBuffers failed\n");
		exit(EXIT_FAILURE);
	}
}

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct wl_seat *seat =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor =
			wl_registry_bind(registry, name, &wl_compositor_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		xdg_wm_base =
			wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
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

int main(int argc, char *argv[]) {
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (compositor == NULL || xdg_wm_base == NULL) {
		fprintf(stderr, "no wl_shm, wl_compositor or xdg_wm_base support\n");
		return EXIT_FAILURE;
	}

	egl_display = eglGetDisplay((EGLNativeDisplayType)display);
	if (egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "failed to create EGL display\n");
		return EXIT_FAILURE;
	}

	EGLint major, minor;
	if (!eglInitialize(egl_display, &major, &minor)) {
		fprintf(stderr, "failed to initialize EGL\n");
		return EXIT_FAILURE;
	}

	EGLint count;
	eglGetConfigs(egl_display, NULL, 0, &count);

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};
	EGLint n = 0;
	EGLConfig *configs = calloc(count, sizeof(EGLConfig));
	eglChooseConfig(egl_display, config_attribs, configs, count, &n);
	if (n == 0) {
		fprintf(stderr, "failed to choose an EGL config\n");
		return EXIT_FAILURE;
	}
	EGLConfig egl_config = configs[0];

	EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};
	egl_context = eglCreateContext(egl_display, egl_config,
		EGL_NO_CONTEXT, context_attribs);

	surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg_surface =
		xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	struct wl_egl_window *egl_window =
		wl_egl_window_create(surface, width, height);
	egl_surface = eglCreateWindowSurface(egl_display, egl_config,
		(EGLNativeWindowType)egl_window, NULL);

	wl_surface_commit(surface);
	wl_display_roundtrip(display);

	// Draw the first frame
	render();

	while (wl_display_dispatch(display) != -1 && running) {
		// This space intentionally left blank
	}

	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(surface);

	return EXIT_SUCCESS;
}
