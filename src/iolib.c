#include <stdlib.h>
#include <stdint.h>

static void log_latencies(const char *scenario, const uint64_t *lat, size_t n, size_t bsz) {
	for (size_t i = 0; i < n; i++) {
		csv_write_row(1, scenario, i, lat[i], bsz);
	}
}