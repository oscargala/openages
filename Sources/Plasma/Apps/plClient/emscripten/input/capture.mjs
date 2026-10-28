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

// Browser event collection only. Engine dispatch belongs to the client pthread.
const editingCodes = new Set([
  'Enter',
  'NumpadEnter',
  'Escape',
  'Backspace',
  'Tab',
  'Space',
  'Minus',
  'Equal',
  'BracketLeft',
  'BracketRight',
  'Backslash',
  'Semicolon',
  'Quote',
  'Backquote',
  'Comma',
  'Period',
  'Slash',
  'CapsLock',
  'PrintScreen',
  'ScrollLock',
  'Pause',
  'Insert',
  'Home',
  'PageUp',
  'Delete',
  'End',
  'PageDown',
  'ArrowRight',
  'ArrowLeft',
  'ArrowDown',
  'ArrowUp',
  'NumpadDivide',
  'NumpadMultiply',
  'NumpadSubtract',
  'NumpadAdd',
  'NumpadDecimal',
  'ShiftLeft',
  'ShiftRight',
  'ControlLeft',
  'ControlRight',
  'AltLeft',
  'AltRight',
]);
export function supportedCode(code) {
  return /^(Key[A-Z]|Digit[0-9]|Numpad[0-9]|F([1-9]|1[0-2]))$/.test(code) || editingCodes.has(code);
}
function printable(text) {
  return [...text]
    .filter((c) => {
      const n = c.codePointAt(0);
      return n >= 32 && !(n >= 127 && n <= 159) && !(n >= 0xd800 && n <= 0xdfff);
    })
    .join('');
}
function modifiers(event) {
  return {
    shift: !!event.shiftKey,
    ctrl: !!event.ctrlKey,
    alt: !!event.altKey,
    caps: !!event.getModifierState?.('CapsLock'),
  };
}

