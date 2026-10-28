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

// The py2 flavour's python.pak import: a PEP 302 finder on sys.meta_path, in place of the
// PEP 451 pair of cyPythonModule451.cpp (Docs/Web/Python 2.3 flavour.md).
//
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file):
// Build/VS2010/SDKs/XPlatform/Cypython-2.3.3/Python/import_pack.c. Cyan's CPython 2.3.3 served
// the pak from a patched find_module there:
//   - find_module: for a top-level name (path == NULL), after the builtin and frozen checks,
//     Pl_IsItPacked(name) -> fd_plasmaPack; the external flavour returns it at once (no loose
//     file overrides the pak; only the internal flavour lets one override it);
//   - load_plasmapacked_module: Pl_OpenPacked(name) (PythonPack::OpenPythonPacked,
//     PC/PlasmaPack.cpp), then PyImport_ExecCodeModuleEx(name, co, name): the module's
//     __file__ is its name.
// Stock 2.3 (find_module, Python/import.c) asks sys.meta_path before the builtin and frozen
// checks, so the finder declines builtin and frozen names itself; it declines a name inside
// a package (path is not None) as Cyan's branch never sees one; python.pak holds no packages.

#include "pyPy2Compat.h"

#if PLASMA_PYTHON_ABI < 3

#include <cstring>
#include <string_theory/string>

#include "cyPythonInterface.h"
#include "plPythonPack.h"
#include "plStatusLog/plStatusLog.h"
#include "pyGlueHelpers.h"
#include "pyObjectRef.h"

// ==========================================================================

struct pyModulePackFinder2
{
    PYTHON_CLASS_NEW_DEFINITION;
};

PYTHON_CLASS_DEFINITION(ptModulePackFinder2, pyModulePackFinder2)
PYTHON_DEFAULT_NEW_DEFINITION(ptModulePackFinder2, pyModulePackFinder2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptModulePackFinder2)
PYTHON_NO_INIT_DEFINITION(ptModulePackFinder2)

// is_builtin and find_frozen, which are static in import.c
static bool IIsBuiltinOrFrozen(const char* name)
{
    for (const _inittab* p = PyImport_Inittab; p->name != nullptr; ++p) {
        if (std::strcmp(p->name, name) == 0)
            return true;
    }
    for (const _frozen* p = PyImport_FrozenModules; p && p->name != nullptr; ++p) {
        if (std::strcmp(p->name, name) == 0)
            return true;
    }
    return false;
}

// find_module(fullname, path=None) -> self (the loader) or None
PYTHON_METHOD_DEFINITION(ptModulePackFinder2, find_module, args)
{
    char*     name = nullptr;
    PyObject* path = Py_None;
    if (!PyArg_ParseTuple(args, "s|O", &name, &path))
        PYTHON_RETURN_ERROR;

    if (path == Py_None && !IIsBuiltinOrFrozen(name) && PythonPack::IsItPythonPacked(ST::string(name))) {
        Py_INCREF(self);
        return (PyObject*)self;
    }
    PYTHON_RETURN_NONE;
}

// load_module(fullname) -> the module (CWE import_pack.c, load_plasmapacked_module)
PYTHON_METHOD_DEFINITION(ptModulePackFinder2, load_module, args)
{
    char* name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name))
        PYTHON_RETURN_ERROR;

    pyObjectRef co = PythonPack::OpenPythonPacked(ST::string(name));
    if (!(co && PyCode_Check(co.Get()))) {
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_ImportError, "Could not load packed code for module %.200s", name);
        PYTHON_RETURN_ERROR;
    }
    return PyImport_ExecCodeModuleEx(name, co.Get(), name);
}

PYTHON_START_METHODS_TABLE(ptModulePackFinder2)
PYTHON_METHOD(ptModulePackFinder2, find_module, ""),
    PYTHON_METHOD(ptModulePackFinder2, load_module, ""),
    PYTHON_END_METHODS_TABLE;

PLASMA_DEFAULT_TYPE(ptModulePackFinder2, "PEP 302 module finder and loader for plPythonPack");
PYTHON_CLASS_NEW_IMPL(ptModulePackFinder2, pyModulePackFinder2);

// ==========================================================================

void PythonInterface::initPyPackHook()
{
    dbgLog->AddLine(plStatusLog::kGreen, "Begin initializing PEP 302 python.pak functionality...");

    // note: steals ref
    PyObject* mod = PyImport_AddModule("_PlasmaImport");
    if (!mod) {
        dbgLog->AddLine(plStatusLog::kRed, "Failed to create _PlasmaImport module!");
        return;
    }

    PYTHON_CLASS_IMPORT_START(mod);
    PYTHON_CLASS_IMPORT(mod, ptModulePackFinder2);
    PYTHON_CLASS_IMPORT_END(mod);

    dbgLog->AddLine(plStatusLog::kGreen, "Installing PEP 302 machinery...");

    pyObjectRef metaFinder = pyModulePackFinder2::New();
    PyObject*   finders = PySys_GetObject("meta_path");
    hsAssert(finders, "Failed to access sys.meta_path");
    hsAssert(PyList_Check(finders), "sys.meta_path finders is not a list");
    if (!metaFinder || !finders || PyList_Append(finders, metaFinder.Get()) != 0) {
        dbgLog->AddLine(plStatusLog::kRed, "Failed to install the PEP 302 python.pak finder!");
        PyErr_Print();
    }
}

#endif // PLASMA_PYTHON_ABI < 3
