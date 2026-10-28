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
#ifndef _pyGameMgr2_h_
#define _pyGameMgr2_h_

// CWE's GameMgr (minigame) Python glue for the py2 flavour (PLASMA_PYTHON_ABI=2 only; see
// Docs/Web/Python 2.3 flavour.md). MOULa's python.pak plays the Heek table, the KI marker games,
// the Eder Delin Blue Spiral and the Ahnonay quabs through Cyan's CWE classes (OpenUru
// CWE-ou-minkata 76ef5e01, FeatureLib/pfPython/Games/*, FeatureLib/pfGameMgr/*): game clients
// that post each server message to a script as a ptGameCliMsg (OnGameCliMsg). H-uru replaced
// them with handler-based clients (ptGmMarker, ptGmVarSync, ptGmBlueSpiral) and has no Heek
// client. The py2 flavour puts CWE's classes, functions and enums back over H-uru's pfGameMgr
// transport.
// Three files implement it: pyGameMgrWire2.cpp (the messages, the clients and the requests on
// the wire, with no Python), pyGameCli2.cpp (the game clients, the PlasmaGame functions and
// InstallGameClasses) and pyGameMsg2.cpp (the message classes, GameMsgHandlerName and GameMsgNew).

#include "pyPy2Compat.h"

#if PLASMA_PYTHON_ABI < 3

class plMessage;

namespace plPy2CWE
{
// Adds CWE's game classes and functions to PlasmaGame (replacing H-uru's ptGameCli) and its
// game enums to PlasmaGameConstants. Returns the number of names set, or -1 (with no Python
// error pending) on failure.
int InstallGameClasses();

// CWE plPythonFileMod::MsgReceive's OnGameMgrMsg and OnGameCliMsg branches
// (plPythonFileMod.cpp): for a message the game manager posts, the script
// handler's name ("OnGameMgrMsg" or "OnGameCliMsg"); nullptr for any other message.
const char* GameMsgHandlerName(plMessage* msg);

// The ptGameMgrMsg or ptGameCliMsg handed to that handler (CWE pyGameMgrMsg::New,
// pyGameCliMsg::New). New reference; nullptr with a Python error set on failure.
PyObject* GameMsgNew(plMessage* msg);
} // namespace plPy2CWE

#endif // PLASMA_PYTHON_ABI < 3

#endif // _pyGameMgr2_h_