// `send` receives ordered, structured-cloneable packets. It must not call the
// engine on the browser thread. No keyboard listeners are attached to the page's
// forms: account/password fields and browser commands stay outside this capture.
export function attachInputCapture(canvas, send) {
  if (!canvas || typeof send !== 'function') throw new TypeError('Canvas and packet sink required');
  const doc = canvas.ownerDocument,
    win = doc.defaultView;
  const listeners = [],
    keys = new Set(),
    buttons = new Set();
  const oldTabindex = canvas.getAttribute('tabindex');
  if (oldTabindex === null) canvas.tabIndex = 0;
  let composing = false,
    disposed = false,
    automaticUnlock = false;
  let focused = doc.activeElement === canvas && !doc.hidden;
  let locked = doc.pointerLockElement === canvas;
  function listen(target, type, handler, options) {
    target.addEventListener(type, handler, options);
    listeners.push(() => target.removeEventListener(type, handler, options));
  }
  function snapshot(event) {
    const state = modifiers(event);
    // Both physical sides share one engine key. Retain the held side even when
    // a browser's synthesized key-up clears the aggregate event modifier flag.
    state.shift ||= keys.has('ShiftLeft') || keys.has('ShiftRight');
    state.ctrl ||= keys.has('ControlLeft') || keys.has('ControlRight');
    state.alt ||= keys.has('AltLeft') || keys.has('AltRight');
    return state;
  }
  function emit(event) {
    if (!disposed) send(event);
  }
  function reset(reason) {
    keys.clear();
    buttons.clear();
    emit({ type: 'reset', reason });
  }
  function focus(active, reason) {
    if (!active) reset(reason);
    if (focused !== active) {
      focused = active;
      emit({ type: 'focus', active });
    }
  }
  function position(event) {
    const rect = canvas.getBoundingClientRect();
    if (!(rect.width > 0 && rect.height > 0)) return null;
    return {
      x: (event.clientX - rect.left) / rect.width,
      y: (event.clientY - rect.top) / rect.height,
      dx: (event.movementX || 0) / rect.width,
      dy: (event.movementY || 0) / rect.height,
      relative: locked,
    };
  }
  function move(event) {
    const p = position(event);
    if (p) emit({ type: 'move', ...p });
  }
  function key(event, down) {
    if (!focused || composing || event.isComposing || (down && event.metaKey)) return;
    const known = supportedCode(event.code);
    if (known && (down || keys.has(event.code))) {
      if (down) keys.add(event.code);
      else keys.delete(event.code);
      emit({ type: 'key', code: event.code, down, repeat: down && !!event.repeat, ...snapshot(event) });
      // Let Escape release browser pointer lock/fullscreen. The engine also sees it.
      if (event.code !== 'Escape') event.preventDefault();
    }
    if (
      down &&
      (!event.ctrlKey || event.getModifierState?.('AltGraph')) &&
      !event.metaKey &&
      [...event.key].length === 1
    ) {
      const text = printable(event.key);
      if (text) {
        emit({ type: 'text', code: known ? event.code : '', text, repeat: !!event.repeat, ...snapshot(event) });
        event.preventDefault();
      }
    }
  }
  listen(canvas, 'keydown', (e) => key(e, true));
  listen(canvas, 'keyup', (e) => key(e, false));
  listen(canvas, 'focus', () => focus(!doc.hidden, 'focus'));
  listen(canvas, 'blur', () => focus(false, 'blur'));
  listen(win, 'blur', () => focus(false, 'window-blur'));
  listen(win, 'focus', () => focus(doc.activeElement === canvas && !doc.hidden, 'window-focus'));
  listen(doc, 'visibilitychange', () => focus(!doc.hidden && doc.activeElement === canvas, 'visibility'));
  listen(win, 'pagehide', () => focus(false, 'pagehide'));
  listen(canvas, 'compositionstart', () => {
    reset('composition');
    composing = true;
  });
  listen(canvas, 'compositionend', (event) => {
    composing = false;
    if (!focused) return;
    const text = printable(event.data || '');
    if (text) emit({ type: 'text', code: '', text, repeat: false, ...snapshot(event) });
  });
  listen(canvas, 'mousedown', (event) => {
    if (event.button < 0 || event.button > 2) return;
    canvas.focus({ preventScroll: true });
    if (!focused) return;
    move(event);
    buttons.add(event.button);
    emit({ type: 'button', button: event.button, down: true, clicks: event.detail || 1 });
    event.preventDefault();
  });
  // A release outside the canvas must reach the game (native window capture).
  listen(win, 'mouseup', (event) => {
    if (!buttons.has(event.button)) return;
    move(event);
    buttons.delete(event.button);
    emit({ type: 'button', button: event.button, down: false, clicks: event.detail || 1 });
    event.preventDefault();
  });
  listen(win, 'mousemove', (event) => {
    if (focused && (locked || buttons.size || event.target === canvas)) move(event);
  });
  listen(
    canvas,
    'wheel',
    (event) => {
      if (!focused || !event.deltaY) return;
      const p = position(event);
      if (!p) return;
      // DOM Y is positive downward; Plasma/native wheel Y is positive upward.
      // Retain magnitude and DOM units so the native adapter can apply its policy.
      emit({ type: 'wheel', ...p, delta: -event.deltaY, mode: event.deltaMode });
      event.preventDefault();
    },
    { passive: false },
  );
  listen(canvas, 'contextmenu', (event) => event.preventDefault());
  listen(canvas, 'auxclick', (event) => event.preventDefault());
  listen(doc, 'pointerlockchange', () => {
    const next = doc.pointerLockElement === canvas;
    if (locked === next) return;
    locked = next;
    if (!locked && !automaticUnlock) reset('pointer-unlock');
    automaticUnlock = false;
    emit({ type: 'pointer-lock', active: locked });
  });
  listen(doc, 'pointerlockerror', () => {
    automaticUnlock = false;
  });
  listen(doc, 'fullscreenchange', () =>
    emit({
      type: 'fullscreen',
      active: doc.fullscreenElement === canvas || !!doc.fullscreenElement?.contains?.(canvas),
    }),
  );
  // Start clean even if the engine is attaching to an already focused canvas.
  emit({ type: 'focus', active: focused });
  return {
    // Call these only from explicit UI gestures; Escape remains the browser exit.
    requestPointerLock() {
      canvas.focus({ preventScroll: true });
      return canvas.requestPointerLock();
    },
    // Leaving engine-requested look mode should preserve other held controls.
    releasePointerLock() {
      if (doc.pointerLockElement !== canvas) return;
      automaticUnlock = true;
      try {
        return doc.exitPointerLock();
      } catch (e) {
        automaticUnlock = false;
        throw e;
      }
    },
    requestFullscreen() {
      canvas.focus({ preventScroll: true });
      return canvas.requestFullscreen();
    },
    dispose() {
      if (disposed) return;
      reset('dispose');
      emit({ type: 'focus', active: false });
      disposed = true;
      for (const remove of listeners) remove();
      if (doc.pointerLockElement === canvas) doc.exitPointerLock();
      if (oldTabindex === null) canvas.removeAttribute('tabindex');
    },
  };
}
