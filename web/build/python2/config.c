/* Module configuration for CPython 2.3.3 on wasm32-emscripten.
 *
 * Shape: CPython 2.3.3's Modules/config.c.in (copyright BeOpen.com, CNRI, Stichting Mathematisch Centrum),
 * filled in by hand as Cyan's PC/config.c is.
 *
 * Licence: this file is derived from CPython, so it is under CPython's licence, not the GPL of the client's own
 * code. LICENSE-PSF.txt beside it is CPython 2.3.3's LICENSE, verbatim: the Python Software Foundation licence,
 * which includes the BeOpen, CNRI and CWI terms of the earlier releases. build.sh installs it with the library,
 * as lib/python2.3/LICENSE.txt.
 *
 * Module set: the builtin modules MOULa's python.pak imports and does not
 * carry itself, measured from the pak's IMPORT_NAME targets, plus the os module (`nt`, as Cyan's PC/config.c,
 * or `posix`; see pyconfig.h) and `gc`, which
 * the stock template and Cyan's PC/config.c both register (gcmodule.c is core either way). `pcre` is imported by
 * the pak's pre.py (a list of the IMPORT_NAME targets alone misses it) and is in Cyan's PC/config.c. Cyan's other
 * builtins (audioop, cmath, imageop, md5, regex, rgbimg, rotor, sha, signal, thread, msvcrt,
 * xreadlines, _hotshot, xxsubtype) are not imported by the pak and are left out.
 */

#include "Python.h"

/* The sizes pyconfig.h declares are the target's. */
#include <time.h>
#include <stdio.h>
#include <sys/types.h>
#include <wchar.h>
typedef char py2_check_long[sizeof(long) == SIZEOF_LONG ? 1 : -1];
typedef char py2_check_void_p[sizeof(void *) == SIZEOF_VOID_P ? 1 : -1];
typedef char py2_check_time_t[sizeof(time_t) == SIZEOF_TIME_T ? 1 : -1];
typedef char py2_check_off_t[sizeof(off_t) == SIZEOF_OFF_T ? 1 : -1];
typedef char py2_check_fpos_t[sizeof(fpos_t) == SIZEOF_FPOS_T ? 1 : -1];
typedef char py2_check_wchar_t[sizeof(wchar_t) == SIZEOF_WCHAR_T ? 1 : -1];
typedef char py2_check_unicode[sizeof(Py_UNICODE) == Py_UNICODE_SIZE ? 1 : -1];

extern void init_codecs(void);
extern void init_locale(void);
extern void init_random(void);
extern void init_sre(void);
extern void init_weakref(void);
extern void initarray(void);
extern void initbinascii(void);
extern void initcPickle(void);
extern void initcStringIO(void);
extern void initerrno(void);
extern void inititertools(void);
extern void initmath(void);
extern void initoperator(void);
extern void initpcre(void);
#ifdef PY2_OS_MODULE_NT
extern void initnt(void);
#else
extern void initposix(void);
#endif
extern void initstrop(void);
extern void initstruct(void);
extern void inittime(void);
extern void initzipimport(void);

extern void PyMarshal_Init(void);
extern void initimp(void);
extern void initgc(void);

struct _inittab _PyImport_Inittab[] = {
	{"_codecs", init_codecs},
	{"_locale", init_locale},
	{"_random", init_random},
	{"_sre", init_sre},
	{"_weakref", init_weakref},
	{"array", initarray},
	{"binascii", initbinascii},
	{"cPickle", initcPickle},
	{"cStringIO", initcStringIO},
	{"errno", initerrno},
	{"itertools", inititertools},
	{"math", initmath},
	{"operator", initoperator},
	{"pcre", initpcre},
#ifdef PY2_OS_MODULE_NT
	{"nt", initnt},
#else
	{"posix", initposix},
#endif
	{"strop", initstrop},
	{"struct", initstruct},
	{"time", inittime},
	{"zipimport", initzipimport},

	/* This module lives in marshal.c */
	{"marshal", PyMarshal_Init},

	/* This lives in import.c */
	{"imp", initimp},

	/* These entries are here for sys.builtin_module_names */
	{"__main__", NULL},
	{"__builtin__", NULL},
	{"sys", NULL},
	{"exceptions", NULL},

	/* This lives in gcmodule.c */
	{"gc", initgc},

	/* Sentinel */
	{0, 0}
};
