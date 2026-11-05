/*
 * tsm_bench: feeds a file to tsm_vte_input() in chunks and reports the speed.
 * It has no GUI, so it can run under perf:
 *
 *   perf record -g ./tsm_bench -n 20 compile_commands.json
 *   perf report --no-children
 *
 * Usage: tsm_bench [-n repeats] [-c chunk_bytes] [-r rows] [-C cols]
 *                  [-s scrollback_lines] file
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libtsm.h"

static void write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data)
{
	(void)vte;
	(void)u8;
	(void)len;
	(void)data;
}

static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	long size;

	if (!f) {
		perror(path);
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(size > 0 ? (size_t)size : 1);
	if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
		fprintf(stderr, "cannot read %s\n", path);
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*len = (size_t)size;
	return buf;
}

int main(int argc, char **argv)
{
	unsigned int rows = 50, cols = 200, sb = 5000;
	size_t chunk = 64 * 1024, len = 0, off, n;
	int repeats = 10, opt, i;
	struct tsm_screen *screen;
	struct tsm_vte *vte;
	double start, ms, best = 0;
	char *data;

	while ((opt = getopt(argc, argv, "n:c:r:C:s:")) != -1) {
		switch (opt) {
		case 'n': repeats = atoi(optarg); break;
		case 'c': chunk = (size_t)atol(optarg); break;
		case 'r': rows = (unsigned int)atoi(optarg); break;
		case 'C': cols = (unsigned int)atoi(optarg); break;
		case 's': sb = (unsigned int)atoi(optarg); break;
		default:
			fprintf(stderr,
				"usage: %s [-n repeats] [-c chunk] [-r rows] "
				"[-C cols] [-s scrollback] file\n", argv[0]);
			return 2;
		}
	}
	if (optind >= argc || chunk == 0 || repeats < 1) {
		fprintf(stderr, "a file is required\n");
		return 2;
	}
	data = read_file(argv[optind], &len);
	if (!data)
		return 1;

	if (tsm_screen_new(&screen, NULL, NULL) ||
	    tsm_screen_resize(screen, cols, rows))
		return 1;
	tsm_screen_set_max_sb(screen, sb);
	if (tsm_vte_new(&vte, screen, write_cb, NULL, NULL, NULL))
		return 1;

	for (i = 0; i < repeats; ++i) {
		start = now_ms();
		for (off = 0; off < len; off += n) {
			n = len - off < chunk ? len - off : chunk;
			tsm_vte_input(vte, data + off, n);
		}
		ms = now_ms() - start;
		if (best == 0 || ms < best)
			best = ms;
		printf("run %2d: %8.2f ms  %7.1f MB/s\n", i + 1, ms,
		       len / 1e6 / (ms / 1e3));
	}
	printf("best   : %8.2f ms  %7.1f MB/s  (%zu bytes, %ux%u, sb %u, "
	       "chunk %zu)\n", best, len / 1e6 / (best / 1e3), len, cols, rows,
	       sb, chunk);

	tsm_vte_unref(vte);
	tsm_screen_unref(screen);
	free(data);
	return 0;
}
