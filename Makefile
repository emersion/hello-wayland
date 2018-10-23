WAYLAND_FLAGS = `pkg-config wayland-client wayland-egl --cflags --libs`
GL_FLAGS = `pkg-config egl glesv2 --cflags --libs`
WAYLAND_PROTOCOLS_DIR = `pkg-config wayland-protocols --variable=pkgdatadir`
WAYLAND_SCANNER = `pkg-config --variable=wayland_scanner wayland-scanner`
CFLAGS ?= -std=c11 -Wall -Werror -g

XDG_SHELL_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml

XDG_SHELL_FILES=xdg-shell-client-protocol.h xdg-shell-protocol.c

all: hello-wayland

hello-wayland: main.c $(XDG_SHELL_FILES)
	$(CC) $(CFLAGS) -o hello-wayland $(WAYLAND_FLAGS) $(GL_FLAGS) *.c

xdg-shell-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(XDG_SHELL_PROTOCOL) xdg-shell-client-protocol.h

xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) code $(XDG_SHELL_PROTOCOL) xdg-shell-protocol.c

.PHONY: clean
clean:
	$(RM) hello-wayland $(XDG_SHELL_FILES)
