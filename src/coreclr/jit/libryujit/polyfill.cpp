#include <stdarg.h>

#include "./host.h"
#include "./lib/debug-trap.h"
#include "./lib/nanoprintf.h"

int vflogf(FILE* file, const char* fmt, va_list args);

void __ryujit_pal_asserte_fail(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vflogf(jitstdout(), fmt, args);
    va_end(args);

    psnip_trap();
}

int sprintf_s(char* string, size_t sizeInBytes, const char* format, ...)
{
    int     ret;
    va_list arglist;
    va_start(arglist, format);
    ret = npf_vsnprintf(string, sizeInBytes, format, arglist);
    va_end(arglist);
    return ret;
}
