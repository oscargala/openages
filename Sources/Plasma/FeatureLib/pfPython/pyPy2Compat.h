/*==LICENSE==*

CyanWorlds.com Engine - MMOG client, server and tools
Copyright (C) 2011  Cyan Worlds, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

Additional permissions under GNU GPL version 3 section 7

If you modify this Program, or any covered work, by linking or
combining it with any of RAD Game Tools Bink SDK, Autodesk 3ds Max SDK,
NVIDIA PhysX SDK, Microsoft DirectX SDK, OpenSSL library, Independent
JPEG Group JPEG library, Microsoft Windows Media SDK, or Apple QuickTime SDK
(or a modified version of those libraries),
containing parts covered by the terms of the Bink SDK EULA, 3ds Max EULA,
PhysX SDK EULA, DirectX SDK EULA, OpenSSL and SSLeay licenses, IJG
JPEG Library README, Windows Media SDK EULA, or QuickTime SDK EULA, the
licensors of this Program grant you additional
permission to convey the resulting work. Corresponding Source for a
non-source form of such a combination shall include the source code for
the parts of OpenSSL and IJG JPEG Library used as well as that of the covered
work.

You can contact Cyan Worlds, Inc. by email legal@cyan.com
 or by snail mail at:
      Cyan Worlds, Inc.
      14617 N Newport Hwy
      Mead, WA   99021

*==LICENSE==*/
#ifndef _pyPy2Compat_h_
#define _pyPy2Compat_h_

// One glue source, two Python ABIs. Every pfPython unit (and the client's frame loop) includes
// Python through this header, never <Python.h> directly. With CPython 3 it is exactly <Python.h>.
// With CPython 2.3.3 (the py2 flavour, PLASMA_PYTHON_ABI=2) it adds the Python 3 C-API names
// H-uru's glue uses that 2.3 lacks, and maps them onto Python 2.3 with the contract MOULa's
// scripts were written against: Cyan's CWE glue (OpenUru CWE-ou-minkata 76ef5e01). The flavour's
// design is in Docs/Web/Python 2.3 flavour.md.
//   - text: the engine's ST::string is UTF-8; a script sees an 8-bit `str` in Windows-1252
//     (the pak's localized constants are cp1252). Python 3's "PyUnicode" constructors therefore
//     make a cp1252 `str`, and the converters accept a cp1252 `str` or a `unicode`. CWE's
//     `unicode` returns (its *W twins) come from the overlay (pyCWEOverlay2.cpp).
//   - integers: PyLong_FromLong makes an `int`, as CWE's PyInt_FromLong; PyLong_FromUnsigned*
//     makes a `long`, as CWE's PyLong_FromUnsignedLong; the PyLong_As* readers accept both.
//   - bool: CWE returned every bool as the int 0/1 (PYTHON_RETURN_BOOL in CWE's
//     pyGlueHelpers.h), so the py2 flavour does too (pyGlueHelpers.h, plPythonConvert.h).
#include <Python.h>

// The flavour selector is PLASMA_PYTHON_ABI, 3 or 2, which the root CMakeLists.txt defines on the
// Python target the build links. pfPython tests it, never PY_MAJOR_VERSION; this checks that it
// agrees with the headers.
#if !defined(PLASMA_PYTHON_ABI)
#error "PLASMA_PYTHON_ABI is not defined (the root CMakeLists.txt defines it on the Python target)"
#elif PLASMA_PYTHON_ABI != PY_MAJOR_VERSION
#error "PLASMA_PYTHON_ABI does not match the Python headers (see the root CMakeLists.txt)"
#endif

#if PLASMA_PYTHON_ABI < 3

#include <compile.h> // PyCodeObject (Python.h includes it from 2.4 on)
#include <eval.h>    // PyEval_EvalCode
#include <frameobject.h>

#include <climits>
#include <string_theory/string>

// ---------------------------------------------------------------------------------------------
// Types and object macros (2.5+/3.x names)
typedef int Py_ssize_t; // 2.3 sizes and indices are int (wasm32: 32-bit either way)
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
#ifndef Py_TYPE
#define Py_TYPE(ob) (((PyObject*)(ob))->ob_type)
#endif
#define PyVarObject_HEAD_INIT(type, size) PyObject_HEAD_INIT(type) size,
#define Py_RETURN_NONE return Py_INCREF(Py_None), Py_None
#define Py_RETURN_TRUE return Py_INCREF(Py_True), Py_True
#define Py_RETURN_FALSE return Py_INCREF(Py_False), Py_False
#define Py_RETURN_NOTIMPLEMENTED return Py_INCREF(Py_NotImplemented), Py_NotImplemented
#define Py_CLEAR(op)                         \
    do {                                     \
        PyObject* _py_tmp = (PyObject*)(op); \
        if (_py_tmp != nullptr) {            \
            (op) = nullptr;                  \
            Py_DECREF(_py_tmp);              \
        }                                    \
    } while (0)

