/*
 * TSM - Screen Management Tests
 *
 * Copyright (c) 2018 Aetf <aetf@unlimitedcodeworks.xyz>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "test_common.h"
#include "libtsm.h"
#include "libtsm-int.h"

START_TEST(test_screen_init)
{
    struct tsm_screen *screen;
	int r;

	r = tsm_screen_new(&screen, NULL, NULL);
	ck_assert_int_eq(r, 0);

	tsm_screen_unref(screen);
	screen = NULL;
}
END_TEST

START_TEST(test_screen_null)
{
    int r;
    unsigned int n;

    r = tsm_screen_new(NULL, NULL, NULL);
    ck_assert_int_eq(r, -EINVAL);

    tsm_screen_ref(NULL);
    tsm_screen_unref(NULL);

    tsm_screen_set_opts(NULL, 0u);
	tsm_screen_reset_opts(NULL, 0u);

	n = tsm_screen_get_opts(NULL);
	ck_assert_int_eq(n, 0);

	n = tsm_screen_get_width(NULL);
	ck_assert_int_eq(n, 0);

	n = tsm_screen_get_height(NULL);
	ck_assert_int_eq(n, 0);

	r = tsm_screen_resize(NULL, 0, 0);
	ck_assert_int_eq(r, -EINVAL);

	r = tsm_screen_set_margins(NULL, 0u, 0u);
	ck_assert_int_eq(r, -EINVAL);

	tsm_screen_set_max_sb(NULL, 0u);

	tsm_screen_clear_sb(NULL);

	tsm_screen_sb_up(NULL, 0u);
	tsm_screen_sb_down(NULL, 0u);
	tsm_screen_sb_page_up(NULL, 0u);
	tsm_screen_sb_page_down(NULL, 0u);
	tsm_screen_sb_reset(NULL);

	tsm_screen_set_def_attr(NULL, NULL);

	tsm_screen_reset(NULL);

	tsm_screen_set_flags(NULL, 0u);
	tsm_screen_reset_flags(NULL, 0u);

	n = tsm_screen_get_flags(NULL);
	ck_assert_int_eq(n, 0);

	n = tsm_screen_get_cursor_x(NULL);
	ck_assert_int_eq(n, 0);

	n = tsm_screen_get_cursor_y(NULL);
	ck_assert_int_eq(n, 0);

	tsm_screen_set_tabstop(NULL);
	tsm_screen_reset_tabstop(NULL);
	tsm_screen_reset_all_tabstops(NULL);

	tsm_screen_write(NULL, 0u, NULL);

	tsm_screen_newline(NULL);

	tsm_screen_scroll_up(NULL, 0u);
	tsm_screen_scroll_down(NULL, 0u);

	tsm_screen_move_to(NULL, 0u, 0u);
	tsm_screen_move_up(NULL, 0u, false);
	tsm_screen_move_down(NULL, 0u, false);
	tsm_screen_move_right(NULL, 0u);
	tsm_screen_move_left(NULL, 0u);
	tsm_screen_move_line_end(NULL);
	tsm_screen_move_line_home(NULL);

	tsm_screen_tab_right(NULL, 0u);
	tsm_screen_tab_left(NULL, 0u);

	tsm_screen_insert_lines(NULL, 0u);
	tsm_screen_delete_lines(NULL, 0u);
	tsm_screen_insert_chars(NULL, 0u);
	tsm_screen_delete_chars(NULL, 0u);

	tsm_screen_erase_cursor(NULL);
	tsm_screen_erase_chars(NULL, 0u);
	tsm_screen_erase_cursor_to_end(NULL, false);
	tsm_screen_erase_home_to_cursor(NULL, false);
	tsm_screen_erase_current_line(NULL, false);
	tsm_screen_erase_screen_to_cursor(NULL, false);
	tsm_screen_erase_cursor_to_screen(NULL, false);
	tsm_screen_erase_screen(NULL, false);
}
END_TEST

START_TEST(test_screen_resize_alt_colors)
{
	struct tsm_screen *screen;
	struct line *line;
	int r, y, x;
	struct tsm_screen_attr *attr;
	struct tsm_screen_attr new_attr;

	r = tsm_screen_new(&screen, NULL, NULL);
	ck_assert_int_eq(r, 0);

	/* start with an initial 2x2 screen */
	r = tsm_screen_resize(screen, 2, 2);
	ck_assert_int_eq(r, 0);

	/* switch to alternate screen */
	tsm_screen_set_flags(screen, TSM_SCREEN_ALTERNATE);

	/* change background color to red */
	memset(&new_attr, 0, sizeof(new_attr));
	new_attr.br = 255;
	new_attr.bg = 0;
	new_attr.bb = 0;

	tsm_screen_set_def_attr(screen, &new_attr);
	tsm_screen_erase_screen(screen, false);

	/* now all cells should be red */
	for (y = 0; y < screen->size_y; y++) {
		line = screen->lines[y];
		for (x = 0; x < screen->size_x; x++) {
			attr = &line->cells[x].attr;
			ck_assert_int_eq(attr->br, 255);
			ck_assert_int_eq(attr->bg, 0);
			ck_assert_int_eq(attr->bb, 0);
		}
	}

	/* enlarge to 4x4 while on alternate screen */
	r = tsm_screen_resize(screen, 4, 4);
	ck_assert_int_eq(r, 0);

	/* leave alternate screen */
	tsm_screen_reset_flags(screen, TSM_SCREEN_ALTERNATE);

	/* now all cells should be black (including the new ones) */
	for (y = 0; y < screen->size_y; y++) {
		line = screen->lines[y];
		for (x = 0; x < screen->size_x; x++) {
			attr = &line->cells[x].attr;
			ck_assert_int_eq(attr->br, 0);
			ck_assert_int_eq(attr->bg, 0);
			ck_assert_int_eq(attr->bb, 0);
		}
	}

	tsm_screen_unref(screen);
	screen = NULL;
}
END_TEST

