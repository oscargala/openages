/* pyconfig.h for CPython 2.3.3 (Cypython-2.3.3, CWE-ou-minkata Build/VS2010/SDKs/XPlatform) on
 * wasm32-emscripten.
 *
 * Hand-written, as Cyan's static build hand-wrote its own (pyconfig_static/pyconfig.h): CPython 2.3 has no
 * autoconf support for Emscripten. The macro names and meanings are CPython 2.3.3's (pyconfig.h.in of that
 * release, copyright Python Software Foundation). The values are Emscripten's (ILP32, little-endian, musl libc),
 * checked at compile time in config.c.
 *
 * Licence: this file is derived from CPython, so it is under CPython's licence, not the GPL of the client's own
 * code: the Python Software Foundation licence, CPython 2.3.3's LICENSE. A verbatim copy is LICENSE-PSF.txt beside
 * this file in the source tree, and lib/python2.3/LICENSE.txt in the installed prefix.
 *
 * Choices:
 *  - Unicode is UCS-2, as in Cyan's build (pyconfig_static/pyconfig.h: Py_UNICODE_SIZE = SIZEOF_SHORT).
 *    Emscripten's wchar_t is 4 bytes, so Py_UNICODE is set explicitly to unsigned short and
 *    HAVE_USABLE_WCHAR_T stays undefined (PyUnicode_FromWideChar/AsWideChar then convert per element).
 *  - No WITH_THREAD, no HAVE_DYNAMIC_LOADING (Python/dynload_stub.c), no signal module (Parser/intrcheck.c and
 *    Python/sigcheck.c provide the interrupt hooks, as the Unix build does without signalmodule).
 *  - WITH_PYMALLOC, WITH_DOC_STRINGS and WITH_UNIVERSAL_NEWLINES as in Cyan's build.
 *  - posixmodule.c is the Unix implementation. Process-control functions (fork, exec, kill, wait, popen,
 *    system, pipes, ptys, user and group ids) are not declared: a browser has none of them.
 *    It is registered as `nt`, the name Cyan's PC/config.c gives it (patch 0003), because MOULa's own
 *    posixpath.py has no curdir/pardir/sep and its os.py fails over `posix`. Build with
 *    -DPY2_OS_MODULE_POSIX (build.sh: PY2_OS_MODULE=posix) for the `posix` name.
 */
#ifndef Py_PYCONFIG_H
#define Py_PYCONFIG_H

#ifndef __EMSCRIPTEN__
#error "this pyconfig.h is for wasm32-emscripten only"
#endif

/* Expose every libc prototype (musl), so that no function is implicitly declared. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

/* --- sizes (wasm32, ILP32) --------------------------------------------------------------------------- */
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_VOID_P 4
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_TIME_T 8
#define SIZEOF_OFF_T 8
#define SIZEOF_FPOS_T 16
#define SIZEOF_WCHAR_T 4
#define HAVE_LONG_LONG 1
#define HAVE_UINTPTR_T 1
/* off_t is wider than long: fileobject.c uses fseeko/ftello */
#define HAVE_LARGEFILE_SUPPORT 1
#define HAVE_FSEEKO 1
#define HAVE_FTELLO 1

/* --- Unicode: UCS-2, as Cyan's build ----------------------------------------------------------------- */
#define Py_USING_UNICODE 1
#define Py_UNICODE_SIZE 2
#define PY_UNICODE_TYPE unsigned short
#define HAVE_WCHAR_H 1
/* HAVE_USABLE_WCHAR_T: deliberately undefined (wchar_t is 4 bytes) */

/* --- interpreter features ---------------------------------------------------------------------------- */
#define WITH_PYMALLOC 1
#define WITH_DOC_STRINGS 1
#define WITH_UNIVERSAL_NEWLINES 1
/* WITH_THREAD, HAVE_DYNAMIC_LOADING: deliberately undefined */
#define PLATFORM "emscripten"
#ifndef PY2_OS_MODULE_POSIX
#define PY2_OS_MODULE_NT 1
#endif

/* --- C language and headers -------------------------------------------------------------------------- */
#define STDC_HEADERS 1
#define HAVE_PROTOTYPES 1
#define HAVE_STDARG_PROTOTYPES 1
#define RETSIGTYPE void
#define HAVE_LIMITS_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_DIRENT_H 1
#define HAVE_UTIME_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TIMES_H 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_SYS_WAIT_H 1
#define TIME_WITH_SYS_TIME 1

/* --- libc functions ---------------------------------------------------------------------------------- */
#define HAVE_CLOCK 1
#define HAVE_LSTAT 1
#define HAVE_FSYNC 1
#define HAVE_FTRUNCATE 1
#define HAVE_GETCWD 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_HYPOT 1
#define HAVE_MKTIME 1
#define HAVE_OPENDIR 1
#define HAVE_PUTENV 1
#define HAVE_UNSETENV 1
#define HAVE_READLINK 1
#define HAVE_SELECT 1
#define HAVE_SETVBUF 1
#define HAVE_SNPRINTF 1
#define HAVE_STRERROR 1
#define HAVE_STRFTIME 1
#define HAVE_SYMLINK 1
#define HAVE_LINK 1
#define HAVE_TIMES 1
#define HAVE_TMPFILE 1
#define HAVE_TZNAME 1
#define HAVE_TZSET 1
#define HAVE_UNAME 1
#define HAVE_UTIMES 1
#define HAVE_WCSCOLL 1
#define HAVE_STRUCT_STAT_ST_BLKSIZE 1
#define HAVE_STRUCT_STAT_ST_BLOCKS 1
#define HAVE_STRUCT_STAT_ST_RDEV 1

#endif /* Py_PYCONFIG_H */
