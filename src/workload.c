#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "iolib.h"

/*
 * Make sure the test file exists and is at least size_bytes long, so the
 * read modes have something to read. Writes bsz-sized blocks of non-zero
 * data, then fsyncs so the data is really on disk before we measure reads.
 * Returns 0 on success, -1 on error.
 */
static int ensure_file(const char *path, uint64_t size_bytes, size_t bsz) {
	struct stat st;
	if (stat(path, &st) == 0 && (uint64_t)st.st_size >= size_bytes) {
		return 0;
	}

	int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		perror("ensure_file: open");
		return -1;
	}

	char *buf = malloc(bsz);
	if (buf == NULL) {
		perror("ensure_file: malloc");
		close(fd);
		return -1;
	}
	memset(buf, 'A', bsz);

	uint64_t written = 0;
	while (written < size_bytes) {
		ssize_t w = write(fd, buf, bsz);
		if (w <= 0) {
			perror("ensure_file: write");
			free(buf);
			close(fd);
			return -1;
		}
		written += (uint64_t)w;
	}

	fsync(fd);
	free(buf);
	close(fd);
	return 0;
}

/*
 * Write one CSV row per operation. This is the only place that touches the
 * iolib CSV writer: if your teammate names it differently, change it here.
 */

int main(int argc, char* argv) {
	char *dir = NULL;
	char *mode = NULL;
	char *dev = NULL;

	int size = 0;
	int bs = 0;
	bool fsync = false;

	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--dir") == 0) {
			dir = argv[++i];
		}
		else if (strcmp(argv[i], "--size") == 0) {
			size = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "--bs") == 0) {
			bs = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "--mode") == 0) {
			mode = argv[++i];
		}
		else if (strcmp(argv[i], "--fsync") == 0) {
			fsync = true;
		}
		else if (strcmp(argv[i], "--dev") == 0) {
			dev = argv[++i];
		}
		else {
			continue;
		}
	}

	char filepath[_MAX_PATH];
	
	snprintf(filepath, sizeof(filepath), "%stestfile.dat", dir);

	/* Filled by every mode, used by the shared reporting code afterwards. */
	size_t bsz = (size_t)bs * 1024;
	size_t n_ops = ((size_t)size * 1024) / (size_t)bs;
	uint64_t *lat = calloc(n_ops, sizeof(uint64_t));
	uint64_t total_bytes = 0;
	uint64_t elapsed_ns = 0;

	/* Only the write modes truncate the file; read modes open it themselves. */
	int fd = -1;
	if (strcmp(mode, "seqwrite") == 0 || strcmp(mode, "randwrite") == 0) {
		fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	}

	if (strcmp(mode, "seqwrite") == 0) {
		int writeCnt = 0;
		size_t block_size = (size_t)bs * size;
		char *buffer = malloc (block_size);

		while (writeCnt + bs < size) {
			write(fd, buffer, bs);
			writeCnt += block_size;
		}

		if (isFsync && fsync(fd) == -1) {
			perror("fsync");
		}
	}
	else if (strcmp(mode, "randwrite") == 0) {
		int writeCnt = 0;
		size_t block_size = (size_t)bs * size;
		char *buffer = malloc (block_size);
		
		while (writeCnt + block_size < size) {
			pwrite(fd, buffer, bs, rand() % n_ops);
		}
		
		if (isFsync && fsync(fd) == -1) {
			perror("fsync");
		}
	}
	else if (strcmp(mode, "seqread") == 0) {
		if (ensure_file(filepath, (uint64_t)size * 1024 * 1024, bsz) != 0) {
			return 1;
		}

		fd = open(filepath, O_RDONLY);
		char *buf = malloc(bsz);
		if (fd < 0 || buf == NULL) {
			perror("seqread: open/malloc");
			return 1;
		}

		size_t done = 0;
		uint64_t start = now_ns();
		for (size_t i = 0; i < n_ops; i++) {
			uint64_t t0 = now_ns();
			ssize_t r = read(fd, buf, bsz);
			uint64_t t1 = now_ns();

			if (r < 0) {
				perror("seqread: read");
				break;
			}
			if (r == 0) {
				break; /* end of file */
			}
			lat[i] = t1 - t0;
			total_bytes += (uint64_t)r;
			done++;
		}
		elapsed_ns = now_ns() - start;
		n_ops = done;

		log_latencies("seqread", lat, n_ops, bsz);
		free(buf);
		close(fd);
	}
	else if (strcmp(mode, "randread") == 0) {
		if (ensure_file(filepath, (uint64_t)size * 1024 * 1024, bsz) != 0) {
			return 1;
		}

		fd = open(filepath, O_RDONLY);
		char *buf = malloc(bsz);
		if (fd < 0 || buf == NULL) {
			perror("randread: open/malloc");
			return 1;
		}

		srand((unsigned)time(NULL));

		size_t done = 0;
		uint64_t start = now_ns();
		for (size_t i = 0; i < n_ops; i++) {
			/* Random block index -> block-aligned offset inside the file. */
			off_t off = (off_t)((size_t)rand() % n_ops) * (off_t)bsz;

			uint64_t t0 = now_ns();
			ssize_t r = pread(fd, buf, bsz, off);
			uint64_t t1 = now_ns();

			if (r <= 0) {
				if (r < 0) {
					perror("randread: pread");
				}
				break;
			}
			lat[i] = t1 - t0;
			total_bytes += (uint64_t)r;
			done++;
		}
		elapsed_ns = now_ns() - start;
		n_ops = done;

		log_latencies("randread", lat, n_ops, bsz);
		free(buf);
		close(fd);
	}
	else {
		printf("hrrruaaaah what are you doing blyat haiyaaah"); // nga ini ga di submit nanti w ganti
	}
}