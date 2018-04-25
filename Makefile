WAYLAND_FLAGS = `pkg-config wayland-client --cflags --libs`
WAYLAND_PROTOCOLS_DIR = `pkg-config wayland-protocols --variable=pkgdatadir`
WAYLAND_SCANNER = `pkg-config --variable=wayland_scanner wayland-scanner`
CFLAGS ?= -std=c11 -Wall -Werror

XDG_SHELL_FILES=xdg-shell-client-protocol.h xdg-shell-protocol.c
CAT_FILES=cat.c cat.h
OS_CREATE_ANONYMOUS_FILE_FILES=os-create-anonymous-file.c os-create-anonymous-file.h

all: hello-wayland

hello-wayland: main.c $(XDG_SHELL_FILES) $(CAT_FILES) $(OS_CREATE_ANONYMOUS_FILE_FILES)
	$(CC) $(CFLAGS) -o $@ $(WAYLAND_FLAGS) *.c

xdg-shell-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) code $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c

.PHONY: clean
clean:
	$(RM) hello-wayland $(XDG_SHELL_FILES)
