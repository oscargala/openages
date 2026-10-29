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

// See plEmscriptenFrameLoop.h.

#include "plEmscriptenFrameLoop.h"

#include <emscripten/emscripten.h>
#include <pthread.h>

#ifdef PLASMA_PIPELINE_WGPU
#include "pfWGPUPipeline/plWGPUPipeline.h"
#endif

extern "C" {
// plEmscriptenFrameLoop.js
void   plFrameLoop_start(void (*tick)());
double plFrameLoop_waitFrame(); // suspends (JSPI) until the next animation frame
double plFrameLoop_waitTask();  // suspends (JSPI) to the end of the current task only
#ifdef PLASMA_TEST_HOOKS
int plFrameLoop_skipped(); // animation frames skipped so far while a tick was suspended
#endif
}

namespace
{
void (*s_tick)() = nullptr;
pthread_t s_owner{};
bool      s_started = false;
// true while the tick's own stack is executing; false while it is suspended (then any wasm
// that runs on this thread is a callback from the event loop, which must not suspend)
bool      s_inTick = false;
#ifdef PLASMA_PIPELINE_WGPU
double s_tickEndMs = -1.0; // when the last tick returned
#endif

void Tick()
{
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnTickBegin();
#endif
#ifdef PLASMA_PIPELINE_WGPU
    // The wait between two ticks is a yield too, for the pipeline's build pacing (Yield below).
    if (s_tickEndMs >= 0.0)
        plWGPUDevice::NoteYield(emscripten_get_now() - s_tickEndMs);
#endif
    s_inTick = true;
    s_tick();
    s_inTick = false;
#ifdef PLASMA_PIPELINE_WGPU
    s_tickEndMs = emscripten_get_now();
#endif
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnTickEnd();
#endif
}
} // namespace

namespace plEmscriptenFrameLoop
{
void Start(void (*tick)())
{
    if (s_started)
        return;
    s_started = true;
    s_tick = tick;
    s_owner = pthread_self();
#ifdef PLASMA_PIPELINE_WGPU
    // The pipeline's GPU readback waits suspend the tick through this loop (plEmscriptenSuspendBegin/End below).
    // Registered before the first tick: the pipeline is made inside one.
    plWGPUPipeline::SetGPUWaitProcs(plEmscriptenSuspendBegin, plEmscriptenSuspendEnd);
#endif
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnStart();
#endif
    plFrameLoop_start(Tick);
}

void Stop()
{
    emscripten_cancel_main_loop();
}

#ifdef PLASMA_TEST_HOOKS
void LogStats(double nowMs)
{
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnLoopReport(nowMs, plFrameLoop_skipped());
}
#endif
} // namespace plEmscriptenFrameLoop

namespace
{
// how names what ends the task, for a harness build's observer only. Returns the suspension in ms, -1 if refused.
double Yield(bool nextFrame, [[maybe_unused]] const char* how)
{
    if (!pthread_equal(pthread_self(), s_owner) || !s_inTick) {
#ifdef PLASMA_TEST_HOOKS
        if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
            observer->OnYieldRefused();
#endif
        return -1.0;
    }
    // The yield suspends inside a Python frame as anywhere else (plEmscriptenFrameLoop.h).
    s_inTick = false;
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnSuspendBegin(how, true);
#endif
    const double waited = nextFrame ? plFrameLoop_waitFrame() : plFrameLoop_waitTask();
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnSuspendEnd();
#endif
    s_inTick = true;
#ifdef PLASMA_PIPELINE_WGPU
    // The pipeline's build pacing: a browser that compiles render pipelines on its GPU process's command stream holds
    // this resume until the builds issued before it are compiled (plWGPUDevice::PumpPipelineBuilds).
    plWGPUDevice::NoteYield(waited);
#endif
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnYield(waited);
#endif
    return waited;
}
} // namespace

double plEmscriptenPresentYield()
{
    return Yield(false, "a present-yield");
}

double plEmscriptenFrameYield()
{
    return Yield(true, "a frame-yield");
}

bool plEmscriptenSuspendBegin([[maybe_unused]] const char* how)
{
    // Yield's suspension; the caller waits (see plEmscriptenFrameLoop.h).
    if (!pthread_equal(pthread_self(), s_owner) || !s_inTick) {
#ifdef PLASMA_TEST_HOOKS
        if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
            observer->OnGpuWaitRefused();
#endif
        return false;
    }
    s_inTick = false;
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnSuspendBegin(how, false);
#endif
    return true;
}

void plEmscriptenSuspendEnd()
{
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnSuspendEnd();
#endif
    s_inTick = true;
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenFrameLoopObserver* observer = plEmscriptenFrameLoopObserver::Get())
        observer->OnGpuWait();
#endif
}
