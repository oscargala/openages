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

// Per-frame hooks of the browser frame loop.
//
// Platform layers that must run once per frame on the client thread (canvas input, audio pumping, ...) register here
// instead of editing main.cpp's loop. Every hook runs at the start of every tick of plEmscriptenFrameLoop, before the
// client's work for that frame (plClient::MainLoop once the game runs; the loader and login states before that), in the
// order added. Hooks run on the client thread only, and never while a tick is suspended inside a long call (an Age
// load, the intro movie): during those, the native client pumps its window messages through plClient's message-pump
// proc (PumpMessageQueueProc in main.cpp).
#ifndef plEmscriptenFrameHooks_inc
#define plEmscriptenFrameHooks_inc

namespace plEmscriptenFrameHooks
{
using HookProc = void (*)(void* user);

// Registers a hook. Not thread-safe: call it either on the client thread (from a hook, or from code that main() runs
// before the loop starts), or before main() from a static initializer in a source file of the plClient target itself
// (its objects are always linked; static constructors run once, on the browser's main thread, before the client thread
// exists). Adding the same (proc, user) twice registers it twice.
void Add(HookProc proc, void* user = nullptr);

// Unregisters the first hook registered with (proc, user); false if there is none.
bool Remove(HookProc proc, void* user = nullptr);

// Runs every hook (main.cpp, at the start of each tick).
void RunAll();

// Native-style platform message pumps, also called within long loads/movies.
// Same registration/thread rules as Add/Remove. Input only: do not suspend,
// start client/GPU work, or rerun the ordinary frame hooks from these callbacks.
void AddMessagePump(HookProc proc, void* user = nullptr);
bool RemoveMessagePump(HookProc proc, void* user = nullptr);
void RunMessagePumps();

// Shutdown hooks: a platform layer's own teardown, for state it keeps across frames that must go while the client and
// its resource manager still exist (keys, keyed objects). Same registration/thread rules as Add. main.cpp runs them
// once, in the order added, at the shutdown tail of every client exit path, right before plClientLoader::ShutdownEnd
// (plClient::Shutdown, then hsgResMgr::Shutdown; plClientLoader::ShutdownEnd). The frame hooks never run again after
// that point, and a static's destructor would only run from exit(), after the resource manager is gone.
void AddShutdown(HookProc proc, void* user = nullptr);
void RunShutdownHooks();
} // namespace plEmscriptenFrameHooks

#endif // plEmscriptenFrameHooks_inc
