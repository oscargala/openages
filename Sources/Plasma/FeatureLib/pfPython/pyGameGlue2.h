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
#ifndef _pyGameGlue2_h_
#define _pyGameGlue2_h_

// The Python side of CWE's GameMgr glue in the py2 flavour, shared by pyGameCli2.cpp (the game
// clients and the PlasmaGame functions) and pyGameMsg2.cpp (the messages): the glue classes
// behind the Python objects, the result conversions with CWE's types, and each file's class
// table, written with H-uru's glue macros (pyGlueHelpers.h).
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file): FeatureLib/pfPython/Games.

#include "pyPy2Compat.h"
#if PLASMA_PYTHON_ABI < 3

#include <cstddef>

#include "pyGameMgrWire2.h"
#include "pyGlueDefinitions.h"
#include "pyGlueHelpers.h"

namespace plPy2CWE
{
namespace Game
{

// ---------------------------------------------------------------------------------------------
// The glue classes. A game client object holds a reference to its client, and a message object
// one to its message (CWE's held raw pointers). Each base class's glue class exposes its type,
// the tp_base of the classes derived from it (PLASMA_DEFAULT_TYPE_WBASE).

// ptGameCli and the four game clients
class pyGameCli2
{
public:
    Cli* fCli = nullptr;

    ~pyGameCli2();

    PYTHON_EXPOSE_TYPE; // ptGameCli

    // A new object of `type` (ptGameCli or a game's client class) for `cli` (CWE pyGameCli::New
    // and the games' New).
    static PyObject* New(PyTypeObject* type, Cli* cli);
};

// The messages: one glue class per base class, each final class holding its base's
class pyGameMsg2
{
public:
    plMessage* fMsg = nullptr;

    ~pyGameMsg2();
};

class pyGameMgrMsg2 : public pyGameMsg2
{
public:
    PYTHON_EXPOSE_TYPE; // ptGameMgrMsg
};

class pyGameCliMsg2 : public pyGameMsg2
{
public:
    PYTHON_EXPOSE_TYPE; // ptGameCliMsg
};

class pyHeekMsg2 : public pyGameCliMsg2
{
public:
    PYTHON_EXPOSE_TYPE; // ptHeekMsg
};

class pyMarkerMsg2 : public pyGameCliMsg2
{
public:
    PYTHON_EXPOSE_TYPE; // ptMarkerMsg
};

class pyBlueSpiralMsg2 : public pyGameCliMsg2
{
public:
    PYTHON_EXPOSE_TYPE; // ptBlueSpiralMsg
};

class pyVarSyncMsg2 : public pyGameCliMsg2
{
public:
    PYTHON_EXPOSE_TYPE; // ptVarSyncMsg
};

// ---------------------------------------------------------------------------------------------
// Conversions with CWE's result types: PyInt_FromLong is `int`, PyLong_FromUnsignedLong and
// PyLong_FromDouble are `long`, PYTHON_RETURN_BOOL is the int 0 or 1 (CWE pyGlueHelpers.h),
// the wire's wchar strings are `unicode` (PyUnicode_FromWideChar), and so is the engine's text
// (PyPlasma_UnicodeFromSTString, pyPy2Compat.h).

inline PyObject* IInt(long v) { return PyInt_FromLong(v); }
inline PyObject* IULong(unsigned long v) { return PyPlasma_FromUnsignedLong(v); }
inline PyObject* IBool(bool v) { return PyInt_FromLong(v ? 1L : 0L); }

inline PyObject* IEmptyUnicode()
{
    return PyUnicode_FromUnicode(nullptr, 0);
}

// ---------------------------------------------------------------------------------------------
// The classes PlasmaGame gets, in the order InstallGameClasses adds them: pyGameCli2.cpp's, then
// pyGameMsg2.cpp's (IMsgClasses).

struct ClassDef
{
    const char*   name;
    PyTypeObject* type;
};

const ClassDef* IMsgClasses(size_t& count);

} // namespace Game
} // namespace plPy2CWE

#endif // PLASMA_PYTHON_ABI < 3

#endif // _pyGameGlue2_h_
