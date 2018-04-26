WAYLAND_FLAGS = `pkg-config wayland-client --cflags --libs`
WAYLAND_PROTOCOLS_DIR = `pkg-config wayland-protocols --variable=pkgdatadir`
WAYLAND_SCANNER = `pkg-config --variable=wayland_scanner wayland-scanner`
CFLAGS ?= -std=c11 -Wall -Werror -g

XDG_SHELL_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml

XDG_SHELL_FILES=xdg-shell-client-protocol.h xdg-shell-protocol.c
OS_CREATE_ANONYMOUS_FILE_FILES=os-create-anonymous-file.c os-create-anonymous-file.h

all: hello-wayland

hello-wayland: main.c cat.h $(XDG_SHELL_FILES) $(OS_CREATE_ANONYMOUS_FILE_FILES)
	$(CC) $(CFLAGS) -o hello-wayland $(WAYLAND_FLAGS) *.c

xdg-shell-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(XDG_SHELL_PROTOCOL) xdg-shell-client-protocol.h

xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) code $(XDG_SHELL_PROTOCOL) xdg-shell-protocol.c

cat.h: cat.png
	convert cat.png -define h:format=bgra -depth 8 cat.h

.PHONY: clean
clean:
	$(RM) hello-wayland cat.h $(XDG_SHELL_FILES)
