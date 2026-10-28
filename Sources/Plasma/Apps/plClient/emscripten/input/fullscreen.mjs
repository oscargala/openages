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

// Page-side window intent only. Engine modes/backing size/DPR belong to the client.
const attached = new WeakMap();
let nextStyle = 0;

export function attachFullscreen(canvas, stage, setStage, onStatus = () => {}) {
  if (!canvas?.ownerDocument || !stage || stage === canvas || !stage.contains(canvas) || typeof setStage !== 'function')
    throw new TypeError('Canvas, canvas wrapper and CSS-fit adapter required');
  if (attached.has(canvas)) return attached.get(canvas);
  const doc = canvas.ownerDocument,
    win = doc.defaultView,
    listeners = [];
  const oldAttribute = stage.getAttribute('data-plasma-fullscreen');
  const marker = 'plasma-fs-' + ++nextStyle,
    style = doc.createElement('style');
  stage.setAttribute('data-plasma-fullscreen', marker);
  style.textContent = `[data-plasma-fullscreen="${marker}"]:fullscreen {display:flex!important;align-items:center!important;justify-content:center!important;background:#000!important;padding:0!important;border:0!important;}
[data-plasma-fullscreen="${marker}"]:fullscreen canvas {margin:0!important;flex:none!important;}`;
  doc.head.appendChild(style);
  let disposed = false,
    desired = false,
    pending = false,
    operation = null,
    mode = null;
  let sequence = 0,
    active = doc.fullscreenElement === stage,
    fit = null,
    scheduled = false;
  const counts = { requests: 0, rejected: 0, entries: 0, exits: 0, errorEvents: 0 };
  let error = null;
  const owns = () => doc.fullscreenElement === stage;
  function state() {
    return {
      ready: !disposed,
      disposed,
      supported: typeof stage.requestFullscreen === 'function' && !!doc.fullscreenEnabled,
      sequence,
      mode,
      desiredFullscreen: desired,
      pending,
      inFlight: operation?.kind || null,
      fullscreen: owns(),
      error,
      ...counts,
    };
  }
  function publish() {
    onStatus(state());
  }
  function listen(target, name, handler) {
    target.addEventListener(name, handler);
    listeners.push(() => target.removeEventListener(name, handler));
  }
  function fitStage() {
    if (owns()) {
      const w = stage.clientWidth,
        h = stage.clientHeight;
      if (w > 0 && h > 0 && (!fit || fit.w !== w || fit.h !== h)) {
        setStage(true, w, h);
        fit = { w, h };
      }
    } else if (fit) {
      setStage(false, 0, 0);
      fit = null;
    }
  }
  function record(e, token) {
    counts.rejected++;
    error = { name: String(e?.name || 'Error'), sequence: token };
  }
  function sync() {
    if (disposed) return;
    const next = owns();
    if (next !== active) {
      if (next) {
        counts.entries++;
        pending = false;
      } else {
        counts.exits++;
      } // Preserve only an explicit newer intent queued during exit.
      active = next;
    }
    fitStage();
    publish();
    if (next && !desired && !operation) exit().catch(() => {});
  }
  function finish() {
    operation = null;
    if (disposed) {
      // Do not tear down a replacement controller on the same canvas.
      if (!attached.has(canvas) && owns()) Promise.resolve(doc.exitFullscreen()).catch(() => {});
      return;
    }
    sync();
  }
  function exit() {
    if (operation) return operation.promise;
    if (!owns()) return Promise.resolve();
    const token = sequence;
    let raw;
    try {
      raw = doc.exitFullscreen();
    } catch (e) {
      record(e, token);
      publish();
      return Promise.reject(e);
    }
    const promise = Promise.resolve(raw)
      .catch((e) => {
        record(e, token);
        throw e;
      })
      .finally(finish);
    operation = { kind: 'exit', promise };
    publish();
    return promise;
  }
  function enter() {
    if (disposed) return Promise.reject(new DOMException('Fullscreen layer disposed', 'AbortError'));
    if (operation) return operation.promise;
    if (owns()) {
      pending = false;
      sync();
      return Promise.resolve();
    }
    canvas.focus({ preventScroll: true });
    const token = sequence;
    counts.requests++;
    pending = false;
    let raw;
    try {
      raw = stage.requestFullscreen();
    } catch (e) {
      record(e, token);
      pending = desired;
      publish();
      return Promise.reject(e);
    }
    const promise = Promise.resolve(raw)
      .catch((e) => {
        record(e, token);
        if (!disposed && token === sequence) pending = desired;
        throw e;
      })
      .finally(finish);
    operation = { kind: 'enter', promise };
    publish();
    return promise;
  }
  function gesture(event) {
    if (
      disposed ||
      !pending ||
      operation ||
      scheduled ||
      event.target !== canvas ||
      !event.isTrusted ||
      event.isComposing ||
      event.repeat
    )
      return;
    if (
      event.type === 'keydown' &&
      (event.code === 'Escape' ||
        event.metaKey ||
        event.ctrlKey ||
        event.altKey ||
        /^(Shift|Control|Alt|Meta)/.test(event.code))
    )
      return;
    if (event.type === 'mousedown' && event.button !== 0) return;
    if (doc.activeElement !== canvas) return;
    scheduled = true;
    // This listener runs on document during bubbling, after canvas input/relock
    // listeners regardless of registration order. A microtask on an earlier canvas
    // listener alone is insufficient: browsers can checkpoint between listeners.
    // No import, timer or awaited API may consume the live gesture here.
    queueMicrotask(() =>
      queueMicrotask(() => {
        scheduled = false;
        if (!disposed && pending && !operation && win.navigator.userActivation?.isActive) enter().catch(() => {});
      }),
    );
  }
  const api = {
    setMode(width, height, windowed, devicePixelRatio) {
      if (disposed) return;
      if (
        !Number.isInteger(width) ||
        width <= 0 ||
        !Number.isInteger(height) ||
        height <= 0 ||
        typeof windowed !== 'boolean' ||
        !Number.isFinite(devicePixelRatio) ||
        devicePixelRatio <= 0
      )
        throw new TypeError('Invalid native display mode');
      mode = { width, height, windowed, devicePixelRatio };
      sequence++;
      error = null;
      desired = !windowed;
      pending = desired && (!owns() || operation?.kind === 'exit');
      publish();
      if (!desired && owns() && !operation) exit().catch(() => {});
      else sync();
    },
    // Explicit input API retains browser-window intent without rewriting engine preferences.
    request() {
      if (disposed) return Promise.reject(new DOMException('Fullscreen layer disposed', 'AbortError'));
      sequence++;
      desired = true;
      pending = !owns() || operation?.kind === 'exit';
      error = null;
      return enter();
    },
    state,
    dispose() {
      if (disposed) return;
      disposed = true;
      desired = false;
      pending = false;
      for (const remove of listeners) remove();
      if (fit) {
        setStage(false, 0, 0);
        fit = null;
      }
      style.remove();
      if (stage.getAttribute('data-plasma-fullscreen') === marker) {
        if (oldAttribute === null) stage.removeAttribute('data-plasma-fullscreen');
        else stage.setAttribute('data-plasma-fullscreen', oldAttribute);
      }
      attached.delete(canvas);
      if (owns() && !operation)
        Promise.resolve(doc.exitFullscreen()).catch((e) => {
          record(e, sequence);
          publish();
        });
      publish();
    },
  };
  attached.set(canvas, api);
  listen(doc, 'mousedown', gesture);
  listen(doc, 'keydown', gesture);
  listen(doc, 'pointerup', (e) => {
    if (e.pointerType !== 'mouse') gesture(e);
  });
  listen(doc, 'fullscreenchange', sync);
  listen(doc, 'fullscreenerror', () => {
    counts.errorEvents++;
    publish();
  });
  listen(win, 'resize', () => {
    if (!disposed) {
      fitStage();
      publish();
    }
  });
  publish();
  return api;
}