// ---------------------------------------------------------------------------------------------
// const-correct overloads of the 2.3 functions that take `char*` (C++ overloads with C++
// linkage beside the C ones; the C function is called unchanged)
inline PyObject* PyObject_GetAttrString(PyObject* o, const char* n) { return PyObject_GetAttrString(o, const_cast<char*>(n)); }
inline int       PyObject_SetAttrString(PyObject* o, const char* n, PyObject* v) { return PyObject_SetAttrString(o, const_cast<char*>(n), v); }
inline int       PyObject_HasAttrString(PyObject* o, const char* n) { return PyObject_HasAttrString(o, const_cast<char*>(n)); }
inline PyObject* PyImport_AddModule(const char* n) { return PyImport_AddModule(const_cast<char*>(n)); }
inline PyObject* PyImport_ImportModule(const char* n) { return PyImport_ImportModule(const_cast<char*>(n)); }
inline int       PyModule_AddObject(PyObject* m, const char* n, PyObject* o) { return PyModule_AddObject(m, const_cast<char*>(n), o); }
inline PyObject* PySys_GetObject(const char* n) { return PySys_GetObject(const_cast<char*>(n)); }
inline int       PySys_SetObject(const char* n, PyObject* o) { return PySys_SetObject(const_cast<char*>(n), o); }
inline PyObject* PyEval_EvalCode(PyObject* co, PyObject* g, PyObject* l) { return PyEval_EvalCode((PyCodeObject*)co, g, l); }
inline PyObject* PyImport_ExecCodeModuleEx(const char* n, PyObject* co, const char* p)
{
    return PyImport_ExecCodeModuleEx(const_cast<char*>(n), co, const_cast<char*>(p));
}

// ---------------------------------------------------------------------------------------------
// Integers. Python 3 has one integer type where 2.3 has `int` and `long`, and H-uru's glue builds
// and tests it with the PyLong_* names. Three of those are 2.3 names too, which this header gives
// the meaning of H-uru's glue, the one CWE's glue had at the same places:
//   - PyLong_FromLong makes an `int` (CWE's PyInt_FromLong). Every call site the py2 flavour
//     compiles was compared with CWE's glue: where CWE made a `long` for a method MOULa's scripts
//     call, the overlay (pyCWEOverlay2.cpp) turns the result into one. Code ported from CWE that
//     means 2.3's own `long` calls PyPlasma_LongObject_FromLong (pyEnum2.cpp).
//   - PyLong_Check accepts an `int` or a `long`, and the PyLong_As* readers below read both.
//     Only the readers the glue calls are mapped; any other PyLong_As* name is 2.3's own.
// 2.3's own PyLong_* (`long` objects only) are used under their names in this header's .cpp.
inline PyObject* PyPlasma_LongObject_FromLong(long v) { return PyLong_FromLong(v); }
inline int       PyPlasma_IsIntOrLong(PyObject* op) { return PyInt_Check(op) || PyObject_TypeCheck(op, &PyLong_Type); }
#undef PyLong_Check
#define PyLong_Check(op) PyPlasma_IsIntOrLong((PyObject*)(op))
#define PyLong_FromLong(v) PyInt_FromLong((long)(v))
#define PyLong_FromSsize_t(v) PyInt_FromLong((long)(v))
#define PyLong_FromSize_t(v) PyPlasma_FromUnsignedLong((unsigned long)(v))
PyObject*     PyPlasma_FromUnsignedLong(unsigned long v); // `long`, as CWE's PyLong_FromUnsignedLong
long          PyPlasma_AsLong(PyObject* o);
unsigned long PyPlasma_AsUnsignedLong(PyObject* o);
double        PyPlasma_AsDouble(PyObject* o);
#define PyLong_AsLong(o) PyPlasma_AsLong(o)
#define PyLong_AsUnsignedLong(o) PyPlasma_AsUnsignedLong(o)
#define PyLong_AsDouble(o) PyPlasma_AsDouble(o)
Py_ssize_t PyNumber_AsSsize_t(PyObject* o, PyObject* exc);

