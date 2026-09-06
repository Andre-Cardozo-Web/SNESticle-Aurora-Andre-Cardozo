#ifndef _AURORA_SAMEBOY_PS2_COMPAT_H
#define _AURORA_SAMEBOY_PS2_COMPAT_H

#include <stdarg.h>

/* AURORA_SAMEBOY_VERSION_DEFS_V2_BEGIN */
#ifndef GB_VERSION
#define GB_VERSION "1.0.3"
#endif
#ifndef GB_COPYRIGHT_YEAR
#define GB_COPYRIGHT_YEAR "2026"
#endif
/* AURORA_SAMEBOY_VERSION_DEFS_V2_END */

#ifdef __cplusplus
extern "C" {
#endif

int aurora_sameboy_vasprintf(char **out, const char *fmt, va_list ap);
int aurora_sameboy_asprintf(char **out, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
