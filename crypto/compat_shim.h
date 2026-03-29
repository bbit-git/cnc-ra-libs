/*
 * compat_shim.h — Minimal shim to compile RA engine crypto files standalone.
 *
 * Force-included before all source files via -include.
 * Blocks the engine's function.h mega-header and provides stubs.
 */
#pragma once

/* Block the engine's function.h from being included */
#define FUNCTION_H

/* Prevent wwfile.h from including <io.h> (Windows-only) */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Provide the io.h functions that the code actually uses */
#ifndef _MAX_PATH
#define _MAX_PATH 260
#endif
#ifndef _MAX_FNAME
#define _MAX_FNAME 256
#endif
#ifndef _MAX_EXT
#define _MAX_EXT 256
#endif

/* Some files use O_BINARY / SH_DENYNO which are Windows-isms */
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef SH_DENYNO
#define SH_DENYNO 0
#endif
#ifndef SH_DENYWR
#define SH_DENYWR 0
#endif

#include <cstring>

/* Redirect ancient headers to modern equivalents */
#include <new>
#define _INC_NEW  /* prevent <new.h> from being included again */

/* strtrim — from engine's miscasm.cpp, needed by ini.cpp */
static inline void strtrim(char* buffer) {
    if (!buffer) return;
    /* Trim trailing whitespace */
    int len = strlen(buffer);
    while (len > 0 && (buffer[len-1] == ' ' || buffer[len-1] == '\t'
                    || buffer[len-1] == '\r' || buffer[len-1] == '\n'))
        buffer[--len] = '\0';
    /* Trim leading whitespace */
    char* src = buffer;
    while (*src == ' ' || *src == '\t') src++;
    if (src != buffer) memmove(buffer, src, strlen(src) + 1);
}

/* Stubs for engine globals */
static inline int Force_CD_Available(int) { return 1; }
static inline void Prog_End(const char*, int) {}
static inline void Emergency_Exit(int) { _exit(1); }
static const int RequiredCD = 0;
static const bool RunningAsDLL = false;
