#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "wcwidth.h"

static int tests_run;
static int test_failures;

void assertWidthIs(int expected_width, uint32_t c) {
	tests_run++;
        int actual_width = tsm_wcwidth(c);
        if (actual_width != expected_width) {
                fprintf(stderr, "ERROR: wcwidth(U+%04x) returned %d, expected %d\n", c, actual_width, expected_width);
		test_failures++;
        }
}

int main() {
        assertWidthIs(1, 'a');
        assertWidthIs(1, L'ö');

	// Some wide:
	assertWidthIs(2, L'Ａ');
	assertWidthIs(2, L'Ｂ');
	assertWidthIs(2, L'Ｃ');
	assertWidthIs(2, L'中');
	assertWidthIs(2, L'文');
	assertWidthIs(2, 0x679C);
	assertWidthIs(2, 0x679D);
	assertWidthIs(2, 0x2070E);
	assertWidthIs(2, 0x20731);

	assertWidthIs(1, 0x11A3);

	assertWidthIs(2, 0x1F428); // Koala emoji.
        assertWidthIs(2, 0x231a);  // Watch emoji.

	// Above U+FFFF: wchar_t is 16 bits on Windows, tsm_wcwidth takes a full
	// code point.
	assertWidthIs(2, 0x1F600); // Grinning face.
	assertWidthIs(2, 0x1F680); // Rocket.
	assertWidthIs(1, 0x1F1E6); // Regional indicator A.

	if (test_failures > 0) printf("%d tests FAILED, ", test_failures);
	printf("%d tests OK\n", tests_run);
	return (test_failures == 0) ? 0 : 1;
}

