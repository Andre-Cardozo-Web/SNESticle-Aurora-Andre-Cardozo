/*
 * Aurora PS2 compatibility helpers for the SameBoy Core.
 * The pinned submodule itself remains pristine.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int aurora_sameboy_vasprintf(char **out, const char *fmt, va_list ap)
{
    va_list copy;
    int n;
    char *buf;

    if (!out || !fmt) return -1;
    *out = NULL;

    va_copy(copy, ap);
    n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) return -1;

    buf = (char *)malloc((size_t)n + 1U);
    if (!buf) return -1;

    va_copy(copy, ap);
    if (vsnprintf(buf, (size_t)n + 1U, fmt, copy) != n) {
        va_end(copy);
        free(buf);
        return -1;
    }
    va_end(copy);

    *out = buf;
    return n;
}

int aurora_sameboy_asprintf(char **out, const char *fmt, ...)
{
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = aurora_sameboy_vasprintf(out, fmt, ap);
    va_end(ap);
    return n;
}
