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

// The page half of hsMessageBox for the browser build (hsMessageBox_Emscripten.cpp, which shows the dialog from the
// page thread and blocks or pumps the calling thread until the answer).
addToLibrary({
  // The modal page dialog, on the browser main thread. It is laid over the page (inside the fullscreen element when
  // there is one, as the Win32 client minimizes a fullscreen window around MessageBoxW: hsMinimizeClientGuard in
  // hsMessageBox_Win.cpp), takes the pointer lock back from the canvas, holds the keyboard focus on its buttons
  // (the first is the default: Enter or Space picks the focused one, Tab moves between them) and answers once
  // (hsMessageBoxEmscriptenAnswer).
  hsMessageBoxShowDialog__deps: ['$UTF8ToString', 'hsMessageBoxEmscriptenAnswer'],
  hsMessageBoxShowDialog: (ctx, request, caption, message, icon, labels, escape) => {
    const doc = globalThis.document;
    const labelList = UTF8ToString(labels).split('\n');
    const icons = { error: ['\u00d7', '#c62828'], question: ['?', '#1565c0'], exclamation: ['!', '#ef8f00'],
                    asterisk: ['i', '#1565c0'] };
    const [glyph, colour] = icons[UTF8ToString(icon)] || icons.error;
    const host = doc.fullscreenElement || doc.body;
    const previousFocus = doc.activeElement;
    if (doc.pointerLockElement && doc.exitPointerLock)
      doc.exitPointerLock();

    const overlay = doc.createElement('div');
    overlay.className = 'plasma-msgbox-overlay';
    overlay.style.cssText = 'position:fixed;inset:0;z-index:2147483647;display:flex;align-items:center;' +
        'justify-content:center;background:rgba(0,0,0,0.45);padding:16px;box-sizing:border-box;';
    const box = doc.createElement('div');
    box.className = 'plasma-msgbox';
    box.setAttribute('role', 'alertdialog');
    box.setAttribute('aria-modal', 'true');
    box.setAttribute('aria-labelledby', 'plasma-msgbox-caption');
    box.setAttribute('aria-describedby', 'plasma-msgbox-message');
    box.dataset.icon = UTF8ToString(icon);
    box.style.cssText = 'min-width:min(320px,100%);max-width:min(560px,100%);box-sizing:border-box;background:#f2f2f2;' +
        'color:#111;border-radius:8px;box-shadow:0 12px 40px rgba(0,0,0,0.5);padding:20px;' +
        'font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;';
    const head = doc.createElement('div');
    head.style.cssText = 'display:flex;align-items:flex-start;gap:14px;';
    const mark = doc.createElement('div');
    mark.textContent = glyph;
    mark.setAttribute('aria-hidden', 'true');
    mark.style.cssText = 'flex:none;width:32px;height:32px;border-radius:50%;color:#fff;font:bold 20px/32px ' +
        'system-ui,sans-serif;text-align:center;background:' + colour + ';';
    const body = doc.createElement('div');
    body.style.cssText = 'min-width:0;flex:1;';
    const title = doc.createElement('div');
    title.id = 'plasma-msgbox-caption';
    title.className = 'plasma-msgbox-caption';
    title.textContent = UTF8ToString(caption);
    title.style.cssText = 'font-weight:600;font-size:15px;margin-bottom:6px;overflow-wrap:anywhere;';
    const text = doc.createElement('div');
    text.id = 'plasma-msgbox-message';
    text.className = 'plasma-msgbox-message';
    text.textContent = UTF8ToString(message);
    text.style.cssText = 'white-space:pre-wrap;overflow-wrap:anywhere;max-height:60vh;overflow:auto;';
    body.append(title, text);
    head.append(mark, body);
    const row = doc.createElement('div');
    row.style.cssText = 'display:flex;flex-wrap:wrap;justify-content:flex-end;gap:8px;margin-top:18px;';
    let answered = false;
    const buttons = labelList.map((label, index) => {
      const button = doc.createElement('button');
      button.type = 'button';
      button.textContent = label;
      button.dataset.plasmaMsgboxButton = label;
      button.style.cssText = 'min-width:84px;padding:6px 14px;border-radius:6px;font:inherit;cursor:pointer;' +
          (index === 0 ? 'background:#1f6feb;color:#fff;border:1px solid #1f6feb;'
                       : 'background:#fff;color:#111;border:1px solid #9a9a9a;');
      button.addEventListener('click', () => answer(index));
      return button;
    });
    row.append(...buttons);
    box.append(head, row);
    overlay.append(box);

    const onKey = (event) => {
      if (event.type === 'keydown' && event.key === 'Escape') {
        event.preventDefault();
        if (escape >= 0)
          answer(escape);
      } else if (event.type === 'keydown' && event.key === 'Tab') {
        event.preventDefault();
        const at = buttons.indexOf(doc.activeElement);
        const step = event.shiftKey ? buttons.length - 1 : 1;
        buttons[((at < 0 ? 0 : at) + step) % buttons.length].focus();
      }
      event.stopPropagation();
    };
    const keepFocus = (event) => {
      if (!overlay.contains(event.target))
        buttons[0].focus();
    };
    function answer(index) {
      if (answered)
        return;
      answered = true;
      doc.removeEventListener('keydown', onKey, true);
      doc.removeEventListener('keyup', onKey, true);
      doc.removeEventListener('focusin', keepFocus, true);
      overlay.remove();
      if (previousFocus && previousFocus.isConnected && previousFocus.focus)
        previousFocus.focus();
      _hsMessageBoxEmscriptenAnswer(ctx, request, index);
    }
    doc.addEventListener('keydown', onKey, true);
    doc.addEventListener('keyup', onKey, true);
    doc.addEventListener('focusin', keepFocus, true);
    host.append(overlay);
    buttons[0].focus();
  },

  // The page thread itself cannot wait for a DOM answer (no wasm code of this client runs there: main() is on a
  // pthread); window.alert/confirm are the only synchronous boxes it has. A lone button is an alert, two or more a
  // confirm: OK picks the first button, Cancel the last.
  hsMessageBoxShowBlocking__deps: ['$UTF8ToString'],
  hsMessageBoxShowBlocking: (caption, message, count) => {
    const text = UTF8ToString(caption) + '\n\n' + UTF8ToString(message);
    if (count <= 1) {
      globalThis.alert(text);
      return 0;
    }
    return globalThis.confirm(text) ? 0 : count - 1;
  },
});
