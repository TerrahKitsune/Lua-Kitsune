#pragma once
#include <stddef.h>

// Skips a UTF-8 byte-order mark (0xEF 0xBB 0xBF) at the start of a string
// buffer.  Only applicable to plain string inputs; streams, functions, and
// other data sources are not touched.
// src and len are updated in-place; both are left unchanged when no BOM is present.
static inline void skip_utf8_bom(const char** src, size_t* len) {
    if (*len >= 3
        && (unsigned char)(*src)[0] == 0xEF
        && (unsigned char)(*src)[1] == 0xBB
        && (unsigned char)(*src)[2] == 0xBF) {
        *src += 3;
        *len -= 3;
    }
}