START_TEST(test_screen_sb_get_line_pos)
{
	struct tsm_screen *screen;
	int r;

	r = tsm_screen_new(&screen, NULL, NULL);
	ck_assert_int_eq(r, 0);

	r = tsm_screen_resize(screen, 5, 5);
	ck_assert_int_eq(r, 0);

	tsm_screen_set_max_sb(screen, 5);

	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 0);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 0);

	/* fill up screen */
	tsm_screen_newline(screen);
	tsm_screen_newline(screen);
	tsm_screen_newline(screen);
	tsm_screen_newline(screen);

	/* create first entry in scrollback buffer */
	tsm_screen_newline(screen);

	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 1);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 1);

	tsm_screen_newline(screen);

	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 2);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 2);

	tsm_screen_newline(screen);

	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 3);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 3);

	/* scroll up by one (third line of sb) */
	tsm_screen_sb_up(screen, 1);
	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 3);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 2);

	/* scroll up by one (second line of sb) */
	tsm_screen_sb_up(screen, 1);
	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 3);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 1);

	/* scroll up by one (first line of sb) */
	tsm_screen_sb_up(screen, 1);
	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 3);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 0);

	/* scroll up beyond top end of buffer */
	tsm_screen_sb_up(screen, 1);
	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 3);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 0);

	/* scroll out of scrollback buffer */
	tsm_screen_sb_down(screen, 3);
	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 3);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 3);

	tsm_screen_newline(screen);
	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 4);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 4);

	tsm_screen_newline(screen);
	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 5);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 5);

	tsm_screen_sb_up(screen, 2);
	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 5);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 3);

	tsm_screen_newline(screen);
	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 5);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 2);

	r = tsm_screen_resize(screen, 5, 3);
	ck_assert_int_eq(r, 0);

	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 5);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 0);

	r = tsm_screen_resize(screen, 5, 5);
	ck_assert_int_eq(r, 0);

	ck_assert_int_eq(tsm_screen_sb_get_line_count(screen), 3);
	ck_assert_int_eq(tsm_screen_sb_get_line_pos(screen), 0);

	tsm_screen_unref(screen);
	screen = NULL;
}
END_TEST


/* The second cell of a wide character is a filler: no symbol, width 0 and the
 * attributes of its owner. Overwriting one half blanks the other half. */
