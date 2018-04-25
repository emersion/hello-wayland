#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int set_cloexec(int fd) {
	long flags = fcntl(fd, F_GETFD);
	if (flags == -1) {
		return -1;
	}
	return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

int os_create_anonymous_file(off_t size) {
	static const char template[] = "/hello-wayland-shared-XXXXXX";

	const char *path = getenv("XDG_RUNTIME_DIR");
	if (path == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *name = malloc(strlen(path) + sizeof(template));
	if (name == NULL) {
		return -1;
	}
	strcpy(name, path);
	strcat(name, template);

	int fd = mkstemp(name);
	if (fd < 0) {
		free(name);
		return -1;
	}
	unlink(name);
	free(name);

	if (set_cloexec(fd) < 0) {
		close(fd);
		return -1;
	}

	if (ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}
