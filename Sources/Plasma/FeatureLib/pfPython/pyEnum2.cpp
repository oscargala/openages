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

// The py2 flavour's script enums: Cyan's own enum types, as MOULa's scripts saw them. Compiled
// only against CPython 2.3 (PLASMA_PYTHON_ABI=2).
//
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file; "CWE" below):
// FeatureLib/pfPython/pyEnum.cpp, the EnumValue and Enum types, pyEnum::AddPlasmaConstantsClasses
// and pyEnum::MakeEnum. Changes: the C identifiers carry a pyEnum2 prefix (H-uru's pyEnum.h names
// its C++ helper EnumValue), MakeEnum is named MakeEnumFromMap and takes its map by const
// reference, H-uru's PYTHON_TYPE_START writes ob_size, the compare macros are defined here (CWE
// pyGlueHelpers.h), `new` replaces TRACKED_NEW, snprintf replaces _snprintf (EnumValue_oct,
// EnumValue_hex), fputs(text, fp) replaces fprintf(fp, text) (EnumValue_print: the same output
// without format interpretation), and nb_long calls CPython 2.3's own PyLong_FromLong as
// PyPlasma_LongObject_FromLong (pyPy2Compat.h gives the name PyLong_FromLong the `int` of H-uru's
// glue), so long() of a value is a `long`, as in CWE (EnumValue_long).
//
// Bugs fixed against CWE:
// - pyEnum2Value_print (CWE EnumValue_print) releases the string it prints. tp_str and tp_repr
//   return a new reference, and CWE never released it, so every `print` of an enum value to a file
//   leaked one string.
// - pyEnum2Value_hash (CWE EnumValue_hash) returns -1 when it sets an exception, CPython's error
//   value for tp_hash (PyObject_Hash, and the dict and hash() callers that test for -1). CWE
//   returned 0, a valid hash, with the TypeError left pending. H-uru's py3 pyEnum.cpp had the same
//   fix (58ed2c153).
// - pyEnum2Enum_dealloc (CWE Enum_dealloc) stops the cycle collector tracking the Enum before it
//   clears the fields. Releasing a field can run code that collects; a tracked object with no
//   references then looks like garbage and is cleared and deallocated a second time (CPython's
//   own deallocators untrack first: dict_dealloc, subtype_dealloc). CWE never untracked.

#include "pyPy2Compat.h"

#if PLASMA_PYTHON_ABI < 3

#include <map>
#include <string>

#include "pyEnum.h"
#include "pyGlueHelpers.h"

// CWE's compare macros, which H-uru's pyGlueHelpers.h no longer has; undefined at the end of the
// file, so that they stay out of the files compiled after this one in a unity build.
#define PYTHON_DEFAULT_COMPARE(pythonClassName) pythonClassName##_compare
#define PYTHON_COMPARE_DEFINITION(pythonClassName, obj1, obj2) static int pythonClassName##_compare(PyObject* obj1, PyObject* obj2)
#define PYTHON_COMPARE_LESS_THAN return -1
#define PYTHON_COMPARE_GREATER_THAN return 1
#define PYTHON_COMPARE_EQUAL return 0

struct pyEnum2Value
{
    // clang-format off
    PyObject_HEAD
    long value;
    char* name;
    // clang-format on
};

static PyObject* NewEnumValue(char const* name, long value);

static PyObject* pyEnum2Value_new(PyTypeObject* type, PyObject* args, PyObject*)
{
    pyEnum2Value* self = (pyEnum2Value*)type->tp_alloc(type, 0);
    if (self != NULL) {
        char* nameTemp = NULL;
        long  value;
        if (!PyArg_ParseTuple(args, "l|s", &value, &nameTemp)) {
            Py_DECREF(self);
            return NULL;
        }

        // copy the value if it was passed
        if (nameTemp) {
            self->name = new char[strlen(nameTemp) + 1];
            strcpy(self->name, nameTemp);
            self->name[strlen(nameTemp)] = '\0';
        } else
            self->name = NULL;
        self->value = value;
    }

    return (PyObject*)self;
}

