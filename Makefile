WAYLAND_FLAGS = $(shell pkg-config wayland-client --cflags --libs)
WAYLAND_PROTOCOLS_DIR = $(shell pkg-config wayland-protocols --variable=pkgdatadir)
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g

XDG_SHELL_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml

HEADERS=cat.h xdg-shell-client-protocol.h shm.h
SOURCES=main.c xdg-shell-protocol.c shm.c

all: hello-wayland

hello-wayland: $(HEADERS) $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) -lrt $(WAYLAND_FLAGS)

xdg-shell-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(XDG_SHELL_PROTOCOL) xdg-shell-client-protocol.h

xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(XDG_SHELL_PROTOCOL) xdg-shell-protocol.c

cat.h: cat.png
	convert cat.png -define h:format=bgra -depth 8 cat.h

.PHONY: clean
clean:
	$(RM) hello-wayland cat.h xdg-shell-protocol.c xdg-shell-client-protocol.h
