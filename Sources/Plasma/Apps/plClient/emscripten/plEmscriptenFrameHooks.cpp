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

// See plEmscriptenFrameHooks.h.

#include "plEmscriptenFrameHooks.h"

#include <utility>
#include <vector>

namespace
{
struct Hook
{
    plEmscriptenFrameHooks::HookProc proc;
    void*                            user;
};

std::vector<Hook>& Hooks()
{
    static std::vector<Hook> hooks;
    return hooks;
}
std::vector<Hook>& MessagePumps()
{
    static std::vector<Hook> hooks;
    return hooks;
}
std::vector<Hook>& ShutdownHooks()
{
    static std::vector<Hook> hooks;
    return hooks;
}
} // namespace

namespace plEmscriptenFrameHooks
{
void Add(HookProc proc, void* user)
{
    if (proc)
        Hooks().push_back({proc, user});
}

bool Remove(HookProc proc, void* user)
{
    std::vector<Hook>& hooks = Hooks();
    for (auto it = hooks.begin(); it != hooks.end(); ++it) {
        if (it->proc == proc && it->user == user) {
            hooks.erase(it);
            return true;
        }
    }
    return false;
}

void RunAll()
{
    if (Hooks().empty())
        return;
    // a copy: a hook may add or remove hooks
    const std::vector<Hook> hooks = Hooks();
    for (const Hook& hook : hooks)
        hook.proc(hook.user);
}
void AddMessagePump(HookProc proc, void* user)
{
    if (proc)
        MessagePumps().push_back({proc, user});
}

bool RemoveMessagePump(HookProc proc, void* user)
{
    auto& hooks = MessagePumps();
    for (auto it = hooks.begin(); it != hooks.end(); ++it) {
        if (it->proc == proc && it->user == user) {
            hooks.erase(it);
            return true;
        }
    }
    return false;
}

void RunMessagePumps()
{
    const auto hooks = MessagePumps(); // callbacks may add/remove registrations
    for (const auto& hook : hooks)
        hook.proc(hook.user);
}

void AddShutdown(HookProc proc, void* user)
{
    if (proc)
        ShutdownHooks().push_back({proc, user});
}

void RunShutdownHooks()
{
    // once: taken out of the registry before the first runs
    const std::vector<Hook> hooks = std::move(ShutdownHooks());
    ShutdownHooks().clear();
    for (const Hook& hook : hooks)
        hook.proc(hook.user);
}
} // namespace plEmscriptenFrameHooks