START_TEST(test_screen_wide_filler)
{
	struct tsm_screen *screen;
	struct tsm_screen_attr attr;
	struct line *line;
	int r;

	r = tsm_screen_new(&screen, NULL, NULL);
	ck_assert_int_eq(r, 0);
	r = tsm_screen_resize(screen, 6, 2);
	ck_assert_int_eq(r, 0);

	memset(&attr, 0, sizeof(attr));
	attr.br = 255;
	line = screen->lines[0];

	/* U+4F60 is two columns wide */
	tsm_screen_write(screen, 0x4F60, &attr);
	ck_assert_int_eq(line->cells[0].width, 2);
	ck_assert_int_eq(line->cells[1].width, 0);
	ck_assert_int_eq(line->cells[1].ch, 0);
	ck_assert_int_eq(line->cells[1].attr.br, 255);

	/* a narrow write on the filler blanks the owner */
	tsm_screen_move_to(screen, 1, 0);
	tsm_screen_write(screen, 'B', &attr);
	ck_assert_int_eq(line->cells[0].width, 1);
	ck_assert_int_eq(line->cells[0].ch, 0);
	ck_assert_int_eq(line->cells[1].width, 1);
	ck_assert_int_eq(line->cells[1].ch, 'B');

	/* a narrow write on the owner blanks the filler */
	tsm_screen_move_to(screen, 3, 0);
	tsm_screen_write(screen, 0x4F60, &attr);
	tsm_screen_move_to(screen, 3, 0);
	tsm_screen_write(screen, 'C', &attr);
	ck_assert_int_eq(line->cells[3].width, 1);
	ck_assert_int_eq(line->cells[3].ch, 'C');
	ck_assert_int_eq(line->cells[4].width, 1);
	ck_assert_int_eq(line->cells[4].ch, 0);

	/* the blanked half gets the attributes of the write, not the old ones */
	attr.br = 10;
	tsm_screen_move_to(screen, 0, 1);
	tsm_screen_write(screen, 0x4F60, &attr);
	attr.br = 20;
	tsm_screen_move_to(screen, 1, 1);
	tsm_screen_write(screen, 'D', &attr);
	line = screen->lines[1];
	ck_assert_int_eq(line->cells[0].width, 1);
	ck_assert_int_eq(line->cells[0].attr.br, 20);

	/* insert mode: the shift must not copy a filler */
	tsm_screen_erase_screen(screen, false);
	tsm_screen_set_flags(screen, TSM_SCREEN_INSERT_MODE);
	tsm_screen_move_to(screen, 0, 0);
	tsm_screen_write(screen, 0x4F60, &attr);
	tsm_screen_move_to(screen, 1, 0);
	tsm_screen_write(screen, 'E', &attr);
	line = screen->lines[0];
	{
		unsigned int x;

		for (x = 0; x < 6; ++x)
			ck_assert_int_ne(line->cells[x].width, 0);
	}
	ck_assert_int_eq(line->cells[1].ch, 'E');

	/* insert mode on the owner column: the wide character is shifted whole */
	tsm_screen_erase_screen(screen, false);
	tsm_screen_move_to(screen, 0, 0);
	tsm_screen_write(screen, 0x4F60, &attr);
	tsm_screen_move_to(screen, 0, 0);
	tsm_screen_write(screen, 'E', &attr);
	line = screen->lines[0];
	ck_assert_int_eq(line->cells[0].ch, 'E');
	ck_assert_int_eq(line->cells[1].ch, 0x4F60);
	ck_assert_int_eq(line->cells[1].width, 2);
	ck_assert_int_eq(line->cells[2].width, 0);

	/* a wide character that still fits after the shift is kept */
	tsm_screen_erase_screen(screen, false);
	tsm_screen_move_to(screen, 3, 0);
	tsm_screen_write(screen, 0x4F60, &attr);
	tsm_screen_move_to(screen, 0, 0);
	tsm_screen_write(screen, 'F', &attr);
	line = screen->lines[0];
	ck_assert_int_eq(line->cells[4].ch, 0x4F60);
	ck_assert_int_eq(line->cells[4].width, 2);
	ck_assert_int_eq(line->cells[5].width, 0);

	/* a wide character that is pushed over the edge is blanked, no orphan */
	tsm_screen_erase_screen(screen, false);
	tsm_screen_move_to(screen, 4, 0);
	tsm_screen_write(screen, 0x4F60, &attr);
	tsm_screen_move_to(screen, 0, 0);
	tsm_screen_write(screen, 'G', &attr);
	line = screen->lines[0];
	ck_assert_int_ne(line->cells[4].width, 0);
	ck_assert_int_ne(line->cells[5].width, 0);
	ck_assert_int_eq(line->cells[5].ch, 0);

	tsm_screen_unref(screen);
	screen = NULL;
}
END_TEST

TEST_DEFINE_CASE(misc)
	TEST(test_screen_init)
	TEST(test_screen_null)
	TEST(test_screen_resize_alt_colors)
	TEST(test_screen_sb_get_line_pos)
	TEST(test_screen_wide_filler)
TEST_END_CASE

TEST_DEFINE(
	TEST_SUITE(screen,
		TEST_CASE(misc),
		TEST_END
	)
)
