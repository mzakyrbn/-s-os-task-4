#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
/*#include <unistd.h>*/
#include "iolib.h"
#include "banner.h"

int main(int argc, char* argv) {
	char *dir = NULL;
	char *mode = NULL;
	char *dev = NULL;

	int size = 0;
	int bs = 0;
	bool isFsync = false;

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
			isFsync = true;
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

	uint64_t dirty_before;
	uint64_t writeback_before;
	uint64_t dirty_after;
	uint64_t writeback_after;
	io_device_stats dev_before;
	io_device_stats dev_after;
	io_latency_stats lat_stats;
	char *filename;

	/* Only the write modes truncate the file; read modes open it themselves. */
	int fd = -1;
	if (strcmp(mode, "seqwrite") == 0 || strcmp(mode, "randwrite") == 0) {
		fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	}

	/* check mode */
	if (strcmp(mode, "seqwrite") == 0) {
		uint64_t start = now_ns;
		io_read_memory_metrics(&dirty_before, &writeback_before);
		io_read_device_stats(dev, &dev_before);

		char *buffer = malloc (bsz);
		memset(buffer, 0, bsz);

		for (size_t i = 0; i < n_ops; i++) {
			uint64_t before = now_ns();
			write(fd, buffer, bsz);
			total_bytes += bsz;
			uint64_t after = now_ns();
			lat[i] = after - before;
		}

		if (isFsync && fsync(fd) == -1) {
			perror("fsync");
		}
		elapsed_ns = now_ns() - start;

		io_read_memory_metrics(&dirty_after, &writeback_after);
		io_read_device_stats(dev, &dev_after);

		log_latencies("randread", lat, n_ops, bsz, filename);
		free(buffer);
		close(fd);
	}

	else if (strcmp(mode, "randwrite") == 0) {
		uint64_t start = now_ns;
		size_t random_block = (size_t)rand() % n_ops;
		off_t offset = (off_t)random_block * (off_t)bsz;

		io_read_memory_metrics(&dirty_before, &writeback_before);
		io_read_device_stats(dev, &dev_before);

		char *buffer = malloc (bsz);
		memset(buffer, 0, bsz);
		
		for (size_t i = 0; i < n_ops; i++) {
			uint64_t before = now_ns();
			pwrite(fd, buffer, bsz, rand() % offset);
			total_bytes += bsz;
			uint64_t after = now_ns();
			lat[i] = after - before;
		}
		
		if (isFsync && fsync(fd) == -1) {
			perror("fsync");
		}
		elapsed_ns = now_ns() - start;

		io_read_memory_metrics(&dirty_after, &writeback_after);
		io_read_device_stats(dev, &dev_after);

		log_latencies("randread", lat, n_ops, bsz, filename);
		free(buffer);
		close(fd);
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

		log_latencies("seqread", lat, n_ops, bsz, filename);
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

		log_latencies("randread", lat, n_ops, bsz, filename);
		free(buf);
		close(fd);
	}
	else {
		printf("hrrruaaaah what are you doing blyat haiyaaah"); // nga ini ga di submit nanti w ganti
	}

	io_compute_latency_stats(lat, n_ops, &lat_stats);
	double throughput = ((double)total_bytes / elapsed_ns) * 1e9 / (1024.0 * 1024.0);  
	uint64_t elapsed_s = elapsed_ns * 1e9;
	char dev_label[64];
	snprintf(dev_label, sizeof(dev_label), "%s stat", dev);

	uint64_t write_ios_delta = dev_after.write_ios - dev_before.write_ios;
	uint64_t write_merges_delta = dev_after.write_merges - dev_before.write_merges;
	uint64_t write_sectors_delta = dev_after.write_sectors - dev_before.write_sectors;

	banner_call();
	printf(
		"\n[sebelum]\n%-16s: %llu kB\n%-16s: %llu kB\n%-16s: write ios %llu | write merges %llu | write sectors %llu\n",
		"dirty", dirty_before, "Writeback", writeback_before, dev_label,
		dev_before.write_ios, dev_before.write_merges, dev_before.write_sectors  
	);
	printf(
		"\n[sesudah]\n%-16s: %llu kB\n%-16s: %llu kB\n%-16s: write ios %llu | write merges %llu | write sectors %llu\n",
		"dirty", dirty_after, "Writeback", writeback_after, dev_label,
		dev_after.write_ios, dev_after.write_merges, dev_after.write_sectors  
	);
	printf(
		"selisih %-16s: write ios %+llu | write merges %+llu | write sectors %+llu",
		dev, write_ios_delta, write_merges_delta, write_sectors_delta 
	);
	printf(
		"%-16s: %PRIu64 s\n%-16s: %f MB/s\n%-16s: avg %llu | min %llu | max %llu | p50 %llu | p95 %llu | p99 %llu\n%-16s: %s",
		"waktu eksekusi", dirty_after, "Throughput", writeback_after, "Latensi (ns)",
		dev_after.write_ios, dev_after.write_merges, dev_after.write_sectors, filename
	);
}