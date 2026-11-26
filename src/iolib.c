#define _POSIX_C_SOURCE 200809L

#include "iolib.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *safe_token(const char *text, const char *fallback,
                              char *buffer, size_t buffer_size)
{
    size_t i;

    if (text == NULL || *text == '\0') {
        text = fallback;
    }
    if (buffer_size == 0) {
        return "";
    }
    for (i = 0; text[i] != '\0' && i + 1 < buffer_size; ++i) {
        unsigned char ch = (unsigned char)text[i];
        buffer[i] = (isalnum(ch) || ch == '-' || ch == '_') ? (char)ch : '_';
    }
    buffer[i] = '\0';
    return buffer;
}

static int make_timestamp(char *buffer, size_t buffer_size)
{
    struct timespec now;
    struct tm local;
    char date_time[32];

    if (clock_gettime(CLOCK_REALTIME, &now) != 0 ||
        localtime_r(&now.tv_sec, &local) == NULL ||
        strftime(date_time, sizeof(date_time), "%Y%m%d-%H%M%S", &local) == 0) {
        return -1;
    }
    if (snprintf(buffer, buffer_size, "%s-%09ld", date_time, now.tv_nsec) >=
        (int)buffer_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int ensure_logs_directory(void)
{
    struct stat info;

    if (mkdir("logs", 0755) == 0 || errno == EEXIST) {
        if (stat("logs", &info) == 0 && S_ISDIR(info.st_mode)) {
            return 0;
        }
        if (errno == 0) {
            errno = ENOTDIR;
        }
    }
    return -1;
}

uint64_t io_now_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

uint64_t now_ns(void)
{
    return io_now_ns();
}

int io_read_memory_metrics(uint64_t *dirty_kb, uint64_t *writeback_kb)
{
    FILE *file;
    char line[256];
    uint64_t dirty = 0;
    uint64_t writeback = 0;
    int got_dirty = 0;
    int got_writeback = 0;

    if (dirty_kb == NULL || writeback_kb == NULL) {
        errno = EINVAL;
        return -1;
    }
    file = fopen("/proc/meminfo", "r");
    if (file == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long long value;
        if (sscanf(line, "Dirty: %llu kB", &value) == 1) {
            dirty = (uint64_t)value;
            got_dirty = 1;
        } else if (sscanf(line, "Writeback: %llu kB", &value) == 1) {
            writeback = (uint64_t)value;
            got_writeback = 1;
        }
    }
    if (ferror(file)) {
        int saved_errno = errno;
        fclose(file);
        errno = saved_errno != 0 ? saved_errno : EIO;
        return -1;
    }
    fclose(file);
    if (!got_dirty || !got_writeback) {
        errno = ENODATA;
        return -1;
    }
    *dirty_kb = dirty;
    *writeback_kb = writeback;
    return 0;
}

int io_read_device_stats(const char *device, io_device_stats *stats)
{
    char path[PATH_MAX];
    char line[512];
    size_t i;
    FILE *file;
    unsigned long long fields[7];

    if (device == NULL || stats == NULL || *device == '\0') {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; device[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)device[i];
        if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.')) {
            errno = EINVAL;
            return -1;
        }
    }
    if (snprintf(path, sizeof(path), "/sys/block/%s/stat", device) >=
        (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    if (fgets(line, sizeof(line), file) == NULL) {
        int saved_errno = ferror(file) ? errno : EIO;
        fclose(file);
        errno = saved_errno;
        return -1;
    }
    fclose(file);
    if (sscanf(line, "%llu %llu %llu %llu %llu %llu %llu",
               &fields[0], &fields[1], &fields[2], &fields[3],
               &fields[4], &fields[5], &fields[6]) != 7) {
        errno = EINVAL;
        return -1;
    }
    stats->write_ios = (uint64_t)fields[4];
    stats->write_merges = (uint64_t)fields[5];
    stats->write_sectors = (uint64_t)fields[6];
    return 0;
}

static int compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static uint64_t nearest_rank(const uint64_t *sorted, size_t count,
                             unsigned int percentile)
{
    size_t rank = (count / 100) * percentile +
                  ((count % 100) * percentile + 99) / 100;
    return sorted[rank - 1];
}

int io_compute_latency_stats(const uint64_t *samples_ns, size_t count,
                             io_latency_stats *stats)
{
    uint64_t *sorted;
    long double total = 0.0L;
    size_t i;

    if (samples_ns == NULL || stats == NULL || count == 0 ||
        count > SIZE_MAX / sizeof(*sorted)) {
        errno = EINVAL;
        return -1;
    }
    sorted = malloc(count * sizeof(*sorted));
    if (sorted == NULL) {
        return -1;
    }
    memcpy(sorted, samples_ns, count * sizeof(*sorted));
    qsort(sorted, count, sizeof(*sorted), compare_u64);
    for (i = 0; i < count; ++i) {
        total += (long double)samples_ns[i];
    }
    stats->count = count;
    stats->average_ns = (double)(total / (long double)count);
    stats->min_ns = sorted[0];
    stats->max_ns = sorted[count - 1];
    stats->p50_ns = nearest_rank(sorted, count, 50);
    stats->p95_ns = nearest_rank(sorted, count, 95);
    stats->p99_ns = nearest_rank(sorted, count, 99);
    free(sorted);
    return 0;
}

int io_csv_open(FILE **file, const char *group, const char *program,
                const char *scenario, char *path, size_t path_size)
{
    char group_name[64];
    char program_name[64];
    char scenario_name[64];
    char timestamp[48];
    int length;

    if (file == NULL || path == NULL || path_size == 0) {
        errno = EINVAL;
        return -1;
    }
    *file = NULL;
    if (ensure_logs_directory() != 0 || make_timestamp(timestamp, sizeof(timestamp)) != 0) {
        return -1;
    }
    safe_token(group, "XX", group_name, sizeof(group_name));
    safe_token(program, "program", program_name, sizeof(program_name));
    safe_token(scenario, "scenario", scenario_name, sizeof(scenario_name));
    length = snprintf(path, path_size, "logs/%s-%s-%s-%s.csv",
                      group_name, program_name, scenario_name, timestamp);
    if (length < 0 || (size_t)length >= path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    *file = fopen(path, "w");
    if (*file == NULL) {
        return -1;
    }
    if (fputs("run_id,scenario,op_index,latency_ns,bytes\n", *file) == EOF) {
        int saved_errno = errno != 0 ? errno : EIO;
        fclose(*file);
        *file = NULL;
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static int write_csv_field(FILE *file, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    int quoted = 0;

    for (; *cursor != '\0'; ++cursor) {
        if (*cursor == ',' || *cursor == '"' || *cursor == '\n' || *cursor == '\r') {
            quoted = 1;
            break;
        }
    }
    if (quoted && fputc('"', file) == EOF) {
        return -1;
    }
    for (cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (*cursor == '"' && fputc('"', file) == EOF) {
            return -1;
        }
        if (fputc(*cursor, file) == EOF) {
            return -1;
        }
    }
    return !quoted || fputc('"', file) != EOF ? 0 : -1;
}

int io_csv_write_row(FILE *file, uint64_t run_id, const char *scenario,
                     uint64_t op_index, uint64_t latency_ns, uint64_t bytes)
{
    if (file == NULL || scenario == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (fprintf(file, "%llu,", (unsigned long long)run_id) < 0 ||
        write_csv_field(file, scenario) != 0 ||
        fprintf(file, ",%llu,%llu,%llu\n", (unsigned long long)op_index,
                (unsigned long long)latency_ns, (unsigned long long)bytes) < 0) {
        if (errno == 0) {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}

void log_latencies(const char *scenario, const uint64_t *latencies_ns,
                   size_t count, size_t bytes_per_operation)
{
    FILE *file = NULL;
    char path[PATH_MAX];
    size_t i;

    if (scenario == NULL || (count != 0 && latencies_ns == NULL)) {
        errno = EINVAL;
        perror("log_latencies");
        return;
    }
    if (io_csv_open(&file, "06", "workload", scenario, path,
                    sizeof(path)) != 0) {
        perror("log_latencies: opening CSV");
        return;
    }
    for (i = 0; i < count; ++i) {
        if (io_csv_write_row(file, 1, scenario, (uint64_t)i,
                             latencies_ns[i],
                             (uint64_t)bytes_per_operation) != 0) {
            perror("log_latencies: writing CSV row");
            fclose(file);
            return;
        }
    }
    if (fclose(file) != 0) {
        perror("log_latencies: closing CSV");
    }
}

static int write_sysctl_value(FILE *output, const char *label, const char *path)
{
    char value[128];
    FILE *input = fopen(path, "r");

    if (input == NULL) {
        return fprintf(output, "%s : unavailable\n", label) < 0 ? -1 : 0;
    }
    if (fgets(value, sizeof(value), input) == NULL) {
        fclose(input);
        return fprintf(output, "%s : unavailable\n", label) < 0 ? -1 : 0;
    }
    fclose(input);
    value[strcspn(value, "\r\n")] = '\0';
    return fprintf(output, "%s : %s\n", label, value) < 0 ? -1 : 0;
}

int io_write_environment_snapshot(const char *group, char *path,
                                  size_t path_size)
{
    char group_name[64];
    char timestamp[48];
    struct utsname kernel;
    FILE *output;
    FILE *input;
    char line[512];
    uint64_t total_ram_kb = 0;
    int length;

    if (path == NULL || path_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_logs_directory() != 0 || make_timestamp(timestamp, sizeof(timestamp)) != 0) {
        return -1;
    }
    safe_token(group, "XX", group_name, sizeof(group_name));
    length = snprintf(path, path_size, "logs/%s-env-%s.txt", group_name, timestamp);
    if (length < 0 || (size_t)length >= path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    output = fopen(path, "w");
    if (output == NULL) {
        return -1;
    }

    if (uname(&kernel) == 0) {
        if (fprintf(output, "Kernel : %s %s\n", kernel.release, kernel.machine) < 0) {
            goto write_error;
        }
    } else if (fprintf(output, "Kernel : unavailable\n") < 0) {
        goto write_error;
    }

    input = fopen("/proc/meminfo", "r");
    if (input != NULL) {
        while (fgets(line, sizeof(line), input) != NULL) {
            unsigned long long value;
            if (sscanf(line, "MemTotal: %llu kB", &value) == 1) {
                total_ram_kb = (uint64_t)value;
                break;
            }
        }
        fclose(input);
    }
    if (total_ram_kb != 0) {
        if (fprintf(output, "RAM : %llu kB\n",
                    (unsigned long long)total_ram_kb) < 0) {
            goto write_error;
        }
    } else if (fprintf(output, "RAM : unavailable\n") < 0) {
        goto write_error;
    }

    if (write_sysctl_value(output, "vm.dirty_ratio",
                           "/proc/sys/vm/dirty_ratio") != 0 ||
        write_sysctl_value(output, "vm.dirty_background_ratio",
                           "/proc/sys/vm/dirty_background_ratio") != 0 ||
        write_sysctl_value(output, "Read-ahead (loop0, kB)",
                           "/sys/block/loop0/queue/read_ahead_kb") != 0) {
        goto write_error;
    }
    if (fputs("lsblk :\n", output) == EOF) {
        goto write_error;
    }
    input = popen("lsblk", "r");
    if (input == NULL) {
        if (fputs("  unavailable\n", output) == EOF) {
            goto write_error;
        }
    } else {
        while (fgets(line, sizeof(line), input) != NULL) {
            if (fprintf(output, "  %s", line) < 0) {
                pclose(input);
                goto write_error;
            }
        }
        pclose(input);
    }
    if (fclose(output) != 0) {
        return -1;
    }
    return 0;

write_error:
    {
        int saved_errno = errno != 0 ? errno : EIO;
        fclose(output);
        errno = saved_errno;
    }
    return -1;
}
