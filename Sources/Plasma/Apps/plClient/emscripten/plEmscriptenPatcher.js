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

// The launcher dialog (plUruLauncher.rc IDD_DIALOG: banner, IDC_STATUS_TEXT "Welcome to URU", IDC_PRODUCTSTRING,
// IDC_TEXT, IDC_PROGRESS / IDC_MARQUEE, IDC_DLSIZE, IDC_DLSPEED, Cancel) as a page dialog. plEmscriptenPatcher.cpp
// calls each function here on the page thread and waits for it. While the dialog is open it is Module.plasma.launcher,
// the page-module object's launcher member (pre.js).
addToLibrary({
  // Opens the dialog: the status text over the product string, "Connecting..." and an indeterminate bar.
  plasmaLauncherShow__deps: ['$UTF8ToString', 'plEmscriptenLauncherCancel'],
  plasmaLauncherShow: (product) => {
    const doc = globalThis.document;
    if (!doc || Module.plasma.launcher) return;
    const make = (tag, id, style, text) => {
      const e = doc.createElement(tag);
      if (id) e.id = id;
      if (style) e.style.cssText = style;
      if (text !== undefined) e.textContent = text;
      return e;
    };
    const overlay = make('div', 'plasma-launcher-overlay', 'position:fixed;inset:0;z-index:2147483645;display:flex;' +
        'align-items:center;justify-content:center;background:rgba(0,0,0,0.55);padding:16px;box-sizing:border-box;' +
        'font:13px -apple-system,"Segoe UI",sans-serif;color:#111;');
    const box = make('div', 'plasma-launcher', 'width:min(460px,100%);box-sizing:border-box;background:#f2f2f2;' +
        'border:1px solid #9a9a9a;border-radius:8px;padding:14px;box-shadow:0 10px 30px rgba(0,0,0,0.45);');
    box.setAttribute('role', 'dialog');
    box.setAttribute('aria-modal', 'true');
    box.setAttribute('aria-label', 'Patcher');
    const group = make('div', '',
                       'border:1px solid #c8c8c8;border-radius:6px;padding:10px;margin-bottom:10px;background:#fff;');
    const status = make('div', 'plasma-launcher-status', 'min-height:2.4em;', 'Welcome to URU');
    const productText = make('div', 'plasma-launcher-product', 'text-align:right;font-size:11px;color:#555;',
                             UTF8ToString(product));
    group.append(status, productText);
    const text = make('div', 'plasma-launcher-text', 'margin:0 2px 6px;white-space:nowrap;overflow:hidden;' +
                      'text-overflow:ellipsis;', 'Connecting...');
    const row = make('div', '', 'display:flex;gap:8px;align-items:center;');
    const bar = make('progress', 'plasma-launcher-progress', 'flex:1;height:12px;');
    bar.removeAttribute('value');           // indeterminate = IDC_MARQUEE
    const cancel = make('button', '', 'min-width:72px;', 'Cancel');
    cancel.type = 'button';
    cancel.dataset.plasmaLauncherButton = 'Cancel';
    row.append(bar, cancel);
    const foot = make('div', '',
                      'display:flex;justify-content:space-between;font-size:11px;color:#333;margin-top:4px;');
    const size = make('span', 'plasma-launcher-size', '', '');
    const speed = make('span', 'plasma-launcher-speed', '', '');
    foot.append(size, speed);
    box.append(group, text, row, foot);
    overlay.append(box);
    const L = {};
    // PatcherDialogProc's IDCANCEL (plUruLauncher/winmain.cpp): the button is disabled, the text reads
    // "Shutting Down...", and the launcher quits. plEmscriptenLauncherCancel wakes the client
    // thread's wait in RunLauncherStep, so the quit begins at once.
    cancel.addEventListener('click', () => {
      cancel.disabled = true;
      text.textContent = 'Shutting Down...';
      _plEmscriptenLauncherCancel();
    });
    overlay.addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.preventDefault(); cancel.click(); } });
    L.set = (t, s, sp, cur, total) => {
      if (t !== null) text.textContent = t;
      if (s !== null) size.textContent = s;
      if (sp !== null) speed.textContent = sp;
      if (total > 0) { bar.max = total; bar.value = cur; } else if (total < 0) { bar.removeAttribute('value'); }
    };
    L.close = () => { overlay.remove(); Module.plasma.launcher = null; };
    Module.plasma.launcher = L;
    (doc.fullscreenElement || doc.body).append(overlay);
    cancel.focus();
  },

  // text/size/speed: null pointers leave the field; total > 0 sets the bar, total < 0 makes it a marquee again
  plasmaLauncherSet__deps: ['$UTF8ToString'],
  plasmaLauncherSet: (text, size, speed, cur, total) => {
    const L = Module.plasma.launcher;
    if (L) L.set(text ? UTF8ToString(text) : null, size ? UTF8ToString(size) : null, speed ? UTF8ToString(speed) : null,
                 cur, total);
  },

  // Closes the open dialog.
  plasmaLauncherClose: () => {
    if (Module.plasma.launcher) Module.plasma.launcher.close();
  },
});
