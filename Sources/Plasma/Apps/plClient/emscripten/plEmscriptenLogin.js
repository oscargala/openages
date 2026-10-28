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

// The page half of the login dialog. plEmscriptenLogin.cpp calls each function here on the page thread and waits for
// it; the dialog's handlers wake the client thread through plEmscriptenLoginActed.
addToLibrary({
  // IDD_URULOGIN_MAIN (win32/res/plClient.rc) and the Mac's PLSLoginWindowController.xib as a modal page dialog: the
  // "Login" title (UruLoginDialogProc), the banner (IDB_BANNER; the Mac's banner.png), the status box with the shard's
  // status text over the product string (IDC_STATUS_TEXT "Welcome to URU", IDC_PRODUCTSTRING), "Account name:",
  // "Password:", "Remember Password:", "Need an account?", and Login (the default button) / Quit. Login is enabled
  // only while the account name is not empty (UruLoginDialogProc). The status text is fetched at once and every 30 s
  // while the dialog is open, at most 255 bytes, and a failed request shows its error text (UruLoginDialogProc's
  // status thread, UPDATE_STATUSMSG_SECONDS, CurlCallback). The busy box is IDD_AUTHENTICATING with its Cancel.
  // Typed values stay in the page until plasmaLoginTake copies them into the client thread's buffers; nothing is
  // logged.
  // Styled as the game, not as a web form: the game's dark panels, the banner on top, and, once the client hands them
  // over (plasmaLoginArt), the game's own cursors and linking-book frames from resource.dat. The dialog never waits
  // for that art: without it the CSS theme alone is drawn.
  // The open dialog is Module.plasma.login, null while none is open (the page and module's one object, boot.mjs).
  plasmaLoginShow__deps: ['$UTF8ToString', '$lengthBytesUTF8', '$stringToUTF8', 'plEmscriptenLoginActed'],
  plasmaLoginShow: (username, remember, focus, statusUrl, signupUrl, product, fakePass) => {
    const doc = globalThis.document;
    if (Module.plasma.login)
      Module.plasma.login.close();
    if (doc.pointerLockElement && doc.exitPointerLock)
      doc.exitPointerLock();
    const make = (tag, className, css, text) => {
      const e = doc.createElement(tag);
      if (className) e.className = className;
      if (css) e.style.cssText = css;
      if (text !== undefined) e.textContent = text;
      return e;
    };
    const status = UTF8ToString(statusUrl), signup = UTF8ToString(signupUrl), fake = UTF8ToString(fakePass);
    const overlay = make('div', 'plasma-login-overlay');
    const theme = make('style', '', '',
        '.plasma-login-overlay{--pl-ink:#e8dec6;--pl-dim:#a3967a;--pl-gold:#c8a65a;--pl-edge:#5c4c2e;' +
        '--pl-panel:rgba(9,8,6,0.94);--pl-field:#14110c;position:fixed;inset:0;z-index:2147483646;display:flex;' +
        'align-items:center;justify-content:center;padding:16px;box-sizing:border-box;overflow:auto;' +
        'background:radial-gradient(ellipse at 50% 45%,rgba(30,25,16,0.86) 0%,rgba(0,0,0,0.97) 72%);}' +
        '.plasma-login-overlay *{box-sizing:border-box;}' +
        '.plasma-login{position:relative;width:min(440px,100%);margin:auto;background:var(--pl-panel);color:var(--pl-ink);' +
        'border:1px solid var(--pl-edge);border-radius:3px;box-shadow:0 0 0 1px #000,0 18px 54px rgba(0,0,0,0.85);' +
        'padding:10px 12px 14px;font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;}' +
        '.plasma-login-title{font-size:12px;font-weight:600;letter-spacing:0.18em;text-transform:uppercase;' +
        'color:var(--pl-gold);margin:0 0 8px;}' +
        '.plasma-login-banner{display:block;width:100%;aspect-ratio:427/56;border:1px solid #000;outline:1px solid ' +
        'var(--pl-edge);background:#0d0b08;margin:0 0 12px;object-fit:cover;}' +
        '.plasma-login-group{border:1px solid var(--pl-edge);border-radius:2px;background:rgba(255,240,200,0.025);' +
        'padding:10px 12px;margin-bottom:12px;}' +
        '.plasma-login-status{min-height:2.9em;white-space:pre-wrap;overflow-wrap:anywhere;max-height:6em;overflow:auto;}' +
        '.plasma-login-product{text-align:right;font-size:12px;color:var(--pl-dim);margin-top:4px;overflow-wrap:anywhere;}' +
        '.plasma-login-fields{display:grid;grid-template-columns:auto minmax(0,1fr);gap:8px 10px;align-items:center;}' +
        '.plasma-login-fields label{color:var(--pl-ink);}' +
        '.plasma-login-overlay input[type=text],.plasma-login-overlay input[type=password]{font:inherit;min-width:0;' +
        'padding:4px 6px;border:1px solid var(--pl-edge);border-radius:2px;background:var(--pl-field);color:var(--pl-ink);' +
        'caret-color:var(--pl-gold);}' +
        '.plasma-login-overlay input:focus-visible,.plasma-login-overlay button:focus-visible{outline:1px solid ' +
        'var(--pl-gold);outline-offset:1px;}' +
        '.plasma-login-overlay input[type=checkbox]{justify-self:start;width:16px;height:16px;margin:0;' +
        'accent-color:var(--pl-gold);}' +
        '.plasma-login-signup{grid-column:1 / span 2;justify-self:center;font:inherit;background:none;border:none;' +
        'color:var(--pl-gold);padding:2px 6px;}' +
        '.plasma-login-signup:enabled:hover{text-decoration:underline;}' +
        '.plasma-login-row{display:flex;justify-content:center;gap:12px;}' +
        '.plasma-login-button{min-width:96px;padding:6px 14px;border-radius:2px;font:inherit;color:var(--pl-ink);' +
        'background:linear-gradient(#262016,#14110b);border:1px solid var(--pl-edge);}' +
        '.plasma-login-button.primary{border-color:var(--pl-gold);}' +
        '.plasma-login-button:enabled:hover{border-color:#e2c47c;color:#fff4d8;}' +
        '.plasma-login-overlay button:enabled{cursor:pointer;}' +
        '.plasma-login-overlay button:disabled{opacity:0.45;cursor:default;}' +
        '.plasma-login-busy{position:absolute;inset:0;display:none;align-items:center;justify-content:center;' +
        'background:rgba(0,0,0,0.9);border-radius:3px;}' +
        '.plasma-login-busy-box{text-align:center;padding:12px 20px 16px;max-width:100%;}' +
        '.plasma-login-book{display:block;width:128px;height:128px;margin:0 auto 6px;}' +
        '.plasma-login-busy-text{margin-bottom:12px;color:var(--pl-ink);}');
    const form = make('form', 'plasma-login');
    form.noValidate = true;
    form.setAttribute('role', 'dialog');
    form.setAttribute('aria-modal', 'true');
    form.setAttribute('aria-labelledby', 'plasma-login-title');
    const title = make('div', 'plasma-login-title', '', 'Login');
    title.id = 'plasma-login-title';
    const banner = make('img', 'plasma-login-banner');
    banner.alt = '';
    const statusBox = make('div', 'plasma-login-group');
    const statusText = make('div', 'plasma-login-status', '', 'Welcome to URU');
    statusText.setAttribute('aria-live', 'polite');
    const productText = make('div', 'plasma-login-product', '', UTF8ToString(product));
    statusBox.append(statusText, productText);
    const fields = make('div', 'plasma-login-group plasma-login-fields');
    const field = (label, id, type, autocomplete) => {
      const l = make('label', '', '', label);
      l.htmlFor = id;
      const input = make('input');
      input.id = id;
      input.type = type;
      input.autocomplete = autocomplete;
      input.spellcheck = false;
      input.setAttribute('autocapitalize', 'none');
      fields.append(l, input);
      return input;
    };
    const account = field('Account name:', 'plasma-login-account', 'text', 'username');
    const password = field('Password:', 'plasma-login-password', 'password', 'current-password');
    const rememberLabel = make('label', '', '', 'Remember Password:');
    rememberLabel.htmlFor = 'plasma-login-remember';
    const rememberBox = make('input');
    rememberBox.id = 'plasma-login-remember';
    rememberBox.type = 'checkbox';
    const signupButton = make('button', 'plasma-login-signup', '', 'Need an account?');
    signupButton.type = 'button';
    signupButton.disabled = !signup;
    fields.append(rememberLabel, rememberBox, signupButton);
    const row = make('div', 'plasma-login-row');
    const button = (label, primary) => {
      const b = make('button', primary ? 'plasma-login-button primary' : 'plasma-login-button', '', label);
      b.dataset.plasmaLoginButton = label;
      return b;
    };
    const login = button('Login', true);
    login.type = 'submit';
    const quit = button('Quit', false);
    quit.type = 'button';
    row.append(login, quit);
    const busy = make('div', 'plasma-login-busy', 'display:none;');
    const busyBox = make('div', 'plasma-login-busy-box');
    busyBox.setAttribute('role', 'alertdialog');
    busyBox.setAttribute('aria-modal', 'true');
    const book = make('img', 'plasma-login-book', 'display:none;');
    book.alt = '';
    const busyText = make('div', 'plasma-login-busy-text', '', 'Logging in to URU. Please wait...');
    const cancel = button('Cancel', false);
    cancel.type = 'button';
    busyBox.append(book, busyText, cancel);
    busy.append(busyBox);
    form.append(title, banner, statusBox, fields, row, busy);
    overlay.append(theme, form);

    account.value = UTF8ToString(username);
    rememberBox.checked = !!remember;
    if (remember)
      password.value = fake;
    const L = { pending: null, locked: false, busy: false, timer: 0, previous: null, urls: [], frames: [], cursors: {},
                bookTimer: 0, bookFrame: 0 };
    const controls = [account, password, rememberBox, signupButton, login, quit];
    const update = () => { login.disabled = L.locked || account.value === ''; };
    const lock = (on) => {
      L.locked = on;
      for (const c of controls) if (c !== signupButton) c.disabled = on;
      signupButton.disabled = on || !signup;
      update();
    };
    // The art handed over by the client (plasmaLoginArt): the banner, then resource.dat's cursors and linking-book
    // frames. The cursors are the game's (the CURSOR_* names of plInputDevice.h) with their hot spot at the centre: the
    // cursor plate is positioned by its centre (plMouseDevice::SetCursorX/Y). The buttons take the poised and clicked
    // cursors over them and the disabled one when disabled; the text fields keep the text caret.
    const cursorRule = make('style');
    overlay.append(cursorRule);
    const cursorCss = () => {
      const c = L.cursors, at = (n, fallback) => (c[n] ? 'url("' + c[n] + '") 24 24,' : '') + fallback;
      if (!c['cursor_up.png'])
        return;
      cursorRule.textContent =
          '.plasma-login-overlay,.plasma-login-overlay *{cursor:' + at('cursor_up.png', 'default') + ';}' +
          '.plasma-login-overlay input[type=text],.plasma-login-overlay input[type=password]{cursor:text;}' +
          '.plasma-login-overlay button:enabled:hover,.plasma-login-overlay input[type=checkbox]:enabled:hover{cursor:' +
          at('cursor_poised.png', 'pointer') + ';}' +
          '.plasma-login-overlay button:enabled:active,.plasma-login-overlay input[type=checkbox]:enabled:active{cursor:' +
          at('cursor_clicked.png', 'pointer') + ';}' +
          '.plasma-login-overlay button:disabled,.plasma-login-overlay input:disabled{cursor:' +
          at('cursor_disabled.png', 'default') + ';}';
    };
    L.art = (name, blob) => {
      const url = URL.createObjectURL(blob);
      L.urls.push(url);
      if (name === 'banner') {
        banner.src = url;
        return;
      }
      const frame = /^xLoading_Linking[.]([0-9]+)[.]png$/.exec(name);
      if (frame) {
        L.frames[Number(frame[1])] = url;
        return;
      }
      L.cursors[name] = url;
      cursorCss();
    };
    // The busy box turns the linking book as the loading screen does: the next frame once more than 30 ms have passed
    // since the last one (plDTProgressMgr::Draw).
    const bookTick = (now) => {
      L.bookTimer = 0;
      if (!L.busy)
        return;
      const frames = L.frames.filter(Boolean);
      if (frames.length && now - L.bookLast > 30) {
        L.bookFrame = (L.bookFrame + 1) % frames.length;
        book.src = frames[L.bookFrame];
        book.style.display = 'block';
        L.bookLast = now;
      }
      L.bookTimer = requestAnimationFrame(bookTick);
    };
    account.addEventListener('input', update);
    form.addEventListener('submit', (event) => {
      event.preventDefault();
      if (L.locked || account.value === '')
        return;
      L.previous = doc.activeElement;
      L.pending = { event: 1, username: account.value, password: password.value, remember: rememberBox.checked };
      // the player starts a login: request persistent storage now, inside their gesture
      if (typeof Module.plasma.storage?.login === 'function') Module.plasma.storage.login('dialog');
      lock(true);
      _plEmscriptenLoginActed();      // wake the client thread (WaitForPlayer)
    });
    quit.addEventListener('click', () => {
      if (L.locked)
        return;
      L.pending = { event: 2 };
      lock(true);
      _plEmscriptenLoginActed();
    });
    cancel.addEventListener('click', () => {
      if (L.busy) {
        L.pending = { event: 3 };
        _plEmscriptenLoginActed();
      }
    });
    // Escape is the dialog cancel: IsDialogMessage turns it into WM_COMMAND IDCANCEL (BN_CLICKED). In IDD_URULOGIN_MAIN
    // IDCANCEL is the Quit button (UruLoginDialogProc); in IDD_AUTHENTICATING it is Cancel (AuthDialogProc). A
    // message box on top (hsMessageBox_Emscripten.js) keeps its own keys: its overlay is not inside this one.
    overlay.addEventListener('keydown', (event) => {
      if (event.key !== 'Escape' || event.repeat)
        return;
      event.preventDefault();
      event.stopPropagation();
      if (L.busy)
        cancel.click();
      else if (!L.locked)
        quit.click();
    });
    signupButton.addEventListener('click', () => {
      if (signup)
        globalThis.open(signup, '_blank', 'noopener,noreferrer');
    });
    const keepFocus = (event) => {
      if (!overlay.contains(event.target) &&
          !(event.target.closest && event.target.closest('.plasma-msgbox-overlay')))
        (L.busy ? cancel : account).focus();
    };
    doc.addEventListener('focusin', keepFocus, true);
    if (status) {
      const poll = async () => {
        try {
          const response = await fetch(status, { cache: 'no-store', credentials: 'omit' });
          const bytes = new Uint8Array(await response.arrayBuffer()).subarray(0, 255);
          if (overlay.isConnected) statusText.textContent = new TextDecoder().decode(bytes);
        } catch (e) {
          if (overlay.isConnected) statusText.textContent = String((e && e.message) || e);
        }
      };
      poll();
      L.timer = setInterval(poll, 30000);
    }
    L.take = (userBuf, passBuf, size) => {
      const p = L.pending;
      if (!p)
        return 0;
      L.pending = null;
      if (p.event !== 1)
        return p.event;
      if (lengthBytesUTF8(p.username) >= size || lengthBytesUTF8(p.password) >= size) {
        lock(false);
        return 0;
      }
      stringToUTF8(p.username, userBuf, size);
      stringToUTF8(p.password, passBuf, size);
      return p.event | (p.remember ? 256 : 0);
    };
    L.setBusy = (on) => {
      L.busy = !!on;
      busy.style.display = on ? 'flex' : 'none';
      if (on) {
        L.bookLast = -1e9;
        if (!L.bookTimer)
          L.bookTimer = requestAnimationFrame(bookTick);
        cancel.focus();
      } else {
        lock(false);
        const back = (L.previous && L.previous.isConnected && !L.previous.disabled) ? L.previous : login;
        back.focus();
      }
    };
    L.close = () => {
      clearInterval(L.timer);
      if (L.bookTimer)
        cancelAnimationFrame(L.bookTimer);
      doc.removeEventListener('focusin', keepFocus, true);
      overlay.remove();
      for (const url of L.urls)
        URL.revokeObjectURL(url);
      Module.plasma.login = null;
      if (Module.canvas && Module.canvas.focus)
        Module.canvas.focus({ preventScroll: true });
    };
    Module.plasma.login = L;
    (doc.fullscreenElement || doc.body).append(overlay);
    update();
    [account, password, login][focus === 2 && login.disabled ? 0 : focus].focus();
  },

  // One image for the open dialog (L.art): its PNG bytes are copied out of the module's memory.
  plasmaLoginArt__deps: ['$UTF8ToString'],
  plasmaLoginArt: (name, data, size) => {
    data >>>= 0;
    const L = Module.plasma.login;
    if (L && L.art)
      L.art(UTF8ToString(name), new Blob([HEAPU8.slice(data, data + size)], { type: 'image/png' }));
  },

  // The player's last action (L.take), for plEmscriptenLogin::Take: 0 none, 1 Login (the account and password are
  // copied into the two buffers of `size` bytes; 256 is added when Remember Password is checked), 2 Quit, 3 Cancel.
  plasmaLoginTake: (userBuf, passBuf, size) => {
    const L = Module.plasma.login;
    return L ? L.take(userBuf, passBuf, size) : 0;
  },

  // The busy box (IDD_AUTHENTICATING) shown or hidden.
  plasmaLoginBusy: (busy) => {
    if (Module.plasma.login)
      Module.plasma.login.setBusy(busy);
  },

  // Closes the open dialog.
  plasmaLoginClose: () => {
    if (Module.plasma.login)
      Module.plasma.login.close();
  },
});
