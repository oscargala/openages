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

// Browser storage for the patcher's install directory (OPFS): the persistent-storage request at
// login and the estimate the patchers check before writing (plEmscriptenStorage.cpp). Both run on the page's main
// thread (navigator.storage.persist() exists only in a window).
addToLibrary({
  // Called on the main thread through emscripten_proxy_sync_with_ctx: writes [usage, quota] (doubles, -1 when
  // unknown) at out, then finishes the proxied call, so the calling pthread blocks only until the promise settles.
  plasma_storage_estimate_start__deps: ['emscripten_proxy_finish'],
  plasma_storage_estimate_start: (ctx, out) => {
    out >>>= 0;
    var done = (usage, quota) => {
      HEAPF64[out >> 3] = usage;
      HEAPF64[(out >> 3) + 1] = quota;
      _emscripten_proxy_finish(ctx);
    };
    var s = globalThis.navigator && navigator.storage;
    if (!s || typeof s.estimate !== 'function') { done(-1, -1); return; }
    s.estimate().then((e) => done(Number(e.usage ?? -1), Number(e.quota ?? -1)), () => done(-1, -1));
  },

  // The page's storage request when the player starts a login (the dialog's Login, or the harness env login):
  // navigator.storage.persist() (pre.js Module.plasma.storage.login). Never on page load: browsers judge persistence
  // requests by the user's engagement with the site. A harness build also logs the answer
  // (the private test harness's pre-js).
  plasma_storage_login__deps: ['$UTF8ToString'],
  plasma_storage_login__proxy: 'async',   // callers pass string literals (never freed)
  plasma_storage_login: (how) => {
    var why = UTF8ToString(how >>> 0);
    if (typeof Module.plasma?.storage?.login === 'function') Module.plasma.storage.login(why);
  },
});
