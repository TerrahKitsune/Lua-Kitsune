#pragma once

#ifdef _WIN32
#include <conio.h>

// Reads one key from the console and returns it as a Unicode code point. _getwch returns
// UTF-16 units, so a character outside the BMP (such as an emoji) arrives as a surrogate
// pair and is combined here. An unpaired surrogate becomes U+FFFD, and a key read while
// looking for the second half is put back. Special keys keep _getwch's 0 / 0xE0 prefix.
static inline long long kitsune_getwch_codepoint(void) {

	wint_t hi = _getwch();
	if (hi < 0xD800 || hi > 0xDFFF)
		return (long long)hi;
	if (hi <= 0xDBFF) {
		wint_t lo = _getwch();
		if (lo >= 0xDC00 && lo <= 0xDFFF)
			return 0x10000 + (((long long)hi - 0xD800) << 10) + ((long long)lo - 0xDC00);
		_ungetwch(lo);
	}
	return 0xFFFD;
}
#endif
