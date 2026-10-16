/*
 * TSM - VTE State Machine Tests
 *
 * Copyright (c) 2022 Andreas Heck <aheck@gmx.de>
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

char write_buffer[512];

bool mouse_cb_called = false;
unsigned int mouse_track_mode = 0;
bool mouse_track_pixels = false;

static void write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data)
{
	ck_assert_ptr_ne(vte, NULL);
	ck_assert_ptr_ne(u8, NULL);

	memcpy(&write_buffer, u8, len);
	write_buffer[len] = '\0';
}

static void mouse_cb(struct tsm_vte *vte, enum tsm_mouse_track_mode track_mode, bool track_pixels, void *data)
{
	mouse_cb_called = true;
	mouse_track_mode = track_mode;
	mouse_track_pixels = track_pixels;
}

struct tsm_screen *screen;
struct tsm_vte *vte;

void setup()
{
	int r;

	r = tsm_screen_new(&screen, NULL, NULL);
	ck_assert_int_eq(r, 0);

	r = tsm_vte_new(&vte, screen, write_cb, NULL, NULL, NULL);
	ck_assert_int_eq(r, 0);

	tsm_vte_set_mouse_cb(vte, mouse_cb, NULL);

	mouse_cb_called = false;
	mouse_track_mode = 0;
	mouse_track_pixels = false;

	memset(&write_buffer, 0, sizeof(write_buffer));
}

void teardown()
{
	tsm_vte_unref(vte);
	vte = NULL;

	tsm_screen_unref(screen);
	screen = NULL;
}

START_TEST(test_mouse_cb_x10)
{
	char *msg;

	/* Set X10 Mode */
	msg = "\e[?9h";
	tsm_vte_input(vte, msg, strlen(msg));

	ck_assert(mouse_cb_called);
	ck_assert_int_eq(mouse_track_mode, TSM_MOUSE_TRACK_BTN);
	ck_assert(!mouse_track_pixels);
}
END_TEST

