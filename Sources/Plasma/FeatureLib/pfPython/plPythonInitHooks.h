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

// The Python init test seam: where a harness build's Python instruments (testhooks/python/) meet
// PythonInterface::initPython. The product build (PLASMA_TEST_HOOKS OFF) has none of it: this header declares nothing
// there, and its call sites in cyPythonInterface.cpp are under #ifdef PLASMA_TEST_HOOKS. In a harness build the
// pointer is null until plTestHooks::Install sets it, and every default does nothing.
#ifndef _plPythonInitHooks_h_
#define _plPythonInitHooks_h_

#ifdef PLASMA_TEST_HOOKS

class plStatusLog;

class plPythonInitHooks
{
public:
    virtual ~plPythonInitHooks() = default;

    // In PythonInterface::initPython of the py3 flavour, on the client thread, before the interpreter's core init and
    // again after its main init: whether this run has a harness build's local game code (a configured directory,
    // plResPatcherLocalGameCode.h). The external flavour then pins the module search path empty and reports the
    // sys.path entries.
    virtual bool LocalGameCode() { return false; }

    // In PythonInterface::initPython, on the client thread, right after the "interpreter is now alive" line: the pak
    // finder and the six Plasma modules are in place (in the py2 flavour also site), the output redirectors are not
    // yet, and no game script has been imported. pythonLog is the interpreter's log (Python.log).
    virtual void BeforeScripts(plStatusLog* /*pythonLog*/) {}

    // The end of PythonInterface::initPython, on the client thread: the six Plasma modules, the pak finder and the
    // output redirectors are in place, and no game script has been imported yet.
    virtual void AfterInit() {}

    static plPythonInitHooks* Get() { return fHooks; }
    static void               Set(plPythonInitHooks* hooks) { fHooks = hooks; }

private:
    static inline plPythonInitHooks* fHooks = nullptr;
};

#endif // PLASMA_TEST_HOOKS

#endif // _plPythonInitHooks_h_
