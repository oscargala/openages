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

// The py2 flavour's C-API shims and text contract (pyPy2Compat.h). Compiled only
// against CPython 2.3 (PLASMA_PYTHON_ABI=2); empty for CPython 3.

#include "pyPy2Compat.h"

#if PLASMA_PYTHON_ABI < 3

#include <cstring>
#include <string>
#include <string_theory/string>

#include "plFileSystem.h"
#include "pyGlueHelpers.h"
#include "pyObjectRef.h"
#include "pyPy2Text.h"

PyObject* PyPlasma_UnicodeFromSTString(const ST::string& str)
{
    std::u16string out = IUtf16FromUtf8(str.c_str(), str.size());
    static_assert(sizeof(Py_UNICODE) == sizeof(char16_t), "the py2 flavour is a UCS-2 build");
    return PyUnicode_FromUnicode(reinterpret_cast<const Py_UNICODE*>(out.data()), (int)out.size());
}

PyObject* PyPlasma_StrFromUtf8(const char* utf8, Py_ssize_t size)
{
    if (!utf8) {
        PyErr_SetString(PyExc_SystemError, "NULL string");
        return nullptr;
    }
    size_t      n = size < 0 ? std::strlen(utf8) : (size_t)size;
    std::string out = ICp1252FromUtf8(utf8, n);
    return PyString_FromStringAndSize(out.data(), (int)out.size());
}

// ---------------------------------------------------------------------------------------------
// The pfPython string converters (pyGlueHelpers.h) for CPython 2.3
ST::string PyUnicode_AsSTString(PyObject* obj)
{
    if (PyString_Check(obj)) {
        std::string out = IUtf8FromCp1252(PyString_AS_STRING(obj), (size_t)PyString_GET_SIZE(obj));
        return ST::string::from_utf8(out.data(), out.size(), ST::assume_valid);
    }
    if (PyObject_TypeCheck(obj, &PyUnicode_Type)) {
        // UCS-2 with surrogate pairs (the flavour's CPython is built with Py_UNICODE_SIZE 2)
        return ST::string::from_utf16(reinterpret_cast<const char16_t*>(PyUnicode_AS_UNICODE(obj)),
                                      (size_t)PyUnicode_GET_SIZE(obj), ST::substitute_invalid);
    }
    return ST::string();
}

int PyUnicode_STStringConverter(PyObject* obj, void* str)
{
    if (PyUnicode_Check(obj)) {
        *reinterpret_cast<ST::string*>(str) = PyUnicode_AsSTString(obj);
        return 1;
    }
    return 0;
}

// Cyan's client ran on Windows, whose file API takes '\' and '/' alike, and MOULa's
// scripts build paths with both (xKI.py, xDialogStartUp.py, xAvatarCustomization.py:
// PtGetUserPath() + "\\Avatars\\"). The py2 flavour's engine takes them the same way: every
// file name a script passes is normalized to the engine's separator (plFileName::Normalize).
int PyUnicode_PlFileNameDecoder(PyObject* obj, void* fn)
{
    if (PyUnicode_Check(obj)) {
        *reinterpret_cast<plFileName*>(fn) = plFileName(PyUnicode_AsSTString(obj)).Normalize('/');
        return 1;
    }
    PyErr_SetString(PyExc_TypeError, "expected a string");
    return 0;
}

wchar_t* PyUnicode_AsWideCharString(PyObject* o, Py_ssize_t* size)
{
    if (!PyUnicode_Check(o)) {
        PyErr_BadArgument();
        return nullptr;
    }
    ST::utf32_buffer wide = PyUnicode_AsSTString(o).to_utf32();
    wchar_t*         result = (wchar_t*)PyMem_Malloc((wide.size() + 1) * sizeof(wchar_t));
    if (!result) {
        PyErr_NoMemory();
        return nullptr;
    }
    for (size_t i = 0; i < wide.size(); ++i)
        result[i] = (wchar_t)wide.data()[i];
    result[wide.size()] = 0;
    if (size)
        *size = (Py_ssize_t)wide.size();
    return result;
}

// ---------------------------------------------------------------------------------------------
// Integers
PyObject* PyPlasma_FromUnsignedLong(unsigned long v)
{
    return PyLong_FromUnsignedLong(v);
}

long PyPlasma_AsLong(PyObject* o)
{
    if (o && PyInt_Check(o))
        return PyInt_AS_LONG(o);
    if (o && PyObject_TypeCheck(o, &PyLong_Type))
        return (PyLong_AsLong)(o);
    return PyInt_AsLong(o); // other numbers through nb_int, as 2.3 itself
}

unsigned long PyPlasma_AsUnsignedLong(PyObject* o)
{
    if (o && PyInt_Check(o)) {
        long v = PyInt_AS_LONG(o);
        if (v < 0) {
            PyErr_SetString(PyExc_OverflowError, "can't convert negative value to unsigned long");
            return (unsigned long)-1;
        }
        return (unsigned long)v;
    }
    return (PyLong_AsUnsignedLong)(o);
}

