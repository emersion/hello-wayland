#include <cglm/cglm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"

static const size_t nvertices = 12 * 3; /* 6 faces → 12 triangles */
static const GLfloat vertices[] = {
    -1.0f,-1.0f,-1.0f,
    -1.0f,-1.0f, 1.0f,
    -1.0f, 1.0f, 1.0f,
    1.0f, 1.0f,-1.0f,
    -1.0f,-1.0f,-1.0f,
    -1.0f, 1.0f,-1.0f,
    1.0f,-1.0f, 1.0f,
    -1.0f,-1.0f,-1.0f,
    1.0f,-1.0f,-1.0f,
    1.0f, 1.0f,-1.0f,
    1.0f,-1.0f,-1.0f,
    -1.0f,-1.0f,-1.0f,
    -1.0f,-1.0f,-1.0f,
    -1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f,-1.0f,
    1.0f,-1.0f, 1.0f,
    -1.0f,-1.0f, 1.0f,
    -1.0f,-1.0f,-1.0f,
    -1.0f, 1.0f, 1.0f,
    -1.0f,-1.0f, 1.0f,
    1.0f,-1.0f, 1.0f,
    1.0f, 1.0f, 1.0f,
    1.0f,-1.0f,-1.0f,
    1.0f, 1.0f,-1.0f,
    1.0f,-1.0f,-1.0f,
    1.0f, 1.0f, 1.0f,
    1.0f,-1.0f, 1.0f,
    1.0f, 1.0f, 1.0f,
    1.0f, 1.0f,-1.0f,
    -1.0f, 1.0f,-1.0f,
    1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f,-1.0f,
    -1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f, 1.0f,
    1.0f,-1.0f, 1.0f,
};

static const GLchar vertex_shader_src[] =
	"#version 100\n"
	"\n"
	"attribute vec3 pos;\n"
	"uniform mat4 mvp;\n"
	"\n"
	"varying vec3 vertex_pos;\n"
	"\n"
	"void main() {\n"
	"	gl_Position = mvp * vec4(pos, 1.0);\n"
	"	vertex_pos = pos;\n"
	"}\n";

static const GLchar fragment_shader_src[] =
	"#version 100\n"
	"precision mediump float;\n"
	"\n"
	"varying vec3 vertex_pos;\n"
	"\n"
	"void main() {\n"
	"	int n = 0;\n"
	"	float thr = 0.01;\n"
	"	if (1.0 - abs(vertex_pos.x) < thr) n++;\n"
	"	if (1.0 - abs(vertex_pos.y) < thr) n++;\n"
	"	if (1.0 - abs(vertex_pos.z) < thr) n++;\n"
	"	if (n >= 2) {\n"
	"		gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
	"	} else {\n"
	"		gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);\n"
	"	}\n"
	"}\n";

static int width = 2160;
static int height = 1200;
static uint32_t xdg_configure_serial = 0;

static bool running = true;

static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;

static struct wl_surface *surface = NULL;
static struct xdg_surface *xdg_surface = NULL;
static struct xdg_toplevel *xdg_toplevel = NULL;

static struct wl_egl_window *egl_window = NULL;
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;

static GLuint gl_prog = 0;

static void render(uint32_t time);

static void frame_handle_done(void *data, struct wl_callback *callback,
		uint32_t time) {
	wl_callback_destroy(callback);
	render(time);
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_handle_done,
};

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_configure_serial = serial;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data,
		struct xdg_toplevel *toplevel, int32_t w, int32_t h,
		struct wl_array *state) {
	if (w > 0) {
		width = w;
	}
	if (h > 0) {
		height = h;
	}
}

static void xdg_toplevel_handle_close(void *data,
		struct xdg_toplevel *xdg_toplevel) {
	running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
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

static GLuint compile_shader(GLuint type, const GLchar *src) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		fprintf(stderr, "Failed to compile shader: %s\n", log);
		return 0;
	}

	return shader;
}

static GLuint compile_program(void) {
	GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
	if (vertex_shader == 0) {
		fprintf(stderr, "Failed to compile vertex shader\n");
		return 0;
	}

	GLuint fragment_shader =
		compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
	if (fragment_shader == 0) {
		fprintf(stderr, "Failed to compile fragment shader\n");
		return 0;
	}

	GLuint shader_program = glCreateProgram();
	glAttachShader(shader_program, vertex_shader);
	glAttachShader(shader_program, fragment_shader);
	glLinkProgram(shader_program);
	GLint ok;
	glGetProgramiv(shader_program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetProgramInfoLog(shader_program, sizeof(log), NULL, log);
		fprintf(stderr, "Failed to link program: %s\n", log);
		return 0;
	}

	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	return shader_program;
}

