#ifndef WCWIDTH_H_INCLUDED
#define WCWIDTH_H_INCLUDED

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int wcwidth(wchar_t ucs);

/* Same as wcwidth() but takes a full code point. wchar_t is 16 bits on Windows,
 * where wcwidth() sees only the low 16 bits of a code point above U+FFFF.
 * libtsm calls tsm_wcwidth() only: do not switch it back to wcwidth(). */
int tsm_wcwidth(uint32_t ucs);

#ifdef __cplusplus
}
#endif


#endif
