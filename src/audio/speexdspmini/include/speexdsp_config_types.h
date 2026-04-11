/* speexdsp_config_types.h - standard integer types for Linux/POSIX */
#ifndef SPEEXDSP_CONFIG_TYPES_H
#define SPEEXDSP_CONFIG_TYPES_H

#include <stdint.h>

/* Guard each typedef: when compiled inside a host project that already
   defines these names as preprocessor macros (e.g. via speex_resampler.h
   with OUTSIDE_SPEEX), the macro-expansion of a typedef would produce
   "typedef int32_t int;" which is invalid C++.  Skipping the typedef when
   the name is already a macro avoids the conflict without breaking the
   standalone speexdspmini demo build. */
#ifndef spx_int16_t
typedef int16_t  spx_int16_t;
#endif
#ifndef spx_uint16_t
typedef uint16_t spx_uint16_t;
#endif
#ifndef spx_int32_t
typedef int32_t  spx_int32_t;
#endif
#ifndef spx_uint32_t
typedef uint32_t spx_uint32_t;
#endif

#endif /* SPEEXDSP_CONFIG_TYPES_H */
