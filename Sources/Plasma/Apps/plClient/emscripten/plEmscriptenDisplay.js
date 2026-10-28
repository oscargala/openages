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

// The page half of plEmscriptenDisplay.cpp. The client calls these from its own thread; each runs on the page's main
// thread, where the window and the page's object Module.plasma (the page's boot.mjs) are. A pointer argument ('p' in
// the signature) arrives as an unsigned address, so it is divided with >>>: this build addresses 4 GB.
addToLibrary({
  // plClient::IResizeNativeDisplayDevice: the new backing, for the page's CSS fit of the canvas
  // (Module.plasma.canvasBacking). The client does not wait for the page.
  plDisplayCanvasBacking__proxy: 'async',
  plDisplayCanvasBacking__sig: 'vii',
  plDisplayCanvasBacking: (width, height) => {
    if (Module.plasma?.canvasBacking)
      Module.plasma.canvasBacking(width, height);
  },

  // The devicePixelRatio watch of a fixed canvas: a matchMedia query for the current ratio, re-armed at each change,
  // writes the new ratio to shared[1] (its float bits) and then adds 1 to the sequence in shared[0], which the client
  // thread polls (plEmscriptenDisplay.cpp PollScale). The client does not wait for the page.
  plDisplayWatchScale__proxy: 'async',
  plDisplayWatchScale__sig: 'vp',
  plDisplayWatchScale: (shared) => {
    var addr = shared >>> 2;
    var f32 = new Float32Array(1);
    var i32 = new Int32Array(f32.buffer);
    function publish() {
      f32[0] = devicePixelRatio;
      Atomics.store(HEAP32, addr + 1, i32[0]);
      Atomics.add(HEAP32, addr, 1);
    }
    function arm() {
      var mq = matchMedia('(resolution: ' + devicePixelRatio + 'dppx)');
      mq.addEventListener('change', function () { publish(); arm(); }, { once: true });
    }
    arm();
  },

  // Whether the page has a player display (Module.plasma.display, boot.mjs), not a fixed canvas.
  plDisplayPlayerMode__proxy: 'sync',
  plDisplayPlayerMode__sig: 'i',
  plDisplayPlayerMode: () => {
    return Module.plasma?.display ? 1 : 0;
  },

  // The player display's window reports, written to the client's memory under a sequence lock: the sequence at
  // seqAddr turns odd, the ten doubles at fieldsAddr are written (plEmscriptenDisplay.cpp WindowField), and the
  // sequence turns even. The client thread reads them (ReadWindow). Synchronous: the page measured the window before it
  // loaded the client (boot.mjs), so the display's subscribe writes the first report before this returns.
  plDisplayWatchWindow__proxy: 'sync',
  plDisplayWatchWindow__sig: 'vpp',
  plDisplayWatchWindow: (seqAddr, fieldsAddr) => {
    var d = Module.plasma?.display;
    if (!d) return;
    var seq = seqAddr >>> 2;
    var v = fieldsAddr >>> 3;
    d.subscribe(function (m) {
      Atomics.add(HEAP32, seq, 1);
      HEAPF64[v + 0] = m.dpr; HEAPF64[v + 1] = m.cssW; HEAPF64[v + 2] = m.cssH;
      HEAPF64[v + 3] = m.devW; HEAPF64[v + 4] = m.devH; HEAPF64[v + 5] = m.fullscreen ? 1 : 0;
      HEAPF64[v + 6] = m.screenW; HEAPF64[v + 7] = m.screenH; HEAPF64[v + 8] = m.cap;
      HEAPF64[v + 9] = m.scale > 1 ? m.scale : 1;
      Atomics.add(HEAP32, seq, 1);
    });
  },
});