double PyPlasma_AsDouble(PyObject* o)
{
    if (o && PyInt_Check(o))
        return (double)PyInt_AS_LONG(o);
    return (PyLong_AsDouble)(o);
}

Py_ssize_t PyNumber_AsSsize_t(PyObject* o, PyObject* exc)
{
    pyObjectRef num = PyNumber_Int(o);
    if (!num)
        return -1;
    if (PyInt_Check(num.Get()))
        return (Py_ssize_t)PyInt_AS_LONG(num.Get());
    // a long beyond int range: as Python 3's PyNumber_AsSsize_t (Objects/abstract.c), an
    // OverflowError is clipped without `exc` and replaced by `exc` with it
    long v = (PyLong_AsLong)(num.Get());
    if (v == -1 && PyErr_Occurred()) {
        if (!PyErr_ExceptionMatches(PyExc_OverflowError))
            return -1;
        PyErr_Clear();
        if (!exc)
            return _PyLong_Sign(num.Get()) < 0 ? PY_SSIZE_T_MIN : PY_SSIZE_T_MAX;
        PyErr_Format(exc, "cannot fit '%.200s' into an index-sized integer", o->ob_type->tp_name);
        return -1;
    }
    return (Py_ssize_t)v;
}

// ---------------------------------------------------------------------------------------------
// Buffers
int PyObject_GetBuffer(PyObject* o, Py_buffer* view, int)
{
    const void* buf = nullptr;
    int         len = 0;
    if (PyObject_AsReadBuffer(o, &buf, &len) != 0)
        return -1;
    view->buf = const_cast<void*>(buf);
    view->len = len;
    view->obj = o;
    Py_INCREF(o);
    return 0;
}

void PyBuffer_Release(Py_buffer* view)
{
    Py_CLEAR(view->obj);
    view->buf = nullptr;
    view->len = 0;
}

// ---------------------------------------------------------------------------------------------
// Calls
PyObject* _PyObject_Vectorcall(PyObject* callable, PyObject* const* args, size_t nargsf, PyObject* kwnames)
{
    Py_ssize_t  nargs = (Py_ssize_t)(nargsf & ~PY_VECTORCALL_ARGUMENTS_OFFSET);
    Py_ssize_t  nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
    pyObjectRef tuple = PyTuple_New(nargs);
    if (!tuple)
        return nullptr;
    for (Py_ssize_t i = 0; i < nargs; ++i) {
        Py_INCREF(args[i]);
        PyTuple_SET_ITEM(tuple.Get(), i, args[i]);
    }
    pyObjectRef kwargs;
    if (nkw > 0) {
        kwargs = PyDict_New();
        if (!kwargs)
            return nullptr;
        for (Py_ssize_t i = 0; i < nkw; ++i) {
            if (PyDict_SetItem(kwargs.Get(), PyTuple_GET_ITEM(kwnames, i), args[nargs + i]) != 0)
                return nullptr;
        }
    }
    return PyObject_Call(callable, tuple.Get(), kwargs.Get());
}

int PyArg_ValidateKeywordArguments(PyObject* kwargs)
{
    if (!PyDict_Check(kwargs)) {
        // CPython 2.3's exported function, not its macro of the same name, which passes __FILE__
        // and would put this file's build path into the client (2.3 Include/pyerrors.h).
        (PyErr_BadInternalCall)();
        return 0;
    }
    int       pos = 0;
    PyObject *key, *value;
    while (PyDict_Next(kwargs, &pos, &key, &value)) {
        if (!PyString_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "keywords must be strings");
            return 0;
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Modules
int PyModule_AddFunctions(PyObject* m, PyMethodDef* functions)
{
    // As Py_InitModule4 (2.3 Python/modsupport.c), which CWE used for the Plasma module
    // (CWE cyPythonInterface.cpp, PythonInterface::initPython): a builtin function per entry,
    // its __module__ the module's name.
    const char* name = PyModule_GetName(m);
    if (!name)
        return -1;
    pyObjectRef n = PyString_FromString(const_cast<char*>(name));
    if (!n)
        return -1;
    PyObject* d = PyModule_GetDict(m);
    for (PyMethodDef* ml = functions; ml->ml_name != nullptr; ml++) {
        if ((ml->ml_flags & METH_CLASS) || (ml->ml_flags & METH_STATIC)) {
            PyErr_SetString(PyExc_ValueError, "module functions cannot set METH_CLASS or METH_STATIC");
            return -1;
        }
        pyObjectRef v = PyCFunction_NewEx(ml, nullptr, n.Get());
        if (!v || PyDict_SetItemString(d, ml->ml_name, v.Get()) != 0)
            return -1;
    }
    return 0;
}

int PyModule_SetDocString(PyObject* m, const char* doc)
{
    pyObjectRef v = PyString_FromString(const_cast<char*>(doc));
    if (!v)
        return -1;
    return PyDict_SetItemString(PyModule_GetDict(m), "__doc__", v.Get());
}

#endif // PLASMA_PYTHON_ABI < 3
