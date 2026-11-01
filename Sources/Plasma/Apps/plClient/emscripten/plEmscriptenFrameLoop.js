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

// JS half of plEmscriptenFrameLoop.cpp (runs in the client thread's worker).
addToLibrary({
  // Emscripten's main loop with requestAnimationFrame timing (emscripten_set_main_loop with fps 0, its runtime
  // keepalive included), but each iteration calls the tick through WebAssembly.promising, so that the tick may suspend
  // (JSPI): a plain main-loop callback is no promising entry, and suspending in it traps ("SuspendError: trying to
  // suspend without WebAssembly.promising"). No wasm frame sits between this iteration and the suspended stack, so the
  // shadow stack pointer stays below the suspended frames meanwhile. Iterations skip while a tick is suspended
  // (plFrameLoop_skipped).
  //
  // An uncaught exception or trap that ends a tick ends the loop for good, reported once (handleException): the client,
  // its shutdown above all, never runs again on the state the fault left (ticking on would run MainLoop after a fault
  // in ClientFinish(), then a second ClientFinish() on the half-destroyed heap). The native client never resumes after
  // a fault either: plCustomUnhandledExceptionFilter (installed by wWinMain in win32/winmain.cpp) reports the crash and
  // returns EXCEPTION_EXECUTE_HANDLER, so the process ends and wWinMain's one shutdown runs at most once.
  $plFrameLoopSkipped: 0,
  plFrameLoop_skipped__deps: ['$plFrameLoopSkipped'],
  plFrameLoop_skipped: () => plFrameLoopSkipped,
  plFrameLoop_start__deps: ['$setMainLoop', '$getWasmTableEntry', '$handleException', '$plFrameLoopSkipped',
                            'emscripten_cancel_main_loop'],
  plFrameLoop_start: (tick) => {
    var promisingTick = WebAssembly.promising(getWasmTableEntry(tick));
    var busy = false;
    var faulted = false;
    setMainLoop(() => {
      if (faulted)
        return;
      if (busy) {
        plFrameLoopSkipped++;
        return;
      }
      busy = true;
      promisingTick().then(() => { busy = false; }, (e) => {
        busy = false;
        // exit() inside a tick ends the promise with 'unwind' / ExitStatus
        if (e === 'unwind' || (e && e.name === 'ExitStatus')) return;
        faulted = true;
        _emscripten_cancel_main_loop();
        err('FRAME LOOP STOPPED: a tick ended with an uncaught exception; no further tick runs ' +
            '(the client is not re-entered): ' + e);
        handleException(e);
      });
    }, 0, false);
  },

  // The suspension: a Promise that resolves at this worker's next animation frame. Returns
  // the milliseconds waited.
  plFrameLoop_waitFrame__async: true,
  plFrameLoop_waitFrame: () => new Promise((resolve) => {
    var t = performance.now();
    requestAnimationFrame(() => resolve(performance.now() - t));
  }),

  // The other suspension: end the task only (a MessageChannel message, not clamped like
  // setTimeout), resuming as soon as the event loop has run what is queued. The canvas texture
  // is presented at the task end either way; this does not wait for the display.
  $plFrameLoopChannel: null,
  plFrameLoop_waitTask__async: true,
  plFrameLoop_waitTask__deps: ['$plFrameLoopChannel'],
  plFrameLoop_waitTask: () => new Promise((resolve) => {
    var t = performance.now();
    if (!plFrameLoopChannel) {
      plFrameLoopChannel = new MessageChannel();
      plFrameLoopChannel.pending = [];
      plFrameLoopChannel.port1.onmessage = () => plFrameLoopChannel.pending.shift()();
    }
    plFrameLoopChannel.pending.push(() => resolve(performance.now() - t));
    plFrameLoopChannel.port2.postMessage(0);
  }),
});
