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

// An uncaught error inside a pthread worker reaches the
// page only as "worker sent an error! <message>", without a stack, and an uncaught C++
// exception only as "[object WebAssembly.Exception]". Report the pthread, the C++
// exception type and what() (getExceptionMessage) and the stack first, through err()
// (proxied to the page's printErr).
if (typeof ENVIRONMENT_IS_PTHREAD !== 'undefined' && ENVIRONMENT_IS_PTHREAD) {
  self.addEventListener('error', (e) => {
    var ex = e.error;
    var what = '';
    try {
      if (typeof WebAssembly.Exception !== 'undefined' && ex instanceof WebAssembly.Exception &&
          typeof getExceptionMessage === 'function') {
        what = ' C++ exception ' + getExceptionMessage(ex).join(': ');
      }
    } catch (_) { what = ' (not a C++ exception: longjmp or exit?)'; }
    var stack = ex && ex.stack ? ex.stack : String(e.message);
    var tid = (typeof _pthread_self === 'function') ? _pthread_self() : '?';
    err('[pthread ' + tid + '] uncaught:' + what + '\n' + stack);
  });
}

// The page and the module meet in one object, Module.plasma. Its schema is documented in the page's boot.mjs header
// (the page creates the object, seals it, and passes it to the factory among the module's arguments) and in
// Docs/Web/Architecture.md ("The page and the module: plasma"). This script, on the page's main thread, fills
// the client's own members and turns the page's configuration into the client's environment:
//   config    the configuration the page passes, one JSON value: null, or an object of the keys of CONFIG below.
//             The client's environment is made from it alone: the environment is not exported (no
//             EXPORTED_RUNTIME_METHODS=ENV), and the environment installed here replaces whatever the ENV object held,
//             so a page passes nothing to the client but this value.
//   storage   {login(how)}: the persistent-storage request when the player starts a login (below).
//   login     the open login dialog (plEmscriptenLogin.js), null while none is open.
//   launcher  the open launcher dialog (plEmscriptenPatcher.js), null while none is open.
//   input     the input layer's handle (plEmscriptenInput.js): {ready, status, error, requestPointerLock,
//             requestFullscreen, dispose, ...}, null until the client attaches its input.
//   fullscreen, fullscreenStatus   the fullscreen layer (input/fullscreen.mjs attachFullscreen,
//             through plEmscriptenFullscreen.js) and its last status, null until the client sets its first display
//             mode.
//   audioBackend, audioActivationError   the native-audio build's playback (plEmscriptenNativeAudio.js): the backend's
//             name and why the browser refused to resume its audio context; null in every other build.
// A page that made no Module.plasma (one that does not use boot.mjs) gets an empty one: no configuration, a fixed
// canvas, the login dialog. Nothing here adds a global of its own.
if (typeof document !== 'undefined') {
  // CONFIG: each key of the configuration and the environment variable the client reads it from. A string is passed
  // as it is, null as the empty string (the client's "not set"); hdPack is {base, ages} or null; assetOrigins is an
  // array of origins. Any other key, or a value of another kind, stops the client before main() runs: the page and the
  // client disagree about the schema, and the client must not guess.
  const CONFIG = Object.freeze({
    data: 'PLASMA_DATA_URL',                  // the game data's base URL (the served data tree, or the client package)
    files: 'PLASMA_DATA_FILES',               // 'http' (the served tree) or 'patcher' (the shard's file server)
    fonts: 'PLASMA_FONTS_URL',                // the fonts' base URL
    sound: 'PLASMA_SOUND_URL',                // the sound files' base URL (sfx), or null
    gateway: 'PLASMA_WS_PROXY',               // the WebSocket gateway
    join: 'PLASMA_WS_JOIN',                   // the gateway's admission-token endpoint, or null
    serverIniText: 'PLASMA_RUNTIME_SERVER_INI',   // the shard's server.ini, its text (verified against its pin)
    name: 'PLASMA_DEPLOYMENT_NAME',           // the deployment's name (the remembered login's key), or null
    status: 'PLASMA_RUNTIME_STATUS_URL',      // the server status relay of the login dialog, or null
    hdPack: ['PLASMA_TEXTURES_HDPACK_URL', 'PLASMA_TEXTURES_HDPACK_AGES'],   // {base, ages}: the high-res packs, or null
    assetOrigins: 'PLASMA_RUNTIME_ASSET_CACHE_ORIGINS',   // the asset origins the cache pins (a JSON array)
  });
  // The client's environment for a configuration (null, or an object of CONFIG's keys).
  const environment = (config) => {
    const env = {};
    if (config === null || config === undefined) return env;
    const fail = (what) => { throw new Error('Module.plasma.config: ' + what); };
    if (typeof config !== 'object' || Array.isArray(config)) fail('not an object');
    const text = (key, v) => { if (v !== null && typeof v !== 'string') fail(key + ' is not a string'); return v === null ? '' : v; };
    for (const [key, value] of Object.entries(config)) {
      if (!Object.hasOwn(CONFIG, key)) fail('unknown key ' + JSON.stringify(key).slice(0, 40));
      const name = CONFIG[key];
      if (key === 'hdPack') {
        const pack = value === null ? {base: '', ages: []} : value;
        if (typeof pack !== 'object' || Array.isArray(pack) || Object.keys(pack).some(k => k !== 'base' && k !== 'ages') ||
            !Array.isArray(pack.ages) || pack.ages.some(a => typeof a !== 'string')) fail('hdPack is not {base, ages}');
        env[name[0]] = text('hdPack.base', pack.base);
        env[name[1]] = pack.ages.join(',');
      } else if (key === 'assetOrigins') {
        if (!Array.isArray(value) || value.some(o => typeof o !== 'string')) fail('assetOrigins is not a list of origins');
        env[name] = JSON.stringify(value);
      } else {
        env[name] = text(key, value);
      }
    }
    return env;
  };
  if (!Module.plasma)
    Module.plasma = {config: null, state: null, display: null, storage: null, fetch: null, login: null, launcher: null,
                     input: null, fullscreen: null, fullscreenStatus: null, audioBackend: null,
                     audioActivationError: null};
  // The persistent-storage request when the player starts a login (plEmscriptenLogin.js's Login
  // submit, which is the user's own gesture, or the harness env login through plEmscriptenStorage.js), never on page
  // load: navigator.storage.persist(), once per page. Returns the request's answer as a promise (the
  // browser's grant, or 'error <name>' when it throws), null when the browser has no persist(), and undefined after
  // the first login. The client ignores it; a harness build's pre-js logs it (testhooks/em/plTestHooksStorage.js), with
  // the kind of login its callers pass ('dialog' or 'env'), which only that line reads.
  let storageRequested = false;
  Module.plasma.storage = {
    login() {
      if (storageRequested) return undefined;
      storageRequested = true;
      const s = globalThis.navigator && navigator.storage;
      if (!s || typeof s.persist !== 'function') return null;
      return (async () => {
        try { return await s.persist(); } catch (e) { return 'error ' + e.name; }
      })();
    },
  };
  // The environment is installed among the run's last preRun callbacks, the moment before the runtime starts (the C++
  // environment is read once, when the module's constructors run, right after preRun): exactly the configuration's,
  // whatever ENV held before. (A harness build's pre-js adds its own callback after this one,
  // testhooks/em/plTestHooksEnvironment.js.)
  Module.preRun = [].concat(Module.preRun || [], () => {
    const env = environment(Module.plasma.config);
    for (const name of Object.keys(ENV)) delete ENV[name];
    Object.assign(ENV, env);
  });
}
