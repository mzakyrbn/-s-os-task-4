#ifndef IOLIB_H
#define IOLIB_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Block-device counters read from /sys/block/<device>/stat. */
typedef struct {
    uint64_t ios;
    uint64_t merges;
    uint64_t sectors;
} io_device_stats;

/* Latencies and percentiles are expressed in nanoseconds. */
typedef struct {
    size_t count;
    double average_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t p50_ns;
    uint64_t p95_ns;
    uint64_t p99_ns;
} io_latency_stats;

/* Returns CLOCK_MONOTONIC time in nanoseconds, or 0 if clock_gettime fails. */
uint64_t io_now_ns(void);

/* Compatibility names used by the current workload implementation. */
uint64_t now_ns(void);
void log_latencies(const char *scenario, const uint64_t *latencies_ns,
                   size_t count, size_t bytes_per_operation, char* fileNameCopy, size_t fileNameCopySize);

/* Reads Dirty and Writeback from /proc/meminfo (values are in kB). */
int io_read_memory_metrics(uint64_t *dirty_kb, uint64_t *writeback_kb);

/* Reads write counters for a device name such as "loop0". */
int io_read_device_stats(const char *device, io_device_stats *stats, char *mode);

/* Computes average, min, max and nearest-rank p50/p95/p99 for samples. */
int io_compute_latency_stats(const uint64_t *samples_ns, size_t count,
                             io_latency_stats *stats);

/* Makes logs/ if needed, then creates a timestamped CSV and writes its header.
   The returned FILE* is owned by the caller and should be closed with fclose. */
int io_csv_open(FILE **file, const char *group, const char *program,
                const char *scenario, char *path, size_t path_size, char *fileNameCopy, size_t fileNameCopySize);

/* Writes one row using the assignment's run_id,scenario,op_index,latency_ns,bytes
   CSV format. */
int io_csv_write_row(FILE *file, uint64_t run_id, const char *scenario,
                     uint64_t op_index, uint64_t latency_ns, uint64_t bytes);

/* Saves kernel, RAM, dirty settings, and lsblk output in logs/<group>-env-*.txt. */
int io_write_environment_snapshot(const char *group, char *path, size_t path_size);

/* ensuring file in the path is not empty */
int ensure_file(const char *path, uint64_t size_bytes, size_t bsz);

#endif /* IOLIB_H */
