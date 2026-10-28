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

// The resource-patcher test seam: what a harness build's game-code capture (testhooks/client/
// plTestHooksGameCode.cpp) sees of plResPatcher's game-code patch. The product build
// (PLASMA_TEST_HOOKS OFF) has none of it: this header declares nothing there, and every call site in plResPatcher.cpp
// is under #ifdef PLASMA_TEST_HOOKS. In a harness build the pointer is null until plTestHooks::Install sets it, and
// every default does nothing. The observer only reads: it never changes what the patcher asks for or inserts (a
// harness build's own game code enters through the other seam, plResPatcherLocalGameCode.h).
#ifndef _plResPatcherObserver_h_
#define _plResPatcherObserver_h_

#ifdef PLASMA_TEST_HOOKS

#include "pnNetBase/pnNbError.h"

class plFileName;

class plResPatcherObserver
{
public:
    virtual ~plResPatcherObserver() = default;

    // plResPatcher::CreatePatcher, on the client thread, before patcher.Start(): this patch asks the shard for its game
    // code (upstream's RequestGameCode branch). Only the files that arrive after this call are the shard's.
    virtual void OnGameCodeRequested() {}

    // plResPatcher::OnGameCodeDiscovered, once the file is in plStreamSource: secure is true when plSecureStream
    // opened it (the shard sent it encrypted, and plStreamSource holds the decrypted bytes). The shard's files arrive
    // on the patcher's worker thread; the local game code's on the client thread, before OnGameCodeRequested.
    virtual void OnGameCodeInserted(const plFileName& file, bool secure) {}

    // plResPatcher::OnCompletion, on the patcher's worker thread, before plResPatcherMsg reaches the client (and so
    // before the client initialises SDL and Python from the inserted files).
    virtual void OnPatchComplete(ENetError result) {}

    static plResPatcherObserver* Get() { return fObserver; }
    static void                  Set(plResPatcherObserver* observer) { fObserver = observer; }

private:
    static inline plResPatcherObserver* fObserver = nullptr;
};

#endif // PLASMA_TEST_HOOKS

#endif // _plResPatcherObserver_h_
