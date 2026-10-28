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

// The resource patcher's local game-code seam: where a harness build's own game code (testhooks/client/
// plTestHooksGameCode.cpp: the test harness's sealed directory, or the one a test deployment mounts) enters
// plResPatcher::CreatePatcher under -LocalPython, through OnGameCodeDiscovered as a shard's game code does. The
// product build (PLASMA_TEST_HOOKS OFF) has none of it: this header declares nothing there, and its one call site in
// plResPatcher.cpp is under #ifdef PLASMA_TEST_HOOKS, so the product's CreatePatcher is upstream's. In a harness build
// the pointer is null until plTestHooks::Install sets it, and the default inserts nothing. (plResPatcherObserver.h is
// the patcher's other seam, which only reads.)
#ifndef _plResPatcherLocalGameCode_h_
#define _plResPatcherLocalGameCode_h_

#ifdef PLASMA_TEST_HOOKS

#include <functional>

class hsStream;
class plFileName;

class plResPatcherLocalGameCode
{
public:
    // Inserts one file into plStreamSource (plResPatcher::OnGameCodeDiscovered), which takes the stream.
    using InsertFn = std::function<void(const plFileName& file, hsStream* stream)>;

    virtual ~plResPatcherLocalGameCode() = default;

    // plResPatcher::CreatePatcher, on the client thread, before patcher.Start() and before upstream's RequestGameCode
    // branch, while no game code has been requested and under -LocalPython. Returns whether a local game-code
    // directory is configured; under -LocalSDL a configured one means the shard is asked for no game code. When the
    // whole directory is valid, its files are handed to insert, synchronously (python/<one>.pak, and with withSdl,
    // which is -LocalSDL, every SDL/*.sdl); when it is not, none is. insert is not kept after the call.
    virtual bool Insert(bool withSdl, const InsertFn& insert) { return false; }

    static plResPatcherLocalGameCode* Get() { return fLocal; }
    static void                       Set(plResPatcherLocalGameCode* local) { fLocal = local; }

private:
    static inline plResPatcherLocalGameCode* fLocal = nullptr;
};

#endif // PLASMA_TEST_HOOKS

#endif // _plResPatcherLocalGameCode_h_
