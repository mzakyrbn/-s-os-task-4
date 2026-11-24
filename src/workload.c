#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <fcntl.h>

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

	int fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);

	if (strcmp(mode, "seqwrite") == 0) {
		
	}
	else if (strcmp(mode, "randwrite") == 0) {

	}
	else if (strcmp(mode, "seqread") == 0) {

	}
	else if (strcmp(mode, "randwread") == 0) {

	}
	else {
		printf("hrrruaaaah what are you doing blyat haiyaaah"); // nga ini ga di submit nanti w ganti
	}
}