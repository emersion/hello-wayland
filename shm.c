#define _GNU_SOURCE
#include <unistd.h>
#include <sys/mman.h>

/**
 * Boilerplate to create an in-memory shared file.
 *
 * Link with `-lrt`.
 */

int create_shm_file(off_t size) {
	int fd = memfd_create("shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (fd < 0) {
		return fd;
	}

	if (ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}
