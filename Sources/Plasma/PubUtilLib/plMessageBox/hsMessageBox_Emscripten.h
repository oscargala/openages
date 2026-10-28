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

#ifndef _hsMessageBox_Emscripten_h_
#define _hsMessageBox_Emscripten_h_

#include <functional>

#include "hsMessageBox.h"

// The browser build's message box for a caller whose own loop must keep running while the box waits for the player.
//
// A Win32 box is modal to its owner window, but its message loop dispatches every message of the thread, so the
// owner's timers keep firing behind it. The login dialog relies on that: its AUTH_LOGIN_TIMER calls NetCommUpdate every
// 10 ms while the "Authentication Failed" box is open (Apps/plClient/win32/winmain.cpp: SetTimer at WM_INITDIALOG,
// KillTimer only at WM_DESTROY, the box from the dialog's own WM_COMMAND). hsMessageBox blocks its thread until the
// answer instead, as the Mac's alert does; this variant shows the same page dialog and calls `pump` every `intervalMs`
// until the player answers. Without a page to wait on (the main browser thread, a suppressed prompt or the test
// harness's answer) it behaves as hsMessageBox.
hsMessageBoxResult hsMessageBoxPumping(const ST::string& message, const ST::string& caption, hsMessageBoxKind kind,
                                       hsMessageBoxIcon icon, const std::function<void()>& pump, double intervalMs);

#endif // _hsMessageBox_Emscripten_h_