// ---------------------------------------------------------------------------------------------
// Text: cp1252 `str` at the boundary (see above). PyUnicode_Check, a 2.3 name, accepts both
// string types, as every Python 3 `str` site corresponds to CWE's `str` (char*) or `unicode`
// (wchar_t*); every call site the py2 flavour compiles converts with a function below or in
// pyPy2Compat.cpp that takes both (PyUnicode_AsSTString, PyUnicode_AsWideCharString, ...).
// There is no PyUnicode_AsUTF8: a `str` holds cp1252, not UTF-8, so a site that needs the
// engine's text calls PyUnicode_AsSTString, and one that needs a `str`'s bytes says so.
inline int PyPlasma_IsStrOrUnicode(PyObject* op) { return PyString_Check(op) || PyObject_TypeCheck(op, &PyUnicode_Type); }
#undef PyUnicode_Check
#define PyUnicode_Check(op) PyPlasma_IsStrOrUnicode((PyObject*)(op))
// A new cp1252 `str` from UTF-8. The text can also be a C string that is not UTF-8
// (PyUnicode_FromString of a char* the engine holds): a byte UTF-8 rejects is kept as it is
// (pyPy2Text.h ICp1252FromUtf8).
PyObject* PyPlasma_StrFromUtf8(const char* utf8, Py_ssize_t size);
// The engine's text as a `unicode` (UCS-2, with surrogate pairs), where PyUnicode_FromSTString
// makes a cp1252 `str` (pyGlueHelpers.h): what CWE's PyUnicode_FromWideChar returned for its *W
// twins and its unicode functions. A byte UTF-8 rejects becomes the code point of its value
// (pyPy2Text.h IUtf16FromUtf8).
PyObject* PyPlasma_UnicodeFromSTString(const ST::string& str);
#define PyUnicode_FromString(s) PyPlasma_StrFromUtf8((s), -1)
#define PyUnicode_FromStringAndSize(s, n) PyPlasma_StrFromUtf8((s), (Py_ssize_t)(n))
wchar_t* PyUnicode_AsWideCharString(PyObject* o, Py_ssize_t* size); // PyMem_Malloc'd UTF-32
#define PyBytes_AsString(o) PyString_AsString(o)
#define PyBytes_Size(o) PyString_Size(o)

// ---------------------------------------------------------------------------------------------
// Buffers (Python 3 Py_buffer over the 2.3 read-buffer protocol, abstract.h PyObject_AsReadBuffer)
struct Py_buffer
{
    void*      buf;
    Py_ssize_t len;
    PyObject*  obj;
};
#define PyBUF_SIMPLE 0
int  PyObject_GetBuffer(PyObject* o, Py_buffer* view, int flags);
void PyBuffer_Release(Py_buffer* view);

// ---------------------------------------------------------------------------------------------
// Calls (Python 3.8+ vectorcall, by tuple and dict in 2.3)
#define PY_VECTORCALL_ARGUMENTS_OFFSET ((size_t)1 << (8 * sizeof(size_t) - 1))
PyObject*        _PyObject_Vectorcall(PyObject* callable, PyObject* const* args, size_t nargsf, PyObject* kwnames);
inline PyObject* PyObject_CallNoArgs(PyObject* o) { return PyObject_CallObject(o, nullptr); }
int              PyArg_ValidateKeywordArguments(PyObject* kwargs);

// ---------------------------------------------------------------------------------------------
// Modules
int PyModule_AddFunctions(PyObject* m, PyMethodDef* functions); // as 2.3's Py_InitModule4 (modsupport.c)
int PyModule_SetDocString(PyObject* m, const char* doc);

// ---------------------------------------------------------------------------------------------
// Frames (3.9+ accessors over the 2.3 frame struct)
inline PyFrameObject* PyThreadState_GetFrame(PyThreadState* ts)
{
    Py_XINCREF(ts->frame);
    return ts->frame;
}
inline PyCodeObject* PyFrame_GetCode(PyFrameObject* f)
{
    Py_INCREF(f->f_code);
    return f->f_code;
}
inline PyFrameObject* PyFrame_GetBack(PyFrameObject* f)
{
    Py_XINCREF(f->f_back);
    return f->f_back;
}
inline int            PyFrame_GetLineNumber(PyFrameObject* f) { return PyCode_Addr2Line(f->f_code, f->f_lasti); }
// The current thread state without the NULL check (2.3: the global, pystate.h PyThreadState_GET)
inline PyThreadState* _PyThreadState_UncheckedGet() { return _PyThreadState_Current; }

#endif // PLASMA_PYTHON_ABI < 3

#endif // _pyPy2Compat_h_