static void pyEnum2Value_dealloc(pyEnum2Value* self)
{
    if (self->name)
        delete[] self->name;
    self->ob_type->tp_free((PyObject*)self);
}

static int pyEnum2Value_print(PyObject* self, FILE* fp, int flags)
{
    // convert this object to a string
    PyObject* strObject = (flags & Py_PRINT_RAW) ? self->ob_type->tp_str(self) : self->ob_type->tp_repr(self);
    if (strObject == NULL)
        return -1; // failure

    const char* text = PyString_AsString(strObject);
    if (text == NULL) {
        Py_DECREF(strObject);
        return -1;
    }

    fputs(text, fp); // and print it to the file
    Py_DECREF(strObject);
    return 0;
}

static PyObject* pyEnum2Value_repr(PyObject* self)
{
    pyEnum2Value* obj = (pyEnum2Value*)self;
    if (obj->name == NULL) {
        // no name, so just output our value
        return PyString_FromFormat("%s(%ld)", obj->ob_type->tp_name, obj->value);
    } else {
        // we have a name, so output it
        return PyString_FromString(obj->name);
    }
}

static PyObject* pyEnum2Value_str(PyObject* self)
{
    pyEnum2Value* obj = (pyEnum2Value*)self;
    if (obj->name == NULL) {
        // no name, so return our value
        return PyString_FromFormat("%s(%ld)", obj->ob_type->tp_name, obj->value);
    } else {
        // just return the name
        return PyString_FromString(obj->name);
    }
}

// forward def because the type object isn't defined yet
static bool IsEnumValue(PyObject* obj);

static long pyEnum2Value_hash(PyObject* v)
{
    // stolen from python's int class
    if (!IsEnumValue(v)) {
        PyErr_SetString(PyExc_TypeError, "hash was passed a non enum value (this would be weird)");
        return -1;
    }
    long x = ((pyEnum2Value*)v)->value;
    if (x == -1)
        x = -2;
    return x;
}

static int pyEnum2Value_nonzero(pyEnum2Value* v)
{
    return v->value != 0;
}

