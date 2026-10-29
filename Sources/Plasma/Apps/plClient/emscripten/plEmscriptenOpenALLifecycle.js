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

// Parts of this file come from Emscripten 6.0.12 src/lib/libopenal.js, which carries this notice:
/**
 * @license
 * Copyright 2013 The Emscripten Authors
 * SPDX-License-Identifier: MIT
 */

// Emscripten 6.0.12: retain gesture recovery for live contexts and release
// sources/WebAudio resources when a noncurrent AL context is destroyed.
addToLibrary({
  // Override the SDK's one-shot helper, rather than adding another listener
  // alongside one which could resume an explicitly paused native device. Each
  // context the SDK creates (alcCreateContext) joins the page's audio activation
  // (plEmscriptenAudioActivation.js), the rule the native output shares.
  $autoResumeAudioContext__deps: ['$plasmaAudioActivation', '$AL'],
  $autoResumeAudioContext: (ac) =>
    plasmaAudioActivation.add(ac, {
      // alcDevicePauseSOFT deliberately removes the scheduler. A gesture must
      // not undo a native device pause; only alcDeviceResumeSOFT does that.
      paused: () => Object.values(AL.contexts).some((ctx) => ctx.audioCtx === ac && ctx.interval === null),
      failed: (error) => console.warn('[OpenAL] gesture resume failed:', error.name),
    }),

  alcDestroyContext__deps: ['$AL', 'alSourcei'],
  alcDestroyContext__proxy: 'sync',
  alcDestroyContext: (contextId) => {
    var ctx = AL.contexts[contextId];
    if (!ctx || AL.currentCtx === ctx) {
      AL.alcErr = 0xA002; // ALC_INVALID_CONTEXT, preserving the SDK's current-context rule.
      return;
    }
    clearInterval(ctx.interval);
    ctx.interval = null;
    // Reuse original source stopping and AL_BUFFER detachment bookkeeping.
    // Device buffers can be shared by other contexts: do not delete them here.
    // This main-thread proxy is synchronous; restore the peer's current context.
    var previous = AL.currentCtx;
    try {
      AL.currentCtx = ctx;
      for (var id of Object.keys(ctx.sources)) {
        var src = ctx.sources[id];
        AL.setSourceState(src, 0x1014); // AL_STOPPED
        _alSourcei(Number(id), 0x1009, 0); // AL_BUFFER
        src.gain.disconnect();
        src.panner?.disconnect();
        delete ctx.sources[id];
        AL.freeIds.push(Number(id));
      }
    } finally {
      AL.currentCtx = previous;
    }
    ctx.gain.disconnect();
    AL.deviceRefCounts[ctx.deviceId]--;
    delete AL.contexts[contextId];
    AL.freeIds.push(contextId);
    if (ctx.audioCtx.state !== 'closed')
      ctx.audioCtx.close().catch(error => console.warn('[OpenAL] context close failed:', error.name));
  },
});
