/*
 * tsm_bench: feeds a file to tsm_vte_input() in chunks and reports the speed.
 * It has no GUI, so it can run under perf:
 *
 *   perf record -g ./tsm_bench -n 20 compile_commands.json
 *   perf report --no-children
 *
 * Usage: tsm_bench [-n repeats] [-c chunk_bytes] [-r rows] [-C cols]
 *                  [-s scrollback_lines] [-d] file
 *
 * -d feeds the file once to a new screen and prints a hash of the whole
 * result (visible cells, scrollback, cursor, flags). Build the benchmark from
 * two versions of libtsm and compare the hashes to check that a change keeps
 * the output the same. A short probe is fed after the file, so the parser
 * state at the end (current attribute, GL charset, an unfinished sequence)
 * also changes the hash.
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

static uint64_t hash_u64(uint64_t h, uint64_t v)
{
	return (h ^ v) * 1099511628211ULL; /* FNV-1a style, one word at a time */
}

static uint64_t hash_attr(uint64_t h, const struct tsm_screen_attr *a)
{
	h = hash_u64(h, (uint8_t)a->fccode);
	h = hash_u64(h, (uint8_t)a->bccode);
	h = hash_u64(h, a->fr);
	h = hash_u64(h, a->fg);
	h = hash_u64(h, a->fb);
	h = hash_u64(h, a->br);
	h = hash_u64(h, a->bg);
	h = hash_u64(h, a->bb);
	h = hash_u64(h, a->bold | a->dim << 1 | a->italic << 2 |
			a->underline << 3 | a->inverse << 4 | a->protect << 5 |
			a->blink << 6);
	return h;
}

static int draw_cb(struct tsm_screen *con, uint64_t id, const uint32_t *ch,
		   size_t len, unsigned int width, unsigned int posx,
		   unsigned int posy, const struct tsm_screen_attr *attr,
		   tsm_age_t age, void *data)
{
	uint64_t *h = data;
	size_t i;

	(void)con;
	(void)id;
	(void)age;
	*h = hash_u64(*h, len);
	for (i = 0; i < len; ++i)
		*h = hash_u64(*h, ch[i]);
	*h = hash_u64(*h, width);
	*h = hash_u64(*h, posx);
	*h = hash_u64(*h, posy);
	*h = hash_attr(*h, attr);
	return 0;
}

static uint64_t hash_screen(struct tsm_screen *screen, unsigned int cols)
{
	uint64_t h = 14695981039346656037ULL;
	unsigned int count = tsm_screen_sb_get_line_count(screen), i, c;
	struct tsm_screen_cell *cells = calloc((size_t)cols, sizeof(*cells));
	unsigned int len;

	/* Scrollback lines, then the visible screen. */
	for (i = 0; cells && i < count; ++i) {
		if (tsm_screen_sb_get_line_cells(screen, i, cells, &len, cols))
			continue;
		h = hash_u64(h, len);
		for (c = 0; c < len; ++c) {
			h = hash_u64(h, cells[c].ch);
			h = hash_u64(h, cells[c].width);
			h = hash_attr(h, &cells[c].attr);
		}
	}
	free(cells);
	h = hash_u64(h, count);
	tsm_screen_draw(screen, draw_cb, &h);
	h = hash_u64(h, tsm_screen_get_cursor_x(screen));
	h = hash_u64(h, tsm_screen_get_cursor_y(screen));
	h = hash_u64(h, tsm_screen_get_flags(screen));
	return h;
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
	int repeats = 10, opt, i, dump = 0, ret = 1;
	struct tsm_screen *screen;
	struct tsm_vte *vte;
	double start, ms, best = 0;
	char *data;

	while ((opt = getopt(argc, argv, "n:c:r:C:s:d")) != -1) {
		switch (opt) {
		case 'n': repeats = atoi(optarg); break;
		case 'c': chunk = (size_t)atol(optarg); break;
		case 'r': rows = (unsigned int)atoi(optarg); break;
		case 'C': cols = (unsigned int)atoi(optarg); break;
		case 's': sb = (unsigned int)atoi(optarg); break;
		case 'd': dump = 1; break;
		default:
			fprintf(stderr,
				"usage: %s [-n repeats] [-c chunk] [-r rows] "
				"[-C cols] [-s scrollback] [-d] file\n", argv[0]);
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

	if (tsm_screen_new(&screen, NULL, NULL))
		goto out_data;
	if (tsm_screen_resize(screen, cols, rows))
		goto out_screen;
	tsm_screen_set_max_sb(screen, sb);
	if (tsm_vte_new(&vte, screen, write_cb, NULL, NULL, NULL))
		goto out_screen;

	if (dump) {
		/* 'X' shows the current attribute, 'q' the GL charset (a line
		 * in DEC special graphics). */
		static const char probe[] = "Xq\r\n";

		for (off = 0; off < len; off += n) {
			n = len - off < chunk ? len - off : chunk;
			tsm_vte_input(vte, data + off, n);
		}
		tsm_vte_input(vte, probe, sizeof(probe) - 1);
		printf("%016llx\n",
		       (unsigned long long)hash_screen(screen, cols));
		ret = 0;
		goto out_vte;
	}

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
	ret = 0;

out_vte:
	tsm_vte_unref(vte);
out_screen:
	tsm_screen_unref(screen);
out_data:
	free(data);
	return ret;
}