static PyObject* pyEnum2Value_and(PyObject* v, PyObject* w)
{
    pyEnum2Value* obj = NULL;
    long          other = 0;
    if (IsEnumValue(v)) {
        obj = (pyEnum2Value*)v;
        if (!PyInt_Check(w)) {
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        other = PyInt_AsLong(w);
    } else if (IsEnumValue(w)) {
        obj = (pyEnum2Value*)w;
        if (!PyInt_Check(v)) {
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        other = PyInt_AsLong(v);
    } else {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    return PyInt_FromLong(obj->value & other);
}

static PyObject* pyEnum2Value_xor(PyObject* v, PyObject* w)
{
    pyEnum2Value* obj = NULL;
    long          other = 0;
    if (IsEnumValue(v)) {
        obj = (pyEnum2Value*)v;
        if (!PyInt_Check(w)) {
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        other = PyInt_AsLong(w);
    } else if (IsEnumValue(w)) {
        obj = (pyEnum2Value*)w;
        if (!PyInt_Check(v)) {
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        other = PyInt_AsLong(v);
    } else {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    return PyInt_FromLong(obj->value ^ other);
}

static PyObject* pyEnum2Value_or(PyObject* v, PyObject* w)
{
    pyEnum2Value* obj = NULL;
    long          other = 0;
    if (IsEnumValue(v)) {
        obj = (pyEnum2Value*)v;
        if (!PyInt_Check(w)) {
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        other = PyInt_AsLong(w);
    } else if (IsEnumValue(w)) {
        obj = (pyEnum2Value*)w;
        if (!PyInt_Check(v)) {
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        other = PyInt_AsLong(v);
    } else {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    return PyInt_FromLong(obj->value | other);
}

static PyObject* pyEnum2Value_int(pyEnum2Value* v)
{
    return PyInt_FromLong(v->value);
}

static PyObject* pyEnum2Value_long(pyEnum2Value* v)
{
    return PyPlasma_LongObject_FromLong(v->value);
}

static PyObject* pyEnum2Value_float(pyEnum2Value* v)
{
    return PyFloat_FromDouble((double)(v->value));
}

static PyObject* pyEnum2Value_oct(pyEnum2Value* v)
{
    char buf[100];
    long x = v->value;
    if (x < 0) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    if (x == 0)
        strcpy(buf, "0");
    else
        snprintf(buf, sizeof(buf), "0%lo", x);
    return PyString_FromString(buf);
}

static PyObject* pyEnum2Value_hex(pyEnum2Value* v)
{
    char buf[100];
    long x = v->value;
    if (x < 0) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    snprintf(buf, sizeof(buf), "0x%lx", x);
    return PyString_FromString(buf);
}

static int pyEnum2Value_coerce(PyObject** pv, PyObject** pw)
{
    if (PyInt_Check(*pw)) {
        long x = PyInt_AsLong(*pw);
        *pw = NewEnumValue(NULL, x);
        Py_INCREF(*pv);
        return 0;
    } else if (PyLong_Check(*pw)) {
        double x = PyLong_AsDouble(*pw);
        if (x == -1.0 && PyErr_Occurred())
            return -1;
        *pw = NewEnumValue(NULL, (long)x);
        Py_INCREF(*pv);
        return 0;
    } else if (PyFloat_Check(*pw)) {
        double x = PyFloat_AsDouble(*pw);
        *pw = NewEnumValue(NULL, (long)x);
        Py_INCREF(*pv);
        return 0;
    } else if (IsEnumValue(*pw)) {
        Py_INCREF(*pv);
        Py_INCREF(*pw);
        return 0;
    }
    return 1; // Can't do it
}

// we support some of the number methods
// clang-format off
PYTHON_START_AS_NUMBER_TABLE(pyEnum2Value)
    0,                             /*nb_add*/
    0,                             /*nb_subtract*/
    0,                             /*nb_multiply*/
    0,                             /*nb_divide*/
    0,                             /*nb_remainder*/
    0,                             /*nb_divmod*/
    0,                             /*nb_power*/
    0,                             /*nb_negative*/
    0,                             /*nb_positive*/
    0,                             /*nb_absolute*/
    (inquiry)pyEnum2Value_nonzero, /*nb_nonzero*/
    0,                             /*nb_invert*/
    0,                             /*nb_lshift*/
    0,                             /*nb_rshift*/
    (binaryfunc)pyEnum2Value_and,  /*nb_and*/
    (binaryfunc)pyEnum2Value_xor,  /*nb_xor*/
    (binaryfunc)pyEnum2Value_or,   /*nb_or*/
    (coercion)pyEnum2Value_coerce, /*nb_coerce*/
    (unaryfunc)pyEnum2Value_int,   /*nb_int*/
    (unaryfunc)pyEnum2Value_long,  /*nb_long*/
    (unaryfunc)pyEnum2Value_float, /*nb_float*/
    (unaryfunc)pyEnum2Value_oct,   /*nb_oct*/
    (unaryfunc)pyEnum2Value_hex,   /*nb_hex*/
    0,                             /*nb_inplace_add*/
    0,                             /*nb_inplace_subtract*/
    0,                             /*nb_inplace_multiply*/
    0,                             /*nb_inplace_divide*/
    0,                             /*nb_inplace_remainder*/
    0,                             /*nb_inplace_power*/
    0,                             /*nb_inplace_lshift*/
    0,                             /*nb_inplace_rshift*/
    0,                             /*nb_inplace_and*/
    0,                             /*nb_inplace_xor*/
    0,                             /*nb_inplace_or*/
    0,                             /* nb_floor_divide */
    0,                             /* nb_true_divide */
    0,                             /* nb_inplace_floor_divide */
    0,                             /* nb_inplace_true_divide */
PYTHON_END_AS_NUMBER_TABLE;
// clang-format on

PYTHON_COMPARE_DEFINITION(pyEnum2Value, v, w)
{
    long i = 0;
    long j = 0;
    if (IsEnumValue(v)) {
        i = ((pyEnum2Value*)v)->value;
        if (PyInt_Check(w))
            j = PyInt_AsLong(w);
        else if (PyFloat_Check(w))
            j = (long)PyFloat_AsDouble(w);
        else if (PyLong_Check(w))
            j = PyLong_AsLong(w);
        else if (IsEnumValue(w))
            j = ((pyEnum2Value*)w)->value;
        else {
            PyErr_SetString(PyExc_TypeError, "cannot compare pyEnum2Value to a non number");
            return 0;
        }

    } else if (IsEnumValue(w)) {
        j = ((pyEnum2Value*)w)->value;
        if (PyInt_Check(v))
            i = PyInt_AsLong(v);
        else if (PyFloat_Check(v))
            i = (long)PyFloat_AsDouble(v);
        else if (PyLong_Check(v))
            i = PyLong_AsLong(v);
        else if (IsEnumValue(v))
            i = ((pyEnum2Value*)v)->value;
        else {
            PyErr_SetString(PyExc_TypeError, "cannot compare pyEnum2Value to a non number");
            return 0;
        }
    } else {
        PyErr_SetString(PyExc_TypeError, "cannot compare pyEnum2Value to a non number");
        return 0;
    }

    if (i < j)
        PYTHON_COMPARE_LESS_THAN;
    else if (i > j)
        PYTHON_COMPARE_GREATER_THAN;
    PYTHON_COMPARE_EQUAL;
}

PYTHON_NO_INIT_DEFINITION(pyEnum2Value)

PYTHON_START_METHODS_TABLE(pyEnum2Value)
PYTHON_END_METHODS_TABLE;

// clang-format off
PYTHON_TYPE_START(pyEnum2Value)
    "PlasmaConstants.EnumValue",
    sizeof(pyEnum2Value),                       /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)pyEnum2Value_dealloc,           /* tp_dealloc */
    pyEnum2Value_print,                         /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    PYTHON_DEFAULT_COMPARE(pyEnum2Value),       /* tp_compare */
    pyEnum2Value_repr,                          /* tp_repr */
    PYTHON_DEFAULT_AS_NUMBER(pyEnum2Value),     /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    pyEnum2Value_hash,                          /* tp_hash */
    0,                                          /* tp_call */
    pyEnum2Value_str,                           /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT
    | Py_TPFLAGS_BASETYPE
    | Py_TPFLAGS_CHECKTYPES,                    /* tp_flags */
    "A basic enumeration value",                /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    PYTHON_DEFAULT_METHODS_TABLE(pyEnum2Value), /* tp_methods */
    0,                                          /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    PYTHON_DEFAULT_INIT(pyEnum2Value),          /* tp_init */
    0,                                          /* tp_alloc */
    pyEnum2Value_new                            /* tp_new */
PYTHON_TYPE_END;
// clang-format on

static bool IsEnumValue(PyObject* obj)
{
    return PyObject_TypeCheck(obj, &pyEnum2Value_type);
}

static PyObject* NewEnumValue(char const* name, long value)
{
    PyObject* tempArgs = NULL;
    if (name)
        tempArgs = Py_BuildValue("(ls)", value, name); // args are value, name
    else
        tempArgs = Py_BuildValue("(l)", value); // args are value only
    pyEnum2Value* newObj = (pyEnum2Value*)pyEnum2Value_type.tp_new(&pyEnum2Value_type, tempArgs, NULL);
    Py_DECREF(tempArgs); // clean up the temps
    return (PyObject*)newObj;
}

// Now for the pyEnum2Enum base class

struct pyEnum2Enum
{
    // clang-format off
    PyObject_HEAD
    PyObject* values;        // the values list
    PyObject* lookup;        // the enum values, key is enum, value is enum value
    PyObject* reverseLookup; // the enum values, key is enum value, value is enum
    // clang-format on
};

static PyObject* pyEnum2Enum_new(PyTypeObject* type, PyObject*, PyObject*)
{
    pyEnum2Enum* self = (pyEnum2Enum*)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->values = PyDict_New();
        if (self->values == NULL) {
            Py_DECREF(self);
            return NULL;
        }
        self->lookup = PyDict_New();
        if (self->lookup == NULL) {
            Py_DECREF(self);
            return NULL;
        }
        self->reverseLookup = PyDict_New();
        if (self->reverseLookup == NULL) {
            Py_DECREF(self);
            return NULL;
        }
    }

    return (PyObject*)self;
}

static int pyEnum2Enum_traverse(pyEnum2Enum* self, visitproc visit, void* arg)
{
    if (self->values && visit(self->values, arg) < 0)
        return -1;
    if (self->lookup && visit(self->lookup, arg) < 0)
        return -1;
    if (self->reverseLookup && visit(self->reverseLookup, arg) < 0)
        return -1;

    return 0;
}

static int pyEnum2Enum_clear(pyEnum2Enum* self)
{
    Py_XDECREF(self->values);
    self->values = NULL;
    Py_XDECREF(self->lookup);
    self->lookup = NULL;
    Py_XDECREF(self->reverseLookup);
    self->reverseLookup = NULL;

    return 0;
}

static void pyEnum2Enum_dealloc(pyEnum2Enum* self)
{
    PyObject_GC_UnTrack(self);
    pyEnum2Enum_clear(self);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject* pyEnum2Enum_getattro(PyObject* self, PyObject* attr_name)
{
    if (!PyString_Check(attr_name)) {
        PyErr_SetString(PyExc_TypeError, "getattro expects a string argument");
        PYTHON_RETURN_ERROR;
    }
    pyEnum2Enum* theEnum = (pyEnum2Enum*)self;
    PyObject*    item = PyDict_GetItem(theEnum->lookup, attr_name);
    if (!item) {
        PyErr_Clear(); // wasn't in the dictionary, check to see if they want the values or lookup dict
        char* name = PyString_AsString(attr_name);
        if (strcmp(name, "values") == 0) {
            Py_INCREF(theEnum->values);
            return theEnum->values;
        } else if (strcmp(name, "lookup") == 0) {
            Py_INCREF(theEnum->lookup);
            return theEnum->lookup;
        } else if (strcmp(name, "reverseLookup") == 0) {
            Py_INCREF(theEnum->reverseLookup);
            return theEnum->reverseLookup;
        }
        return PyObject_GenericGetAttr(self, attr_name); // let the default method handle it
    }
    Py_INCREF(item);
    return item;
}

static int pyEnum2Enum_setattro(PyObject* self, PyObject* attr_name, PyObject* value)
{
    if (!PyString_Check(attr_name)) {
        PyErr_SetString(PyExc_TypeError, "setattro expects a string argument");
        return -1;
    }
    pyEnum2Enum* theEnum = (pyEnum2Enum*)self;
    PyObject*    item = PyDict_GetItem(theEnum->lookup, attr_name);
    if (!item) {
        PyErr_Clear(); // wasn't in the dictionary, check to see if they want the values or lookup dict
        char* name = PyString_AsString(attr_name);
        if (strcmp(name, "values") == 0) {
            PyErr_SetString(PyExc_RuntimeError, "Cannot set the value attribute");
            return -1;
        } else if (strcmp(name, "lookup") == 0) {
            PyErr_SetString(PyExc_RuntimeError, "Cannot set the lookup attribute");
            return -1;
        } else if (strcmp(name, "reverseLookup") == 0) {
            PyErr_SetString(PyExc_RuntimeError, "Cannot set the reverseLookup attribute");
            return -1;
        }
        // they aren't trying to set an enum value or any of our special values, so let them
        return PyObject_GenericSetAttr(self, attr_name, value); // let the default method handle it
    }
    PyErr_SetString(PyExc_RuntimeError, "Cannot set any enum value");
    return -1;
}

PYTHON_NO_INIT_DEFINITION(pyEnum2Enum)

PYTHON_START_METHODS_TABLE(pyEnum2Enum)
PYTHON_END_METHODS_TABLE;

// clang-format off
PYTHON_TYPE_START(pyEnum2Enum)
    "PlasmaConstants.Enum",
    sizeof(pyEnum2Enum),                       /* tp_basicsize */
    0,                                         /* tp_itemsize */
    (destructor)pyEnum2Enum_dealloc,           /* tp_dealloc */
    0,                                         /* tp_print */
    0,                                         /* tp_getattr */
    0,                                         /* tp_setattr */
    0,                                         /* tp_compare */
    0,                                         /* tp_repr */
    0,                                         /* tp_as_number */
    0,                                         /* tp_as_sequence */
    0,                                         /* tp_as_mapping */
    0,                                         /* tp_hash */
    0,                                         /* tp_call */
    0,                                         /* tp_str */
    pyEnum2Enum_getattro,                      /* tp_getattro */
    pyEnum2Enum_setattro,                      /* tp_setattro */
    0,                                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT
    | Py_TPFLAGS_BASETYPE
    | Py_TPFLAGS_HAVE_GC,                      /* tp_flags */
    "pyEnum2Enum base class",                  /* tp_doc */
    (traverseproc)pyEnum2Enum_traverse,        /* tp_traverse */
    (inquiry)pyEnum2Enum_clear,                /* tp_clear */
    0,                                         /* tp_richcompare */
    0,                                         /* tp_weaklistoffset */
    0,                                         /* tp_iter */
    0,                                         /* tp_iternext */
    PYTHON_DEFAULT_METHODS_TABLE(pyEnum2Enum), /* tp_methods */
    0,                                         /* tp_members */
    0,                                         /* tp_getset */
    0,                                         /* tp_base */
    0,                                         /* tp_dict */
    0,                                         /* tp_descr_get */
    0,                                         /* tp_descr_set */
    0,                                         /* tp_dictoffset */
    PYTHON_DEFAULT_INIT(pyEnum2Enum),          /* tp_init */
    0,                                         /* tp_alloc */
    pyEnum2Enum_new                            /* tp_new */
PYTHON_TYPE_END;
// clang-format on

// creates and sets up the enum base class
void pyEnum::AddPlasmaConstantsClasses(PyObject* m)
{
    PYTHON_CLASS_IMPORT_START(m);
    PYTHON_CLASS_IMPORT(m, pyEnum2Value);
    PYTHON_CLASS_IMPORT(m, pyEnum2Enum);
    PYTHON_CLASS_IMPORT_END(m);
}

// makes an enum object using the specified name and values
void pyEnum::MakeEnumFromMap(PyObject* m, const char* name, const std::map<std::string, int>& values)
{
    if (m == NULL)
        return;

    pyEnum2Enum* newEnum = (pyEnum2Enum*)pyEnum2Enum_type.tp_new(&pyEnum2Enum_type, NULL, NULL);
    if (newEnum == NULL) {
        std::string errorStr = "Could not create enum named ";
        errorStr += name;
        PyErr_SetString(PyExc_RuntimeError, errorStr.c_str());
        return;
    }

    PyObject* valuesDict = newEnum->values;
    PyObject* lookupDict = newEnum->lookup;
    PyObject* reverseLookupDict = newEnum->reverseLookup;
    for (std::map<std::string, int>::const_iterator curValue = values.begin(); curValue != values.end(); curValue++) {
        std::string key = curValue->first;
        int         value = curValue->second;

        std::string enumValueName = name;
        enumValueName += "." + key;

        PyObject* newValue = NewEnumValue(enumValueName.c_str(), value);
        PyObject* valueObj = PyInt_FromLong((long)value);
        PyObject* keyObj = PyString_FromString(key.c_str());
        PyDict_SetItem(valuesDict, valueObj, newValue);
        PyDict_SetItem(lookupDict, keyObj, newValue);
        PyDict_SetItem(reverseLookupDict, newValue, keyObj);
        Py_DECREF(keyObj);
        Py_DECREF(valueObj);
        Py_DECREF(newValue);
    }

    PyModule_AddObject(m, (char*)name, (PyObject*)newEnum);
}

#undef PYTHON_DEFAULT_COMPARE
#undef PYTHON_COMPARE_DEFINITION
#undef PYTHON_COMPARE_LESS_THAN
#undef PYTHON_COMPARE_GREATER_THAN
#undef PYTHON_COMPARE_EQUAL

#endif // PLASMA_PYTHON_ABI < 3
