/* io.h — stub for Windows <io.h> on Linux */
#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Windows compat: _open, _close, _read, _write, _lseek, _access */
#ifndef _open
#define _open  open
#define _close close
#define _read  read
#define _write write
#define _lseek lseek
#endif
