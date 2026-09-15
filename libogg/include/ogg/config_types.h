#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__

/*
Hand-written replacement for the file autotools/CMake normally generate from
config_types.h.in. Both MSVC and GCC/Clang have provided <stdint.h> for well
over a decade, so there is no need to probe for platform-specific integer
types at configure time -- these typedefs are identical on every platform
this engine targets.
*/

#include <stdint.h>

typedef int16_t  ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t  ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t  ogg_int64_t;
typedef uint64_t ogg_uint64_t;

#endif
