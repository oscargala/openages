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

// DOM on the browser main thread, bounded queue drained by the client message-pump hook.
addToLibrary({
  $plInputWorker: {receiver: null, port: null, lastStatus: '', failed: false},
  plInputInit__deps: ['$plInputWorker', 'plInputAttachMain', 'pthread_self'],
  plInputInit: () => {
    // Install this BEFORE main-side dynamic imports and transfer. No Emscripten cmd field.
    self.addEventListener('message', function receive({data}) {
      if (!data?.plasmaInputPort || plInputWorker.port) return;
      self.removeEventListener('message', receive);
      plInputWorker.port = data.plasmaInputPort;
      import(data.transportURL).then(({createInputPortReceiver}) => {
        plInputWorker.receiver = createInputPortReceiver(plInputWorker.port);
      }).catch(e => { plInputWorker.failed = true; err('[input] worker attach failed: ' + e); });
    });
    _plInputAttachMain(_pthread_self());
  },
  plInputAttachMain__proxy: 'sync',
  plInputAttachMain__deps: ['$PThread'],
  plInputAttachMain: (thread) => {
    // The input layer's page handle is Module.plasma.input (the page's boot.mjs declares it).
    var plasma = Module['plasma'];
    if (plasma.input) return;
    var api = plasma.input = {ready: false, status: null, error: null};
    var base = new URL('../input/', scriptDirectory);
    Promise.all([import(new URL('capture.mjs', base).href), import(new URL('transport.mjs', base).href)])
      .then(([{attachInputCapture}, {createInputPortSender}]) => {
        var worker = PThread.pthreads[thread];
        if (!worker) throw new Error('Client pthread is unavailable');
        var channel = new MessageChannel();
        var sender = createInputPortSender(channel.port1, {
          schedule: requestAnimationFrame.bind(globalThis), cancel: cancelAnimationFrame.bind(globalThis)
        });
        channel.port1.addEventListener('message', ({data}) => {
          if (data?.channel !== 'plasma-input-status') return;
          api.status = data;
          api.ready = !!data.ready;
          // Recenter is requested by the engine, rather than inferred from a mouse binding.
          if (data.recenter && !api.recenter && document.activeElement === Module['canvas'] &&
              navigator.userActivation?.isActive && !document.pointerLockElement)
            api.requestPointerLock(true).catch(() => {});
          if (!data.recenter && api.automaticLock && document.pointerLockElement === Module['canvas']) {
            api.automaticLock = false;
            capture.releasePointerLock();
          }
          api.recenter = !!data.recenter;
        });
        channel.port1.start();
        worker.postMessage({plasmaInputPort: channel.port2,
          transportURL: new URL('transport.mjs', base).href}, [channel.port2]);
        var capture = attachInputCapture(Module['canvas'], sender.send);
        api.requestPointerLock = (automatic = false) => {
          api.automaticLock = automatic;
          return Promise.resolve().then(() => capture.requestPointerLock())
            .catch(e => { api.pointerLockError = e.name; throw e; });
        };
        api.requestFullscreen = () => plasma.fullscreen
          ? plasma.fullscreen.request()
          : Promise.reject(new DOMException('Fullscreen layer not ready','InvalidStateError'));
        api.dispose = () => { capture.dispose(); sender.dispose(); channel.port1.close(); api.ready = false; };
        // Request lock on a new user gesture if a prior recenter request lacked activation.
        var relock = () => {
          if (api.recenter && !document.pointerLockElement) api.requestPointerLock(true).catch(() => {});
        };
        Module['canvas'].addEventListener('mousedown', relock);
        var dispose = api.dispose;
        api.dispose = () => { Module['canvas'].removeEventListener('mousedown', relock); dispose(); };
        var onExit = Module['onExit'];
        Module['onExit'] = code => { api.dispose(); if (onExit) onExit(code); };
      }).catch(e => { api.error = String(e); err('[input] browser attach failed: ' + e); });
  },
  plInputDrain__deps: ['$plInputWorker', '$stringToNewUTF8', 'free', 'plInputKey',
    'plInputMove', 'plInputButton', 'plInputWheel', 'plInputFocus', 'plInputReset', 'plInputPointerLock'],
  plInputDrain: () => {
    if (!plInputWorker.receiver) return;
    for (var e of plInputWorker.receiver.drain()) {
      if (e.type === 'key' || e.type === 'text') {
        var code = stringToNewUTF8(typeof e.code === 'string' ? e.code : '');
        var mods = (e.shift ? 1 : 0) | (e.ctrl ? 2 : 0) | (e.alt ? 4 : 0) | (e.caps ? 8 : 0);
        try {
          if (e.type === 'key') _plInputKey(code, !!e.down, !!e.repeat, mods, 0);
          else if (typeof e.text === 'string')
            for (var c of e.text) _plInputKey(code, 1, !!e.repeat, mods, c.codePointAt(0));
        } finally { _free(code); }
      } else if (e.type === 'move') _plInputMove(e.x, e.y, e.dx, e.dy, !!e.relative);
      else if (e.type === 'button') _plInputButton(e.button, !!e.down, e.clicks);
      else if (e.type === 'wheel') _plInputWheel(e.x, e.y, e.delta);
      else if (e.type === 'focus') _plInputFocus(!!e.active);
      else if (e.type === 'reset') _plInputReset();
      else if (e.type === 'pointer-lock') _plInputPointerLock(!!e.active);
    }
  },
  plInputStatus__deps: ['$plInputWorker'],
  plInputStatus: (ready, active, recenter, buttons, caps, x, y, width, height, clientThread) => {
    if (!plInputWorker.port) return;
    var data = {channel: 'plasma-input-status', ready, active, recenter, buttons, caps,
      x, y, width, height, clientThread};
    var serial = JSON.stringify(data);
    if (serial === plInputWorker.lastStatus) return;
    plInputWorker.lastStatus = serial;
    plInputWorker.port.postMessage(data);
  },
});
