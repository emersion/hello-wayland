#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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

static const int width = 512;
static const int height = 512;

static bool running = true;

static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;

static struct xdg_toplevel *xdg_toplevel = NULL;

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
		fprintf(stderr, "no wl_compositor or xdg_wm_base support\n");
		return EXIT_FAILURE;
	}

	const char *client_exts_str = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (client_exts_str == NULL) {
		fprintf(stderr, "failed to query EGL client extensions\n");
		return EXIT_FAILURE;
	}
	if (strstr(client_exts_str, "EGL_EXT_platform_wayland") == NULL) {
		fprintf(stderr, "missing EGL_EXT_platform_wayland\n");
		return EXIT_FAILURE;
	}

	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
		(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC eglCreatePlatformWindowSurfaceEXT =
		(void *)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");

	EGLDisplay egl_display = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT,
		display, NULL);
	if (egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "failed to create EGL display\n");
		return EXIT_FAILURE;
	}

	EGLint major, minor;
	if (!eglInitialize(egl_display, &major, &minor)) {
		fprintf(stderr, "failed to initialize EGL\n");
		return EXIT_FAILURE;
	}

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};
	EGLConfig egl_config;
	EGLint n = 0;
	if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &n) ||
			n == 0) {
		fprintf(stderr, "failed to choose EGL config\n");
		return EXIT_FAILURE;
	}

	EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};
	EGLContext egl_context = eglCreateContext(egl_display, egl_config,
		EGL_NO_CONTEXT, context_attribs);

	struct wl_surface *surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg_surface =
		xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

	xdg_toplevel_set_min_size(xdg_toplevel, width, height);
	xdg_toplevel_set_max_size(xdg_toplevel, width, height);

	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	wl_surface_commit(surface);
	wl_display_roundtrip(display);

	struct wl_egl_window *egl_window =
		wl_egl_window_create(surface, width, height);
	EGLSurface egl_surface = eglCreatePlatformWindowSurfaceEXT(egl_display,
		egl_config, egl_window, NULL);

	if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
		fprintf(stderr, "eglMakeCurrent failed\n");
		return EXIT_FAILURE;
	}

	eglSwapInterval(egl_display, 0);

	glClearColor(1.0, 1.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	if (!eglSwapBuffers(egl_display, egl_surface)) {
		fprintf(stderr, "eglSwapBuffers failed\n");
		return EXIT_FAILURE;
	}

	while (wl_display_dispatch(display) != -1 && running) {
		// This space intentionally left blank
	}

	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(surface);

	return EXIT_SUCCESS;
}