START_TEST(test_mouse_x10)
{
	char *msg;
	char *expected;
	int r;

	/* Set X10 Mode */
	msg = "\e[?9h";
	tsm_vte_input(vte, msg, strlen(msg));

	/* left click on left upper cell (0, 0) should be translated to (1, 1) in output */
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[M !!";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* right click on (0, 0) */
	memset(&write_buffer,0,  sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 2, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[M\"!!";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* middle click on (0, 0) */
	memset(&write_buffer,0,  sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 1, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[M!!!";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* left click out of range (299, 279) */
	memset(&write_buffer,0,  sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 299, 279, 0, 0, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[M \xff\xff";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);
}
END_TEST

START_TEST(test_mouse_cb_sgr)
{
	char *msg;

	/* Set SGR Mode */
	msg = "\e[?1006h";
	tsm_vte_input(vte, msg, strlen(msg));

	/* mouse_cb should not be called if event type is not set */
	ck_assert(!mouse_cb_called);

	/* reset for next test */
	mouse_cb_called = false;
	mouse_track_mode = 0;
	mouse_track_pixels = false;

	/* Set Button Events */
	memset(&write_buffer, 0, sizeof(write_buffer));
	msg = "\e[?1002h";
	tsm_vte_input(vte, msg, strlen(msg));

	ck_assert(mouse_cb_called);
	ck_assert_int_eq(mouse_track_mode, TSM_MOUSE_TRACK_BTN);
	ck_assert(!mouse_track_pixels);
}
END_TEST

START_TEST(test_mouse_sgr)
{
	char *expected;
	char *msg;
	int r;

	/* Set SGR Mode and only notify for mouse button clicks */
	msg = "\e[?1006h\e[?1002h";
	tsm_vte_input(vte, msg, strlen(msg));

	/* left click on left upper cell (0, 0) should be translated to (1, 1) in output */
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[<0;1;1M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* button release event for (1, 1) */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 0, TSM_MOUSE_EVENT_RELEASED, 0);
	expected = "\e[<0;1;1m";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* button 1 (middle mouse button) */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 1, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[<1;1;1M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* button 2 (right mouse button) */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 2, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[<2;1;1M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* button 4 (mouse wheel up scroll) */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 4, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[<64;1;1M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* button 5 (mouse wheel down scroll) */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 5, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[<65;1;1M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* check for (50, 120) */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 49, 119, 0, 0, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[<0;50;120M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);
}
END_TEST

START_TEST(test_mouse_sgr_cell_change)
{
	char *msg;
	char *expected;
	int r;

	/* Set SGR Mode and only notify for mouse button clicks */
	msg = "\e[?1006h\e[?1003h";
	tsm_vte_input(vte, msg, strlen(msg));

	/* move over (0, 0) */
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 0, TSM_MOUSE_EVENT_MOVED, 0);
	expected = "\e[<35;1;1M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* repeated reportings of the same cell should be ignored */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 0, TSM_MOUSE_EVENT_MOVED, 0);
	ck_assert_int_eq(write_buffer[0], 0);

	/* different cells must be reported */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 1, 1, 0, 0, 0, TSM_MOUSE_EVENT_MOVED, 0);
	expected = "\e[<35;2;2M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	/* a click must be reported in all cases */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 1, 1, 0, 0, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[<0;2;2M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);
}
END_TEST

START_TEST(test_mouse_cb_pixels)
{
	char *msg;

	/* Set Pixel Mode */
	msg = "\e[?1016h";
	tsm_vte_input(vte, msg, strlen(msg));

	/* mouse_cb should not be called if event type is not set */
	ck_assert(!mouse_cb_called);

	/* reset for next test */
	mouse_cb_called = false;
	mouse_track_mode = 0;
	mouse_track_pixels = false;

	/* Set Button Events */
	memset(&write_buffer, 0, sizeof(write_buffer));
	msg = "\e[?1003h";
	tsm_vte_input(vte, msg, strlen(msg));

	ck_assert(mouse_cb_called);
	ck_assert_int_eq(mouse_track_mode, TSM_MOUSE_TRACK_ANY);
	ck_assert(mouse_track_pixels);
}
END_TEST

START_TEST(test_mouse_pixels)
{
	char *msg;
	char *expected;
	int r;

	/* Set SGR Mode and only notify for mouse button clicks */
	msg = "\e[?1016h\e[?1003h";
	tsm_vte_input(vte, msg, strlen(msg));

	tsm_vte_handle_mouse(vte, 0, 0, 236, 120, 0, TSM_MOUSE_EVENT_MOVED, 0);
	expected = "\e[<35;236;120M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 236, 120, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	expected = "\e[<0;236;120M";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);

	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_handle_mouse(vte, 0, 0, 236, 120, 0, TSM_MOUSE_EVENT_RELEASED, 0);
	expected = "\e[<0;236;120m";
	r = memcmp(&write_buffer, expected, strlen(expected));
	ck_assert_int_eq(r, 0);
}
END_TEST

TEST_DEFINE_CASE(tests_x10)
	CHECKED_FIXTURE(setup, teardown)
	TEST(test_mouse_cb_x10)
	TEST(test_mouse_x10)
TEST_END_CASE

TEST_DEFINE_CASE(tests_sgr)
	CHECKED_FIXTURE(setup, teardown)
	TEST(test_mouse_cb_sgr)
	TEST(test_mouse_sgr)
	TEST(test_mouse_sgr_cell_change)
TEST_END_CASE

TEST_DEFINE_CASE(tests_sgr_pixels)
	CHECKED_FIXTURE(setup, teardown)
	TEST(test_mouse_cb_pixels)
	TEST(test_mouse_pixels)
TEST_END_CASE

START_TEST(test_mouse_mode_encoding_order)
{
	/* The encoding and the event mode are independent: either order works. */
	char *msg = "\e[?1006h\e[?1000h\e[?1002h";
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert_int_eq(tsm_vte_get_mouse_mode(vte), TSM_VTE_MOUSE_EVENT_BTN);

	tsm_vte_handle_mouse(vte, 10, 5, 0, 0, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	ck_assert_str_eq(write_buffer, "\e[<0;11;6M");

	/* a drag is passed in mode 1002 */
	memset(&write_buffer, 0, sizeof(write_buffer));
	ck_assert(tsm_vte_handle_mouse(vte, 11, 5, 0, 0, 32, TSM_MOUSE_EVENT_MOVED, 0));
	ck_assert_str_eq(write_buffer, "\e[<32;12;6M");
}
END_TEST

START_TEST(test_mouse_mode_encoding_last)
{
	char *msg = "\e[?1000h\e[?1002h\e[?1006h";
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert_int_eq(tsm_vte_get_mouse_mode(vte), TSM_VTE_MOUSE_EVENT_BTN);

	tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	ck_assert_str_eq(write_buffer, "\e[<0;1;1M");
}
END_TEST

START_TEST(test_mouse_reset_inactive_mode)
{
	char *msg = "\e[?1000h\e[?1002h\e[?1000l";
	tsm_vte_input(vte, msg, strlen(msg));
	/* 1000 is not the active mode, so its reset changes nothing */
	ck_assert_int_eq(tsm_vte_get_mouse_mode(vte), TSM_VTE_MOUSE_EVENT_BTN);

	msg = "\e[?1002l";
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert_int_eq(tsm_vte_get_mouse_mode(vte), 0);
}
END_TEST

START_TEST(test_mouse_encoding_only)
{
	char *msg = "\e[?1006h";
	tsm_vte_input(vte, msg, strlen(msg));

	/* without an event mode the application wants nothing */
	ck_assert_int_eq(tsm_vte_get_mouse_mode(vte), 0);
	ck_assert(!tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 0, TSM_MOUSE_EVENT_PRESSED, 0));
	ck_assert_int_eq(write_buffer[0], 0);
}
END_TEST

START_TEST(test_mouse_pixels_without_sgr)
{
	/* 1016 works on its own, without 1006 */
	char *msg = "\e[?1016h\e[?1002h";
	tsm_vte_input(vte, msg, strlen(msg));

	tsm_vte_handle_mouse(vte, 1, 1, 236, 120, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	ck_assert_str_eq(write_buffer, "\e[<0;236;120M");
}
END_TEST

START_TEST(test_mouse_mode_filters)
{
	char *msg;

	/* mode 9 ignores the wheel */
	msg = "\e[?9h";
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert(!tsm_vte_handle_mouse(vte, 0, 0, 0, 0, TSM_MOUSE_BUTTON_WHEEL_UP,
				       TSM_MOUSE_EVENT_PRESSED, 0));
	ck_assert_int_eq(write_buffer[0], 0);

	/* mode 1000 drops every move, also a drag */
	msg = "\e[?1000h";
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert(!tsm_vte_handle_mouse(vte, 1, 1, 0, 0, 32, TSM_MOUSE_EVENT_MOVED, 0));
	ck_assert(!tsm_vte_handle_mouse(vte, 2, 2, 0, 0, 3, TSM_MOUSE_EVENT_MOVED, 0));
	ck_assert_int_eq(write_buffer[0], 0);

	/* mode 1002 passes drags (button 32 to 34) but not a hover */
	msg = "\e[?1002h";
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert(tsm_vte_handle_mouse(vte, 3, 3, 0, 0, 33, TSM_MOUSE_EVENT_MOVED, 0));
	ck_assert(tsm_vte_handle_mouse(vte, 4, 4, 0, 0, 34, TSM_MOUSE_EVENT_MOVED, 0));
	ck_assert(!tsm_vte_handle_mouse(vte, 5, 5, 0, 0, 3, TSM_MOUSE_EVENT_MOVED, 0));
}
END_TEST

START_TEST(test_mouse_legacy_drag)
{
	/* legacy encoding (no 1006) in mode 1002 */
	char *msg = "\e[?1002h";
	tsm_vte_input(vte, msg, strlen(msg));

	ck_assert(tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 32, TSM_MOUSE_EVENT_MOVED, 0));
	/* button 32 + 0x20 is "@", column and row 0 + 0x21 are "!" */
	ck_assert_str_eq(write_buffer, "\e[M@!!");
}
END_TEST

START_TEST(test_mouse_cb_order)
{
	char *msg;

	/* 1016 first, then the event mode: the callback gets pixels */
	msg = "\e[?1016h\e[?1002h";
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert(mouse_cb_called);
	ck_assert_int_eq(mouse_track_mode, TSM_MOUSE_TRACK_BTN);
	ck_assert(mouse_track_pixels);

	/* resetting the encoding is reported while the mode is on */
	mouse_cb_called = false;
	msg = "\e[?1016l";
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert(mouse_cb_called);
	ck_assert_int_eq(mouse_track_mode, TSM_MOUSE_TRACK_BTN);
	ck_assert(!mouse_track_pixels);

	/* resetting the mode reports "no tracking" */
	mouse_cb_called = false;
	msg = "\e[?1002l";
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert(mouse_cb_called);
	ck_assert_int_eq(mouse_track_mode, TSM_MOUSE_TRACK_DISABLE);
}
END_TEST

START_TEST(test_mouse_last_encoding_wins)
{
	char *msg = "\e[?1016h\e[?1006h\e[?1002h";
	tsm_vte_input(vte, msg, strlen(msg));
	tsm_vte_handle_mouse(vte, 1, 1, 236, 120, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	ck_assert_str_eq(write_buffer, "\e[<0;2;2M");

	memset(&write_buffer, 0, sizeof(write_buffer));
	msg = "\e[?1006h\e[?1016h";
	tsm_vte_input(vte, msg, strlen(msg));
	tsm_vte_handle_mouse(vte, 1, 1, 236, 120, 0, TSM_MOUSE_EVENT_PRESSED, 0);
	ck_assert_str_eq(write_buffer, "\e[<0;236;120M");
}
END_TEST

START_TEST(test_mouse_legacy_cell_change)
{
	/* the repeated cell filter also applies to the legacy encoding */
	char *msg = "\e[?1003h";
	tsm_vte_input(vte, msg, strlen(msg));

	/* the first move, onto cell (0, 0), must not be dropped. A hover is
	 * 3 + 32 (motion) + 0x20 = "C", not "#" which would be a release. */
	ck_assert(tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 3, TSM_MOUSE_EVENT_MOVED, 0));
	ck_assert_str_eq(write_buffer, "\e[MC!!");

	memset(&write_buffer, 0, sizeof(write_buffer));
	ck_assert(!tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 3, TSM_MOUSE_EVENT_MOVED, 0));
	ck_assert_int_eq(write_buffer[0], 0);

	ck_assert(tsm_vte_handle_mouse(vte, 1, 0, 0, 0, 3, TSM_MOUSE_EVENT_MOVED, 0));
	ck_assert_str_eq(write_buffer, "\e[MC\"!");

	/* setting a mode forgets the last cell */
	memset(&write_buffer, 0, sizeof(write_buffer));
	tsm_vte_input(vte, msg, strlen(msg));
	ck_assert(tsm_vte_handle_mouse(vte, 1, 0, 0, 0, 3, TSM_MOUSE_EVENT_MOVED, 0));
	ck_assert_str_eq(write_buffer, "\e[MC\"!");

	/* a release is still "#" */
	memset(&write_buffer, 0, sizeof(write_buffer));
	ck_assert(tsm_vte_handle_mouse(vte, 1, 0, 0, 0, 0, TSM_MOUSE_EVENT_RELEASED, 0));
	ck_assert_str_eq(write_buffer, "\e[M#\"!");
}
END_TEST

START_TEST(test_mouse_hover_modifiers)
{
	/* a hover keeps its modifiers, like a drag does */
	char *msg = "\e[?1003h\e[?1006h";
	tsm_vte_input(vte, msg, strlen(msg));

	ck_assert(tsm_vte_handle_mouse(vte, 5, 5, 0, 0, 3, TSM_MOUSE_EVENT_MOVED,
				       TSM_MOUSE_MODIFIER_CTRL));
	ck_assert_str_eq(write_buffer, "\e[<51;6;6M");

	memset(&write_buffer, 0, sizeof(write_buffer));
	ck_assert(tsm_vte_handle_mouse(vte, 6, 5, 0, 0, 32, TSM_MOUSE_EVENT_MOVED,
				       TSM_MOUSE_MODIFIER_SHIFT));
	ck_assert_str_eq(write_buffer, "\e[<36;7;6M");
}
END_TEST

START_TEST(test_mouse_x10_sgr)
{
	/* mode 9 is press only and has no modifiers, also with SGR */
	char *msg = "\e[?9h\e[?1006h";
	tsm_vte_input(vte, msg, strlen(msg));

	ck_assert(!tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 0, TSM_MOUSE_EVENT_RELEASED, 0));
	ck_assert_int_eq(write_buffer[0], 0);

	ck_assert(tsm_vte_handle_mouse(vte, 0, 0, 0, 0, 0, TSM_MOUSE_EVENT_PRESSED,
				       TSM_MOUSE_MODIFIER_CTRL));
	ck_assert_str_eq(write_buffer, "\e[<0;1;1M");
}
END_TEST

TEST_DEFINE_CASE(tests_mouse_modes)
	CHECKED_FIXTURE(setup, teardown)
	TEST(test_mouse_mode_encoding_order)
	TEST(test_mouse_mode_encoding_last)
	TEST(test_mouse_reset_inactive_mode)
	TEST(test_mouse_encoding_only)
	TEST(test_mouse_pixels_without_sgr)
	TEST(test_mouse_mode_filters)
	TEST(test_mouse_legacy_drag)
	TEST(test_mouse_cb_order)
	TEST(test_mouse_last_encoding_wins)
	TEST(test_mouse_legacy_cell_change)
	TEST(test_mouse_x10_sgr)
	TEST(test_mouse_hover_modifiers)
TEST_END_CASE

// clang-format off
TEST_DEFINE(
	TEST_SUITE(vte_mouse,
		TEST_CASE(tests_x10),
		TEST_CASE(tests_sgr),
		TEST_CASE(tests_sgr_pixels),
		TEST_CASE(tests_mouse_modes),
		TEST_END
	)
)
// clang-format on
