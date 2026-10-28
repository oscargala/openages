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

// plEmscriptenStorage.h. The JS half is plEmscriptenStorage.js (main thread).
#include "plEmscriptenStorage.h"

#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

#include <atomic>
#include <cstdio>

extern "C" void plasma_storage_estimate_start(em_proxying_ctx* ctx, double* out);
extern "C" void plasma_storage_login(const char* how);

namespace
{
std::atomic<unsigned> sWriteFailures{0};
}

bool plEmscriptenStorage::Estimate(double& usage, double& quota)
{
    double out[2] = {-1, -1};
    if (emscripten_is_main_browser_thread())
        return false; // the estimate settles on the main thread, which must not wait for itself
    emscripten_proxy_sync_with_ctx(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(),
                                   [](em_proxying_ctx* ctx, void* arg) {
        plasma_storage_estimate_start(ctx, static_cast<double*>(arg));
    }, out);
    usage = out[0];
    quota = out[1];
    return usage >= 0 && quota >= 0;
}

void plEmscriptenStorage::LoginStarted(const char* how)
{
    plasma_storage_login(how); // proxied to the main thread (plEmscriptenStorage.js), asynchronous
}

void plEmscriptenStorage::NoteWriteFailure(const char* path, int error)
{
    ++sWriteFailures;
    std::printf("storage: write to %s failed (errno %d): the partial file was removed\n", path, error);
    std::fflush(stdout);
}

unsigned plEmscriptenStorage::WriteFailures()
{
    return sWriteFailures.load();
}

// No log line of its own: the patcher logs what it does with the answer (pfPatcher.cpp
// IHandleManifestDownload, "Browser storage: ... available ..." in the patcher's log).
int64_t plEmscriptenStorage::Available()
{
    double     usage = -1, quota = -1;
    const bool known = plEmscriptenStorage::Estimate(usage, quota);
    return known ? (quota > usage ? int64_t(quota - usage) : 0) : -1;
}
