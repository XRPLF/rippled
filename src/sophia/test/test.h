#ifndef SP_TEST_H_
#define SP_TEST_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#define t(expr) ({ \
	if (! (expr)) { \
		printf("fail (%s:%d) %s\n", __FILE__, __LINE__, #expr); \
		fflush(NULL); \
		abort(); \
	} \
})

#define test(f) ({ \
	printf("%s: ", #f); \
	fflush(NULL); \
	f(); \
	printf("ok\n"); \
	fflush(NULL); \
})

static inline int
exists(char *path, uint32_t epoch, char *ext) {
	char file[1024];
	snprintf(file, sizeof(file), "%s/%"PRIu32".%s", path, epoch, ext);
	struct stat st;
	return lstat(file, &st) == 0;
}

static inline int rmrf(char *path) {
	DIR *d = opendir(path);
	if (d == NULL)
		return -1;
	char file[1024];
	struct dirent *de;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(file, sizeof(file), "%s/%s", path, de->d_name);
		int rc = unlink(file);
		if (rc == -1) {
			closedir(d);
			return -1;
		}
	}
	closedir(d);
	return rmdir(path);
}

#endif