static void render(uint32_t time) {
	if (xdg_configure_serial != 0) {
		wl_egl_window_resize(egl_window, width, height, 0, 0);
		xdg_surface_ack_configure(xdg_surface, xdg_configure_serial);
		xdg_configure_serial = 0;
	}

	if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
		fprintf(stderr, "eglMakeCurrent failed\n");
		return;
	}

	glViewport(0, 0, width, height);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(0.08, 0.07, 0.16, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	GLint pos_loc = glGetAttribLocation(gl_prog, "pos");
	GLint mvp_loc = glGetUniformLocation(gl_prog, "mvp");

	glUseProgram(gl_prog);

	mat4 model_matrix = GLM_MAT4_IDENTITY_INIT;
	glm_rotate_y(model_matrix, (float)time / 1000, model_matrix);
	glm_scale_uni(model_matrix, 0.5);

	mat4 view_matrix;
	vec3 eye = { 4.0, 3.0, 3.0 };
	vec3 center = { 0.0, 0.0, 0.0 };
	vec3 up = { 0.0, 1.0, 0.0 };
	glm_lookat(eye, center, up, view_matrix);

	mat4 projection_matrix;
	glm_perspective_default((float)width / height, projection_matrix);

	mat4 mvp_matrix = GLM_MAT4_IDENTITY_INIT;
	glm_mat4_mul(projection_matrix, view_matrix, mvp_matrix);
	glm_mat4_mul(mvp_matrix, model_matrix, mvp_matrix);

	glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, (GLfloat *)mvp_matrix);

	GLint coords_per_vertex =
		sizeof(vertices) / sizeof(vertices[0]) / nvertices;
	glVertexAttribPointer(pos_loc, coords_per_vertex,
		GL_FLOAT, GL_FALSE, 0, vertices);
	glEnableVertexAttribArray(pos_loc);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, nvertices);

	glDisableVertexAttribArray(pos_loc);

	glUseProgram(0);

	struct wl_callback *callback = wl_surface_frame(surface);
	wl_callback_add_listener(callback, &frame_listener, NULL);

	if (!eglSwapBuffers(egl_display, egl_surface)) {
		fprintf(stderr, "eglSwapBuffers failed\n");
	}
}

int main(int argc, char *argv[]) {
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (compositor == NULL || xdg_wm_base == NULL) {
		fprintf(stderr, "no wl_shm, wl_compositor or xdg_wm_base support\n");
		return 1;
	}

	egl_display = eglGetDisplay((EGLNativeDisplayType)display);
	if (egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "failed to create EGL display\n");
		return 1;
	}

	EGLint major, minor;
	if (!eglInitialize(egl_display, &major, &minor)) {
		fprintf(stderr, "failed to initialize EGL\n");
		return 1;
	}

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};
	EGLint n = 0;
	EGLConfig egl_config;
	eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &n);
	if (n == 0) {
		fprintf(stderr, "failed to choose an EGL config\n");
		return 1;
	}

	EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};
	egl_context = eglCreateContext(egl_display, egl_config,
		EGL_NO_CONTEXT, context_attribs);
	if (egl_context == EGL_NO_CONTEXT) {
		fprintf(stderr, "eglCreateContext failed\n");
		return 1;
	}

	surface = wl_compositor_create_surface(compositor);
	xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	wl_surface_commit(surface);
	wl_display_roundtrip(display);

	egl_window = wl_egl_window_create(surface, width, height);
	if (egl_window == NULL) {
		fprintf(stderr, "wl_egl_window_create failed\n");
		return 1;
	}

	egl_surface = eglCreateWindowSurface(egl_display, egl_config,
		(EGLNativeWindowType)egl_window, NULL);
	if (egl_surface == EGL_NO_SURFACE) {
		fprintf(stderr, "eglCreateWindowSurface failed\n");
		return 1;
	}

	if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
		fprintf(stderr, "eglMakeCurrent failed\n");
		return 1;
	}

	eglSwapInterval(egl_display, 0);

	gl_prog = compile_program();
	if (gl_prog == 0) {
		fprintf(stderr, "Failed to compile shader program\n");
		return 1;
	}

	render(0);

	while (wl_display_dispatch(display) != -1 && running) {
		// This space intentionally left blank
	}

	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(surface);

	return 0;
}
