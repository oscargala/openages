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

// The message-box test seam: how a harness build answers hsMessageBox, so that an unattended run never waits for a
// player. The product build (PLASMA_TEST_HOOKS OFF) has none of it: this header declares nothing there, and the call
// site in hsMessageBox is under #ifdef PLASMA_TEST_HOOKS. In a harness build the pointer is null until
// plTestHooks::Install sets it; no responder, or the default policy, shows the box as the product does. The ID in the
// comment names the member's instrument in the test harness's own records, which are not part of this repository.
#ifndef _hsMessageBoxTestResponder_h_
#define _hsMessageBoxTestResponder_h_

#ifdef PLASMA_TEST_HOOKS

class hsMessageBoxTestResponder
{
public:
    enum AnswerPolicy
    {
        kAnswerManual, // show the box and wait for a click, as the product does
        kAnswerAuto,   // answer with the default (first) button without showing the box
    };

    virtual ~hsMessageBoxTestResponder() = default;

    // For each box, before it is shown: auto-answer unattended runs, or keep the box for a harness click (M-04).
    virtual AnswerPolicy GetAnswerPolicy() const { return kAnswerManual; }

    static hsMessageBoxTestResponder* Get() { return fResponder; }
    static void                       Set(hsMessageBoxTestResponder* responder) { fResponder = responder; }

private:
    static inline hsMessageBoxTestResponder* fResponder = nullptr;
};

#endif // PLASMA_TEST_HOOKS

#endif // _hsMessageBoxTestResponder_h_
