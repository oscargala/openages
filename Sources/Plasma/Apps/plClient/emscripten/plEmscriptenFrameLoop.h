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

// The browser frame loop of plClient.
//
// The client thread (main()'s PROXY_TO_PTHREAD pthread) runs one tick per requestAnimationFrame of its worker, the
// browser analogue of the macOS client's CVDisplayLink-driven runLoop (Mac-Cocoa/main.mm, setupRunLoop/runLoop). What
// the thread draws reaches the canvas only when it returns to its event loop, so the loop never blocks between frames.
//
// The module is built with -sJSPI. Each tick is entered through WebAssembly.promising, so code deep inside a tick can
// suspend until the next animation frame and resume (plEmscriptenPresentYield): that is how the places where the native
// client presents frames inside one long call (the progress manager's draws during a load, the intro movie loop) reach
// the screen. While a tick is suspended, later animation frames do not start another tick. Requires a browser with JS
// Promise Integration (WebAssembly.Suspending).
#ifndef plEmscriptenFrameLoop_inc
#define plEmscriptenFrameLoop_inc

namespace plEmscriptenFrameLoop
{
// Starts the loop on the calling thread (the client thread): tick() runs once per animation frame of this worker,
// through WebAssembly.promising. Returns at once; the loop holds the thread alive until Stop(). Call once.
void Start(void (*tick)());

// Stops scheduling ticks (call from a tick, before exiting).
void Stop();

#ifdef PLASMA_TEST_HOOKS
// The client loop's 1 Hz report in a harness build: hands the observer below the time
// (emscripten_get_now() ms since main() started) and the animation frames the loop has skipped so far while a
// tick was suspended, for its statistics line.
void LogStats(double nowMs);
#endif
} // namespace plEmscriptenFrameLoop

// The browser has no present call: the canvas texture drawn on the client thread is presented when the worker's task
// ends. These two calls end the task from deep inside a tick by suspending it (JSPI), at the places where the native
// client presents a frame inside one long call. Both do nothing (return at once) off the client thread, outside a tick,
// or inside a callback that runs while a tick is suspended. Inside a Python frame they suspend as anywhere else, as
// native presents there: the progress manager's callback draws and presents whatever is on the stack
// (plClient::IProgressMgrCallbackProc, IDraw, then the pipeline's EndRender, plDXPipeline::EndRender and IFlipSurface),
// and a Python script's call may be below it. That is legal only because libpython is built without PY_CALL_TRAMPOLINE
// (JSPI cannot suspend across its JS frame); plClient's CMakeLists.txt refuses a libpython that has it.
//
// plEmscriptenPresentYield: the frame drawn just before is presented and the caller resumes as soon as the event loop
// has run what was queued (a MessageChannel message, no timer clamp). For callers that pace their own draws, like the
// progress manager's draws during a load (plClient::IProgressMgrCallbackProc draws at most at kMaxFrameRate, 60 Hz):
// native presents there without waiting for the display (EndRender), so the load does not wait either.
void plEmscriptenPresentYield();

// plEmscriptenFrameYield: as above, but the caller resumes at the next animation frame of the client thread's worker.
// For loops that draw as fast as they run and rely on the present to pace them, like plClient::IPlayIntroMovie.
void plEmscriptenFrameYield();

// A wait on a JS promise from deep inside a tick (the GPU readback of
// plWGPUPipeline::CaptureScreen and CompletePendingCaptures, wgpuInstanceWaitAny, which suspends
// the tick under JSPI, reached through the procs plEmscriptenFrameLoop::Start registers with
// plWGPUPipeline::SetGPUWaitProcs): the caller brackets its wait with these two calls, so that the wait
// counts as a suspension of the tick (the task ends, animation frames skip), as a yield does. A
// GPU wait is never skipped: its caller needs the data. A Python frame on the stack is legal, as for a yield.
// plEmscriptenSuspendBegin returns false, and the caller must not wait, off the client thread, outside a running tick,
// or inside a callback that runs while the tick is suspended; it never returns false inside a tick on the client
// thread. The caller must not hold the frame's canvas texture across the wait (the task end presents it). Every true
// return is matched by one plEmscriptenSuspendEnd().
bool plEmscriptenSuspendBegin(const char* how);
void plEmscriptenSuspendEnd();

#ifdef PLASMA_TEST_HOOKS
// The frame-loop test seam: what a harness build's frame-loop instrument (testhooks/em/plTestHooksFrameLoop.cpp) sees
// of the loop's ticks, suspensions, yields and GPU waits. The product build has none of it: this class is declared only
// under PLASMA_TEST_HOOKS, and every call site is under the same switch, so the product's loop is the same code without
// the calls. In a harness build the pointer is null until plTestHooks::Install sets it, and every default does nothing.
// Every member runs on the client thread at the point its comment names, and returns without suspending.
class plEmscriptenFrameLoopObserver
{
public:
    virtual ~plEmscriptenFrameLoopObserver() = default;

    // plEmscriptenFrameLoop::Start, before the first tick is scheduled.
    virtual void OnStart() {}

    // A tick starts (a browser task begins) and returns (its task ends): tick and task statistics.
    virtual void OnTickBegin() {}
    virtual void OnTickEnd() {}

    // The tick is about to suspend. how names what ends the task ("a present-yield", "a frame-yield", or the GPU wait's
    // reason); isYield is true for the two yields, false for a GPU wait. Ends the task's statistics, logs the Python
    // frame the suspension leaves on the stack, and installs the re-entry counter for the suspension.
    virtual void OnSuspendBegin(const char* how, bool isYield) {}

    // The suspended tick resumed (a new task begins), before the yield's or the GPU wait's own call below: removes the
    // re-entry counter.
    virtual void OnSuspendEnd() {}

    // A yield resumed after waitedMs, or a GPU wait resumed.
    virtual void OnYield(double waitedMs) {}
    virtual void OnGpuWait() {}

    // A yield, or a GPU wait's plEmscriptenSuspendBegin, was refused: off the client thread, outside a running tick, or
    // in a callback that runs while the tick is suspended.
    virtual void OnYieldRefused() {}
    virtual void OnGpuWaitRefused() {}

    // The 1 Hz report (plEmscriptenFrameLoop::LogStats): the FRAME LOOP line at nowMs, with the animation frames the
    // loop has skipped so far while a tick was suspended.
    virtual void OnLoopReport(double nowMs, int framesSkipped) {}

    static plEmscriptenFrameLoopObserver* Get() { return fObserver; }
    static void                           Set(plEmscriptenFrameLoopObserver* observer) { fObserver = observer; }

private:
    static inline plEmscriptenFrameLoopObserver* fObserver = nullptr;
};
#endif // PLASMA_TEST_HOOKS

#endif // plEmscriptenFrameLoop_inc
