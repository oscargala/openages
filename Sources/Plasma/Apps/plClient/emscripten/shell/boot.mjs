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

// The page logic of index.html, the player page: the deployment, the display, the browser checks, the client's
// environment and its start. index.html loads this module and calls nothing else.
//
// The page always loads its deployment document, /runtime/config.json on its own origin (config.mjs
// loadPageDeployment), and does not start without one. It reads one query parameter, renderscale= (config.mjs
// PLAYER_PARAMETERS), and logs the names of any others in one console line. It logs in through the client's login
// dialog and runs until the game quits.
//
// The page's status line (#status) is written for players. Every error has a cause code (CAUSES below): the status
// line names it, and the console gets one line "[page] error <code>: <detail>" (the detail names no URL and no
// response body), so a bug report can quote both. While nothing is wrong it says what the page is doing (STATUS
// below) until the client's first frame is on the canvas; from then on the client draws its own screens.
//
// The page and the client meet in one object, plasma. The page makes it before anything else, fills the page's members,
// seals it, and only then loads the client script. That script defines the factory createPlasmaClient; the page calls
// it with the module's arguments, the object among them, and the client reads the object as Module.plasma (its pre-js,
// Sources/Plasma/Apps/plClient/emscripten/pre.js, fills the client's members). The runtime itself (its memory,
// environment and libraries) stays inside the factory; of it the page reads only its memory's size when the client
// exits, for its exit line. The page also keeps the object as globalThis.plasma, read-only, where the test runners read
// the run's state.
// Docs/Web/Architecture.md ("The page and the module: plasma") has the same schema for a deployer:
//   config            page -> client: the client's configuration, one JSON value (a frozen object of plain data), or
//                     null (only the test harness page on the development server's own data tree). Its keys are the
//                     deployment's roles (config.mjs clientConfiguration): data, files, fonts, sound, gateway, join,
//                     serverIniText, name, status, hdPack {base, ages}, assetOrigins. The
//                     client's environment is made from this value alone (pre.js); the page
//                     has no other way to set it (the module does not export ENV)
//   state             {done, exitCode, aborted, cause, capabilityFailure, firstFrame, lines, t0}: the run's state, read
//                     by the test runners and the tests. aborted is the status line of a fatal error and cause its code;
//                     firstFrame the time of firstFrame() below (ms after t0)
//   display           the player display (null on a fixed canvas): the window report the client reads
//                     (plEmscriptenDisplay.cpp StartWindowWatch, which also takes a non-null display as the player mode),
//                     setCap and setScale
//   canvasBacking(w, h)   the client reset its display (plClient::IResizeNativeDisplayDevice): the new backing
//   setStage(w, h)    a fixed canvas's CSS fit box, null for 1:1 (the player display fills the window)
//   setFullscreenStage(active, w, h)   the fullscreen layer's page seam (plEmscriptenFullscreen.js)
//   bootFailure(missing, detail, source)   a browser requirement is missing (the preflight below; the client's WebGPU
//                     device, plWGPUPipeline.js)
//   firstFrame()      the canvas has presented the client's first frame (plWGPUPipeline.js, once per run)
//   storage           client: {login(how)}, the persistent-storage request at the player's login (pre.js)
//   fetch             the data backend's bounds {cacheBytes, writeBehindBytes, readAheadChunks}
//                     (plEmscriptenFetchBackend.js); the player page passes none (null: the defaults). The backend runs
//                     on the client's own worker, which sees only what a pre-js sets there (a test build's)
//   login             client: the open login dialog (plEmscriptenLogin.js), null while none is open
//   launcher          client: the open launcher dialog of a patcher deployment (plEmscriptenPatcher.js), null while
//                     none is open
//   input             client: the input layer's handle (plEmscriptenInput.js), null until the client attaches its
//                     input: {ready, status, error, requestPointerLock(), requestFullscreen(), dispose()}
//   fullscreen        client: the fullscreen layer (input/fullscreen.mjs attachFullscreen, through
//                     plEmscriptenFullscreen.js), null until the client sets its first display mode; the page's
//                     fullscreen button goes through it (below)
//   fullscreenStatus  client: the fullscreen layer's last status, or {ready: false, error} when it failed to attach
//   audioBackend, audioActivationError
//                     client: the native-audio build's playback (plEmscriptenNativeAudio.js): its backend's name, and
//                     why the browser refused to resume its audio context; null in every other build
//   harness           the test harness page's own member (the seam's module below), never on the player page: the
//                     client's test variables (env, which only a harness build's pre-js reads), the runner's account
//                     (credentials, which only a harness build's login takes), the client's file export (files),
//                     what a harness build's observers publish, and the runtime's own objects for the test runners'
//                     instruments (runtime) (the private test harness lists them)
//
// The test harness seam: boot(harness). The player page passes nothing. The private test harness page
// (its page and module, never part of a kit) passes an object, and its members are the only way
// test options reach this module, which reads no test parameter itself. Every function member is optional, runs once,
// and may be async; ctx is {plasma, report(code, detail), causeError(code, detail), config (the config.mjs module),
// deployment}:
//   causes                   the harness page's own cause codes, {CODE: {fatal, text}}, none of them in CAUSES
//   module                   an object: plasma.harness (above), which the harness page fills
//   deployment               an object: the harness page's own deployment rules (config.mjs validateConfiguration's
//                            extension: the test-harness game-code modes), which the deployment document is read with
//   admit(ctx)               before the deployment document is requested: may refuse (throw ctx.causeError(...))
//   start(ctx)               once the deployment is known (ctx.deployment; null when the harness runs on the
//                            development server's own data tree, which only it may): may refuse, may force a browser
//                            failure (plasma.bootFailure), writes its own profile files. Returns the start options:
//                            {args (client arguments), canvas {w, h} (a fixed canvas: the client's one display mode,
//                            instead of the player display), stage {w, h} (its CSS fit box), renderCap (the player
//                            display's cap), hdPack {base, ages} (a texture-pack directory of its own), hdPackSeed
//                            (true or false: the texture setting to write first)}
//   configure(config)        the client's configuration as the page computed it (null without a deployment); returns
//                            the one the client gets (plasma.config)
//   output(text, isError)    each line the client prints
//   beforeClient(ctx)        the last step before the client script loads

// The cause codes. fatal: the run cannot continue; the status line shows text, then the code. Not fatal: the console
// line alone (the game goes on). The texts are for players: plain words, nothing about how the page works inside.
// config.mjs gives each refused deployment document or server.ini its code (CONFIG-*, INI-*, its fail()).
const SETTINGS = 'The game could not start: this site’s game settings could not be loaded. Please try again later.';
const SERVER = 'The game could not start: the game server’s settings could not be loaded. Please try again later.';
export const CAUSES = Object.freeze({
  'BROWSER-MISSING': {
    fatal: true,
    text: 'This browser cannot start the game: it lacks a feature the game needs (listed below).',
  },
  'BROWSER-GPU': { fatal: true, text: 'This browser cannot start the game: it could not open the graphics device.' },
  'CONFIG-NONE': { fatal: true, text: 'The game could not start: this site has no game set up.' },
  'CONFIG-ORIGIN': { fatal: true, text: SETTINGS },
  'CONFIG-FETCH': {
    fatal: true,
    text: 'The game could not start: its settings could not be downloaded. Check your connection and reload this page.',
  },
  'CONFIG-HTTP': { fatal: true, text: SETTINGS },
  'CONFIG-SIZE': { fatal: true, text: SETTINGS },
  'CONFIG-JSON': { fatal: true, text: SETTINGS },
  'CONFIG-INVALID': { fatal: true, text: SETTINGS },
  'INI-FETCH': {
    fatal: true,
    text: 'The game could not start: the game server’s settings could not be downloaded. Check your connection and reload this page.',
  },
  'INI-HTTP': { fatal: true, text: SERVER },
  'INI-SIZE': { fatal: true, text: SERVER },
  'INI-PIN': { fatal: true, text: SERVER },
  'INI-INVALID': { fatal: true, text: SERVER },
  'CLIENT-LOAD': { fatal: true, text: 'The game could not be downloaded. Check your connection and reload this page.' },
  'CLIENT-ABORT': { fatal: true, text: 'The game stopped because of an error. Reload this page to try again.' },
  'CLIENT-EXIT': { fatal: true, text: 'The game closed because of an error. Reload this page to try again.' },
  'BOOT-INTERNAL': {
    fatal: true,
    text: 'The game could not start because of an unexpected problem. Reload this page to try again.',
  },
  'STORAGE-INI': { fatal: false, text: 'The first-run graphics settings could not be saved in this browser.' },
  'STORAGE-TEXTURES': {
    fatal: false,
    text: 'The high-resolution texture setting could not be read or saved in this browser.',
  },
  'DISPLAY-FULLSCREEN': { fatal: false, text: 'Full screen was refused by the browser.' },
  'SOURCE-NOTICE': { fatal: false, text: 'The link to the source code of this game could not be loaded.' },
  'SCRIPT-ERROR': { fatal: false, text: 'A script error was reported.' },
  'SCRIPT-REJECTION': { fatal: false, text: 'A script promise was rejected.' },
});
// The status line of a fatal cause.
export function statusText(code) {
  return CAUSES[code].text + ' Error code: ' + code;
}
// The status line while nothing is wrong: loading before the module runs (#status in index.html) and again from the
// client's start until its first frame, download while the client itself downloads, running once its first frame is on
// the canvas (a player page then hides the line: the client draws its own screens), left after the game's own quit.
// Only the page's and the client's events change it, never a timer: a client that never presents a frame leaves the
// loading line as it is.
export const STATUS = Object.freeze({
  loading: 'Loading the game…',
  download: 'Downloading the game…',
  running: 'The game is running.',
  left: 'You have left the game. Reload this page to play again.',
});
// The browser requirements the preflight checks (plasma.bootFailure names a missing one by its key).
export const BROWSER_REQUIREMENTS = Object.freeze(['isolation', 'suspending', 'promising', 'webgpu', 'offscreen']);
// The error carried by a failure the page itself detects.
function causeError(code, detail) {
  const e = new Error(detail);
  e.code = code;
  return e;
}
// An exception's detail for the console: its name and the start of its message, with any URL replaced.
export function detailOf(e) {
  const text = (e && e.name ? e.name + ': ' : '') + String(e && e.message !== undefined ? e.message : e);
  return text.replace(/\b[a-z][a-z0-9+.-]*:\/\/\S+/gi, '<url>').slice(0, 300);
}

export function boot(harness = null) {
  if (globalThis.plasma) throw new Error('the page has already started');
  // The cause codes of this page: the player page's, and a harness page's own (none may repeat one of CAUSES).
  if (harness && harness.causes && Object.keys(harness.causes).some((code) => code in CAUSES))
    throw new Error('a harness cause code repeats a page cause code');
  const causes = harness && harness.causes ? Object.freeze({ ...CAUSES, ...harness.causes }) : CAUSES;
  const html = document.documentElement;
  // The query parameters this page reads: none until the deployment is known, then the player parameters (config.mjs
  // playerParameters). Nothing else here reads location.search.
  let q = new URLSearchParams();
  const args = []; // the client's arguments (the module's arguments), filled once the deployment is known
  const statusEl = document.getElementById('status');
  const state = {
    done: false,
    exitCode: null,
    aborted: null,
    cause: null,
    firstFrame: null,
    lines: 0,
    t0: performance.now(),
  };
  // The page and the client's one object, plasma (see the header). The page's members are set below; the object
  // is sealed once they exist, and the client fills its own (storage, login, launcher, input, fullscreen,
  // fullscreenStatus, audioBackend, audioActivationError) when its script runs. The harness member exists on the test
  // harness page only.
  const plasma = {
    config: null,
    state,
    display: null,
    canvasBacking: null,
    setStage: null,
    setFullscreenStage: null,
    bootFailure: null,
    firstFrame: null,
    storage: null,
    fetch: null,
    login: null,
    launcher: null,
    input: null,
    fullscreen: null,
    fullscreenStatus: null,
    audioBackend: null,
    audioActivationError: null,
  };
  if (harness && harness.module) plasma.harness = harness.module;
  Object.defineProperty(globalThis, 'plasma', { value: plasma, enumerable: true });
  // An error's console line; a fatal one also ends the run and shows its status line. The first fatal cause is kept
  // (the abort and exit callbacks that follow it do not replace it), except that a missing browser requirement
  // replaces the client error it caused (override).
  function report(code, detail) {
    console.error('[page] error ' + code + ': ' + detail);
    if (!causes[code].fatal || (state.cause && !(override(code) && !override(state.cause)))) return;
    state.cause = code;
    state.aborted = causes[code].text + ' Error code: ' + code;
    state.done = true;
    statusEl.textContent = state.aborted;
    html.classList.add('plasma-ended');
  }
  const override = (code) => code.startsWith('BROWSER-');
  // Whether the player page has asked for its source notice (sourceNotice, below): once per page.
  let sourceNoticeAsked = false;
  // The start options of a harness page (the seam's start); none on the player page.
  let options = {};
  // The display: the player display, or a harness page's fixed canvas (options.canvas: the client's one display
  // mode). Set up once the deployment is known, before plClient.js transfers #canvas to the client thread.
  function setupDisplay() {
    const canvasSize = options.canvas;
    if (canvasSize) {
      const c = document.getElementById('canvas');
      c.width = canvasSize.w;
      c.height = canvasSize.h;
      c.style.width = canvasSize.w + 'px';
      c.style.height = canvasSize.h + 'px';
      c.style.maxWidth = 'none';
    }
    // The player display, every player page. The canvas fills #stage, which fills the
    // window. plasma.display reports the window to the client (plEmscriptenDisplay.cpp
    // StartWindowWatch): its CSS size, its size in device pixels (ResizeObserver device-pixel-content-box where
    // the browser has it, else CSS size x devicePixelRatio), devicePixelRatio, whether #stage is fullscreen,
    // the screen in device pixels and the render-scale cap; a change is reported 150 ms after the last one.
    // The client owns the render scale and resizes its display (plClient::ResetDisplayDevice); the page sets
    // the backing store only once, before the client takes the canvas, as the client will compute it
    // (backingFraction below, plEmscriptenDisplay.cpp DefaultFraction): round(device size x fraction).
    if (!canvasSize)
      (() => {
        // Measured (FPS): 1.5 keeps 30+ FPS full screen at devicePixelRatio 2 on the Mac mini.
        const DEFAULT_RENDER_CAP = 1.5;
        // Supersampling. The player's render-scale choice (the #render-scale control, saved in
        // this browser profile) renders the backing at that whole multiple of the device pixels; the browser's compositor
        // scales the canvas down to its CSS box. Its filter is bilinear at the device pixel centres, which at an exact 2x
        // is a 2x2 box and at fractional ratios aliases (a compositor test), so only whole factors are
        // offered. 1 = off: the cap applies.
        const RENDER_SCALES = [1, 2],
          RENDER_SCALE_KEY = 'plasma.renderScale';
        // A supersampled backing is offered only while it stays within a 4K frame: area at most
        // 3840x2160 = 8,294,400 pixels and neither side above 3840 (the long side, so a portrait window follows the same
        // rule with the sides swapped). Measured: Relto at a 3840x2160 backing runs 38 FPS, at 7680x4320
        // (2x on a 1920x1080 CSS window at devicePixelRatio 2) 10.6 FPS, below the project's 24 FPS floor.
        const SS_MAX_AREA = 3840 * 2160,
          SS_MAX_SIDE = 3840;
        // The largest backing side: WebGPU's default maxTextureDimension2D, the device's limit (the client asks for no
        // higher limit, plWGPUEnumerate.cpp); plEmscriptenDisplay.cpp kMaxBacking.
        const MAX_BACKING = 8192;
        const validScale = (v) => (RENDER_SCALES.includes(Number(v)) ? Number(v) : 0);
        // The backing a scale would make of a window of devW x devH device pixels, and whether it stays within the limit
        // (scale 1, the capped default, always does).
        const scaleBacking = (scale, devW, devH) => ({ w: Math.round(devW * scale), h: Math.round(devH * scale) });
        function scaleFits(scale, devW, devH) {
          if (!(scale > 1)) return true;
          const b = scaleBacking(scale, devW, devH);
          return b.w * b.h <= SS_MAX_AREA && Math.max(b.w, b.h) <= SS_MAX_SIDE;
        }
        let savedScale = 0;
        try {
          savedScale = validScale(localStorage.getItem(RENDER_SCALE_KEY));
        } catch (e) {}
        // renderscale= (the player parameter): this visit's override of the player's saved choice, which it leaves
        // unchanged; default the saved choice, else 1 = off.
        const scaleParam = validScale(q.get('renderscale'));
        html.classList.add('plasma-player');
        const stageDiv = document.getElementById('stage'),
          playerCanvas = document.getElementById('canvas');
        const capParam = Number(options.renderCap);
        let last = null,
          timer = 0,
          firstResolve;
        const display = (plasma.display = {
          cap: capParam > 0 ? capParam : DEFAULT_RENDER_CAP,
          scale: scaleParam || savedScale || 1,
          metrics: null,
          seq: 0,
          listeners: [],
          first: new Promise((resolve) => {
            firstResolve = resolve;
          }),
          subscribe(fn) {
            this.listeners.push(fn);
            if (this.metrics) fn(this.metrics);
          },
          // A runtime change of the cap (the FPS measurement; a later page setting), reported as any change.
          setCap(cap) {
            if (!(cap > 0)) throw new TypeError('render cap');
            this.cap = cap;
            publish();
            return this.metrics;
          },
          // The render scale (supersampling) the player chose: saved in the profile unless save is false (the harness),
          // reported as any change; the client then follows it even after a resolution the game chose
          // (plEmscriptenDisplay.cpp). The choice is the player's; the scale reported to the client (metrics.scale) is the
          // choice only while its backing fits the limit (SS_MAX_AREA, SS_MAX_SIDE), else 1 (metrics.scaleChosen keeps it).
          setScale(scale, save = true) {
            const v = validScale(scale);
            if (!v) throw new TypeError('render scale');
            this.scale = v;
            if (save)
              try {
                localStorage.setItem(RENDER_SCALE_KEY, String(v));
              } catch (e) {}
            showScale(v);
            publish();
            return this.metrics;
          },
        });
        // The backing as a fraction of the device pixels: off, min(dpr, cap) / dpr; supersampling, the largest whole
        // factor up to the scale for which neither side exceeds MAX_BACKING (at least 1, the device pixels).
        function backingFraction(m) {
          const capped = Math.min(m.dpr, m.cap) / m.dpr;
          if (!(m.scale > 1)) return capped;
          return Math.max(capped, 1, Math.floor(Math.min(m.scale, MAX_BACKING / m.devW, MAX_BACKING / m.devH)));
        }
        // The menu is one button per scale in the page's control strip (index.html #render-scale),
        // not a <select>: a select takes the keyboard focus to open its list and keeps it when the list closes unchosen, so the
        // game's keys would reach the menu instead of the canvas. aria-checked marks the scale shown.
        const scaleMenu = document.getElementById('render-scale');
        const scaleButtons = [...scaleMenu.querySelectorAll('button[data-scale]')];
        function showScale(v) {
          for (const b of scaleButtons) b.setAttribute('aria-checked', String(Number(b.dataset.scale) === v));
        }
        showScale(display.scale);
        const SCALE_TITLE = scaleMenu.title;
        // The menu follows the window. A scale whose backing would exceed the limit is disabled
        // and says why in its title (and the menu's); the menu shows the scale in effect, so a saved 2x that the window
        // outgrew reads Standard until the window fits again. Returns whether the player's choice is limited.
        function updateScaleMenu(chosen, effective, devW, devH) {
          const reasons = [];
          for (const o of scaleButtons) {
            const v = Number(o.dataset.scale),
              fits = scaleFits(v, devW, devH);
            o.disabled = !fits;
            const b = scaleBacking(v, devW, devH);
            o.title = fits
              ? ''
              : o.textContent + ' needs a smaller window on this display (would render ' + b.w + 'x' + b.h + ')';
            if (!fits) reasons.push(o.title);
          }
          showScale(effective);
          scaleMenu.title = reasons.length
            ? reasons.join('; ') + (chosen > effective ? '; your choice returns when the window fits' : '')
            : SCALE_TITLE;
          return chosen > effective;
        }
        let scaleLimited = false;
        // The limit is a function of the window's device pixels alone (not of the backing the client then sizes), so a
        // report cannot feed back into the next one; a change of the verdict is logged once, whether the window grew past
        // the limit or shrank back within it.
        function logScaleLimit(chosen, limited, devW, devH) {
          if (limited === scaleLimited) return;
          scaleLimited = limited;
          if (!limited && !(chosen > 1)) return; // the player picked Standard meanwhile: nothing to report
          const b = scaleBacking(chosen, devW, devH),
            lim = SS_MAX_SIDE + 'x' + SS_MAX_AREA / SS_MAX_SIDE;
          console.log(
            limited
              ? '[page] render scale ' +
                  chosen +
                  'x falls back to Standard: its backing would be ' +
                  b.w +
                  'x' +
                  b.h +
                  ', over the ' +
                  lim +
                  ' limit (the choice is kept; it returns when the window fits)'
              : '[page] render scale ' +
                  chosen +
                  'x available again: its backing is ' +
                  b.w +
                  'x' +
                  b.h +
                  ', within the ' +
                  lim +
                  ' limit (supersampling resumes)',
          );
        }
        // A press on a scale is the player's choice (Standard chosen while a saved 2x is limited replaces it); the choice in
        // place changes nothing.
        for (const b of scaleButtons)
          b.addEventListener('click', () => {
            const v = Number(b.dataset.scale);
            if (b.disabled || v === display.scale) return;
            display.setScale(v);
            playerCanvas.focus({ preventScroll: true });
          });
        function publish() {
          timer = 0;
          if (!last) return;
          const dpr = devicePixelRatio || 1;
          let devW = last.devW,
            devH = last.devH;
          // Use the device-pixel box only when it agrees with CSS x devicePixelRatio (headless Chromium's emulated
          // scale factor reports CSS pixels there).
          if (!(
            devW > 0 &&
            devH > 0 &&
            Math.abs(devW - last.cssW * dpr) <= 1.5 &&
            Math.abs(devH - last.cssH * dpr) <= 1.5
          )) {
            devW = Math.round(last.cssW * dpr);
            devH = Math.round(last.cssH * dpr);
          }
          // The client gets the scale in effect: the player's choice while its backing fits the limit.
          const chosen = display.scale,
            effective = scaleFits(chosen, devW, devH) ? chosen : 1;
          const m = {
            seq: ++display.seq,
            dpr,
            cssW: last.cssW,
            cssH: last.cssH,
            devW,
            devH,
            fullscreen: document.fullscreenElement === stageDiv,
            screenW: Math.round(screen.width * dpr),
            screenH: Math.round(screen.height * dpr),
            cap: display.cap,
            scale: effective,
            scaleChosen: chosen,
          };
          display.metrics = m;
          if (m.seq === 1) {
            const f = backingFraction(m);
            playerCanvas.width = Math.max(1, Math.min(MAX_BACKING, Math.round(devW * f)));
            playerCanvas.height = Math.max(1, Math.min(MAX_BACKING, Math.round(devH * f)));
            firstResolve(m);
          }
          console.log(
            '[page] player window css ' +
              m.cssW +
              'x' +
              m.cssH +
              ' device ' +
              devW +
              'x' +
              devH +
              ' dpr ' +
              dpr +
              ' fullscreen ' +
              (m.fullscreen ? 1 : 0) +
              ' screen ' +
              m.screenW +
              'x' +
              m.screenH +
              ' cap ' +
              m.cap +
              (m.scale > 1 ? ' render scale ' + m.scale : ''),
          );
          logScaleLimit(chosen, updateScaleMenu(chosen, effective, devW, devH), devW, devH);
          for (const fn of display.listeners) fn(m);
        }
        function schedule() {
          clearTimeout(timer);
          timer = setTimeout(publish, 150);
        }
        const observer = new ResizeObserver((entries) => {
          const e = entries[entries.length - 1],
            box = e.contentBoxSize?.[0],
            dev = e.devicePixelContentBoxSize?.[0];
          last = {
            cssW: box ? box.inlineSize : e.contentRect.width,
            cssH: box ? box.blockSize : e.contentRect.height,
            devW: dev ? dev.inlineSize : 0,
            devH: dev ? dev.blockSize : 0,
          };
          if (!display.metrics) publish();
          else schedule();
        });
        try {
          observer.observe(playerCanvas, { box: 'device-pixel-content-box' });
        } catch (e) {
          observer.observe(playerCanvas);
        }
        document.addEventListener('fullscreenchange', schedule);
        // devicePixelRatio changes that do not resize the content box (browsers without device-pixel-content-box)
        (function watchRatio() {
          matchMedia('(resolution: ' + devicePixelRatio + 'dppx)').addEventListener(
            'change',
            () => {
              schedule();
              watchRatio();
            },
            { once: true },
          );
        })();
        // The page's fullscreen toggle: through the client's fullscreen layer when it is attached (it keeps the
        // game's fullscreen intent, input/fullscreen.mjs), else directly. Esc leaves fullscreen.
        document.getElementById('fullscreen-button').addEventListener('click', () => {
          const api = plasma.fullscreen;
          if (api) {
            api.request().catch(() => {});
            return;
          }
          playerCanvas.focus({ preventScroll: true });
          Promise.resolve(stageDiv.requestFullscreen()).catch((e) =>
            report('DISPLAY-FULLSCREEN', 'the fullscreen request failed: ' + detailOf(e)),
          );
        });
      })();
    backing.w = canvasEl.width;
    backing.h = canvasEl.height;
    if (options.stage) plasma.setStage(options.stage.w, options.stage.h);
  }
  // CSS scaling of a fixed canvas. Input maps through the canvas's bounding rect (input/capture.mjs),
  // so it follows the scale.
  const canvasEl = document.getElementById('canvas');
  const backing = { w: canvasEl.width, h: canvasEl.height };
  let stageBox = null;
  // The fullscreen layer's page seam (its callers are the fullscreen layer's; fullscreen applies
  // to #stage): while fullscreen is active the canvas is CSS-fitted into the fullscreen box
  // instead of the stage box. CSS fit only: no backing-size change, no DPR message, stageBox
  // untouched, so leaving fullscreen restores the previous fit exactly.
  let fullscreenBox = null;
  function fitCanvas() {
    // On the player display the canvas always fills the window (CSS); only the backing changes.
    if (plasma.display) {
      console.log('[page] canvas backing ' + backing.w + 'x' + backing.h + ' (player mode, the window)');
      return;
    }
    const box = fullscreenBox || stageBox;
    if (!box) {
      canvasEl.style.width = backing.w + 'px';
      canvasEl.style.height = backing.h + 'px';
    } else {
      const s = Math.min(box.w / backing.w, box.h / backing.h);
      canvasEl.style.width = backing.w * s + 'px';
      canvasEl.style.height = backing.h * s + 'px';
    }
    canvasEl.style.maxWidth = 'none';
    console.log(
      '[page] canvas backing ' +
        backing.w +
        'x' +
        backing.h +
        ' css ' +
        canvasEl.style.width +
        ' x ' +
        canvasEl.style.height +
        (fullscreenBox
          ? ' (fullscreen ' + box.w + 'x' + box.h + ')'
          : stageBox
            ? ' (stage ' + box.w + 'x' + box.h + ')'
            : ' (1:1)'),
    );
  }
  plasma.canvasBacking = (w, h) => {
    backing.w = w;
    backing.h = h;
    fitCanvas();
  };
  plasma.setStage = (w, h) => {
    stageBox = w && h ? { w: w, h: h } : null;
    fitCanvas();
  };
  plasma.setFullscreenStage = (active, w, h) => {
    fullscreenBox = active && w && h ? { w: w, h: h } : null;
    fitCanvas();
  };
  // (the canvas fitter ends here: a fixture test runs it, from the canvasEl line, in a page of its own)
  // The client's output goes to the console; a harness page also gets each line (the seam's output).
  function output(text, isErr) {
    state.lines++;
    if (harness && harness.output) harness.output(text, isErr);
  }
  // The preflight: refuse missing browser requirements before loading client code or touching OPFS.
  const capabilityLabels = {
    isolation: 'Shared memory access (cross-origin isolation)',
    suspending: 'WebAssembly promise integration (Suspending)',
    promising: 'WebAssembly promise integration (promising)',
    webgpu: 'WebGPU graphics',
    offscreen: 'Canvas transfer to a graphics worker (OffscreenCanvas)',
  };
  plasma.bootFailure = (missing, detail = '', source = 'worker') => {
    if (state.capabilityFailure) return; // Retain the first cause across abort/exit callbacks.
    state.capabilityFailure = { missing, detail: String(detail).slice(0, 512), source };
    report(
      missing.every((key) => key in capabilityLabels) ? 'BROWSER-MISSING' : 'BROWSER-GPU',
      'browser requirements not met: ' + missing.join(', ') + (detail ? ': ' + String(detail).slice(0, 512) : ''),
    );
    document.getElementById('stage').hidden = true;
    // The control strip's settings go with the run (the texture setting, the render scale, full screen). Its source
    // notice stays (index.html #source-notice): a player whose browser cannot run the game still reaches the source
    // and the notices.
    for (const control of document.getElementById('page-controls').children)
      if (control.id !== 'source-notice') control.hidden = true;
    document.getElementById('compatibility-reason').textContent =
      detail ||
      'The following requirements are unavailable. Update your browser or check the page hosting configuration.';
    const list = document.getElementById('compatibility-missing');
    for (const key of missing) {
      const item = document.createElement('li');
      item.textContent = capabilityLabels[key] || 'WebGPU adapter or graphics device';
      list.appendChild(item);
    }
    document.getElementById('compatibility').hidden = false;
  };
  // The canvas has presented the client's first frame (plWGPUEnumerate.cpp NoteCanvasTexture, a second canvas texture
  // means the first was presented): from here the client draws its own screens, so the status line steps aside
  // (index.html plasma-presented). A run that has already ended keeps its last status line.
  plasma.firstFrame = () => {
    if (state.firstFrame !== null || state.done) return;
    state.firstFrame = performance.now() - state.t0;
    html.classList.add('plasma-presented');
    statusEl.textContent = STATUS.running;
  };
  // The page's control strip (index.html #page-controls) never takes the keyboard from the
  // canvas, as the native client's window frame takes none from the game. A press anywhere in the strip leaves the focus
  // where it was, and the control still acts on its click: the scale, the texture setting, and the fullscreen request,
  // which keeps its trusted gesture. A click on a label would focus its checkbox as it
  // passes the click on (Chromium), so the strip passes it on itself: a script click toggles the box and focuses
  // nothing. Tab stays with the game while the canvas has the focus (input/capture.mjs); a keyboard user reaches the strip
  // before that.
  {
    const strip = document.getElementById('page-controls');
    strip.addEventListener('mousedown', (e) => e.preventDefault());
    strip.addEventListener('click', (e) => {
      const label = e.target.closest('label'),
        box = label && label.control;
      if (!box || e.target === box) return;
      e.preventDefault();
      box.click();
    });
  }
  Object.seal(plasma);
  const capabilities = {
    isolation: globalThis.crossOriginIsolated === true,
    suspending: typeof globalThis.WebAssembly?.Suspending === 'function',
    promising: typeof globalThis.WebAssembly?.promising === 'function',
    webgpu: !!navigator.gpu,
    offscreen: typeof HTMLCanvasElement.prototype.transferControlToOffscreen === 'function',
  };
  const missingCapabilities = Object.keys(capabilities).filter((key) => !capabilities[key]);
  if (missingCapabilities.length) {
    plasma.bootFailure(missingCapabilities, '', 'preflight');
    // The player page asks for its source notice here too: the preflight ends the page before the deployment, after
    // which it asks otherwise (below).
    if (!harness) sourceNotice();
    return;
  }
  let deployment = null;
  // The module's arguments: the factory's moduleArg, which becomes the client's Module.
  const moduleArg = {
    plasma,
    arguments: args,
    canvas: document.getElementById('canvas'),
    print: (text) => {
      console.log(text);
      output(text, false);
    },
    printErr: (text) => {
      console.error(text);
      output(text, true);
    },
    // The client's runtime is ready and main() starts: the download is over and the game loads. The canvas stays black
    // until the client presents its first frame (plasma.firstFrame), so the status line stays; the client's patcher and
    // login dialogs open over it.
    onRuntimeInitialized: () => {
      if (!state.cause) statusEl.textContent = STATUS.loading;
    },
    // The client has exited. The exit line also names the size of its wasm memory, which only grows, so it is the
    // largest the client's memory was in the run. The memory is the module's wasmMemory (plClient's link options
    // export it); the module is this moduleArg, which the factory returns as the client's Module.
    onExit: (code) => {
      state.exitCode = code;
      state.done = true;
      html.classList.add('plasma-ended');
      const memoryBytes = moduleArg.wasmMemory.buffer.byteLength;
      console.log('[page] runtime exited with code ' + code + ' (wasm memory ' + memoryBytes + ' bytes)');
      if (code !== 0) report('CLIENT-EXIT', 'the client exited with code ' + code);
      else if (!state.cause) statusEl.textContent = STATUS.left;
    },
    onAbort: (what) => {
      state.done = true;
      html.classList.add('plasma-ended');
      report('CLIENT-ABORT', 'the runtime aborted: ' + String(what).slice(0, 512));
    },
  };
  globalThis.addEventListener('error', (e) =>
    report('SCRIPT-ERROR', e.message + (e.error && e.error.stack ? '\n' + e.error.stack : '')),
  );
  // A rejected promise nothing else handled; the client's start is one (the factory's promise, loadClient below).
  const rejection = (reason) =>
    report('SCRIPT-REJECTION', reason + (reason && reason.stack ? '\n' + reason.stack : ''));
  globalThis.addEventListener('unhandledrejection', (e) => rejection(e.reason));
  function loadClient() {
    const s = document.createElement('script');
    // The deployment names the build (config.mjs: build 'py2' exactly with gameCode 'server-py2'):
    // plClient-py2.js, whose loader fetches build/plClient-py2.wasm; otherwise plClient.js. A harness page on the
    // test harness server's own data tree has no deployment and keeps plClient.js (that server's --client-name
    // serves another build under that name).
    const client = deployment ? deployment.config.client : 'plClient';
    if (deployment) console.log('[page] deployment build ' + deployment.config.build + ': build/' + client + '.js');
    s.src = 'build/' + client + '.js';
    // The script only defines the factory; the client starts when the page calls it.
    s.onload = () => globalThis.createPlasmaClient(moduleArg).catch(rejection);
    s.onerror = () => report('CLIENT-LOAD', 'build/' + client + '.js did not load');
    statusEl.textContent = STATUS.download;
    document.body.appendChild(s);
  }
  let composeInitialGraphicsIni = null; // config.mjs initialGraphicsIni, with a deployment
  // The optional verified original high-res textures. Off by default;
  // the player's choice is the profile file Init/webtextures.txt ("HDTextures 1" or "HDTextures 0"), which the client
  // reads each time it loads an Age (plWGPUTexturePack::PrepareRoom), so a change takes effect at the next Age. The
  // setting is offered only when the deployment has packs (textures.hdPack) or a harness page names its own; while it
  // is on the page credits the art (CC BY-NC-SA 4.0). A harness page may write the setting first (options.hdPackSeed).
  async function texturePackSetting() {
    const offered = deployment && deployment.config.hdPack ? true : !!options.hdPack;
    const box = document.getElementById('texpack'),
      on = document.getElementById('texpack-on');
    const credit = document.getElementById('texpack-credit');
    async function initDir() {
      return (await navigator.storage.getDirectory()).getDirectoryHandle('Init', { create: true });
    }
    async function write(value) {
      const w = await (await (await initDir()).getFileHandle('webtextures.txt', { create: true })).createWritable();
      await w.write('HDTextures ' + (value ? 1 : 0) + '\r\n');
      await w.close();
      console.log('[page] Init/webtextures.txt HDTextures ' + (value ? 1 : 0));
    }
    try {
      if (options.hdPackSeed === true || options.hdPackSeed === false) await write(options.hdPackSeed);
      let current = false;
      try {
        const text = await (await (await (await initDir()).getFileHandle('webtextures.txt')).getFile()).text();
        current = /^HDTextures[ \t]+([1-9]\d*)[ \t]*$/m.test(text);
      } catch (e) {
        if (e.name !== 'NotFoundError') throw e;
      }
      on.checked = current;
      credit.hidden = !current;
      box.hidden = !offered;
      if (offered) console.log('[page] high-res textures ' + (current ? 'on' : 'off'));
      on.addEventListener('change', () => {
        const value = on.checked;
        credit.hidden = !value;
        write(value).catch((e) =>
          report('STORAGE-TEXTURES', 'Init/webtextures.txt could not be written: ' + detailOf(e)),
        );
      });
    } catch (e) {
      report('STORAGE-TEXTURES', 'the high-res texture setting failed: ' + detailOf(e));
    }
  }
  // The player page's source notice (index.html #source-notice). The page and the client are under the
  // GNU GPL version 3; source.json, beside the page, says where their source is published
  // (web/deploy/plasma_web/provenance.py): its url names the release tag, else the release commit, else (no release
  // build) the repository.
  // The link shows that url; a site that names no repository gets the words without a link.
  // The page asks once, after its deployment, or when it ends before that: the preflight, a refused deployment
  // document or server.ini, the page's own fault. So every ending of the player page keeps the notice.
  async function sourceNotice() {
    if (sourceNoticeAsked) return;
    sourceNoticeAsked = true;
    const box = document.getElementById('source-notice'),
      link = document.getElementById('source-link');
    try {
      const response = await fetch('source.json', { cache: 'no-store' });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const doc = await response.json();
      const url = typeof doc.url === 'string' && /^https:\/\/[^\s]+$/.test(doc.url) ? doc.url : null;
      const ref = doc.tag ? 'tag ' + doc.tag : doc.commit ? 'commit ' + doc.commit : 'no release';
      if (url) {
        link.href = url;
        link.title = 'The source code of this page and its client (' + ref + ')';
      } else {
        link.hidden = true;
        document.getElementById('source-none').hidden = false;
      }
      box.hidden = false;
      console.log('[page] source code: ' + (url || 'no repository named') + ' (' + ref + ')');
    } catch (e) {
      report('SOURCE-NOTICE', 'source.json could not be read: ' + detailOf(e));
    }
  }
  (async () => {
    const config = await import('./runtime/config.mjs');
    const ctx = { plasma, report, causeError, config, deployment: null };
    // A harness page may refuse before the deployment document is requested (the seam's admit).
    if (harness && harness.admit) await harness.admit(ctx);
    // The deployment, then the parameters this page reads (config.mjs): before anything reads the query. Only a harness
    // page may run without a deployment (on the development server's own data tree).
    deployment = ctx.deployment = await config.loadPageDeployment(
      location.href,
      !!harness,
      (harness && harness.deployment) || null,
    );
    const page = config.playerParameters(location.search);
    q = page.params;
    html.dataset.plasmaMode = harness ? 'harness' : 'player';
    composeInitialGraphicsIni = config.initialGraphicsIni;
    if (!harness) {
      // (the player page's console lines carry no test word)
      console.log('[page] player page');
      sourceNotice();
      if (page.ignored.length) {
        const names = page.ignored.slice(0, 24).map((name) => name.replace(/[^\x21-\x7e]/g, '?').slice(0, 40));
        console.log(
          '[page] query parameters ignored on a player page (it reads only ' +
            config.PLAYER_PARAMETERS.join(', ') +
            '): ' +
            names.join(', ') +
            (page.ignored.length > names.length ? ' (+' + (page.ignored.length - names.length) + ')' : ''),
        );
      }
    }
    // A harness page's start options (the seam's start); it may have ended the run (a forced browser failure).
    if (harness && harness.start) options = (await harness.start(ctx)) || {};
    if (state.done) return;
    args.push(...(options.args || []));
    setupDisplay();
    console.log('[page] crossOriginIsolated=' + globalThis.crossOriginIsolated + ' args=' + JSON.stringify(args));
    // The canvas backing is set from the first window report before the client takes the canvas
    if (plasma.display) await plasma.display.first;
    // A deployment's first-run graphics.ini (config.mjs graphics.initialIni) is installed as
    // Init/graphics.ini only when the profile has none, the condition of the client's own default writer
    // (plClient::IDetectAudioVideoSettings); an existing file, the player's own settings, is never touched. Its size
    // is the canvas backing, the default mode that writer would take.
    if (deployment && deployment.config.initialGraphicsIni) {
      try {
        const root = await navigator.storage.getDirectory();
        const init = await root.getDirectoryHandle('Init', { create: true });
        let exists = true;
        try {
          await init.getFileHandle('graphics.ini');
        } catch (e) {
          if (e.name !== 'NotFoundError') throw e;
          exists = false;
        }
        if (exists) {
          console.log('[page] Init/graphics.ini exists: the deployment initial graphics.ini is not installed');
        } else {
          const text = composeInitialGraphicsIni(deployment.config.initialGraphicsIni, canvasEl.width, canvasEl.height);
          const w = await (await init.getFileHandle('graphics.ini', { create: true })).createWritable();
          await w.write(text);
          await w.close();
          console.log('[page] Init/graphics.ini installed from the deployment (' + text.length + ' chars)');
        }
      } catch (e) {
        report('STORAGE-INI', 'the initial graphics.ini install failed: ' + detailOf(e));
      }
    }
    await texturePackSetting();
    // The client's configuration (the header's config): the deployment's, as config.mjs derived it; a harness page may
    // make it its own (the seam's configure). The client's pre-js makes the client's environment from it.
    let clientConfig = deployment ? deployment.client : null;
    if (harness && harness.configure) clientConfig = harness.configure(clientConfig);
    // One JSON value: a copy through JSON (plain data only), frozen to the last level.
    const frozen = (v) => {
      if (v && typeof v === 'object') Object.values(v).forEach(frozen);
      return Object.freeze(v);
    };
    plasma.config = clientConfig === null ? null : frozen(JSON.parse(JSON.stringify(clientConfig)));
    if (harness && harness.beforeClient) await harness.beforeClient(ctx);
    loadClient();
  })().catch((e) => {
    // config.mjs names the reason a deployment document or its server.ini was refused (CONFIG-*, INI-*); the page's
    // and a harness page's own refusals carry theirs; anything else is the page's own fault.
    const code = e && typeof e.code === 'string' && causes[e.code] && causes[e.code].fatal ? e.code : 'BOOT-INTERNAL';
    report(
      code,
      code === 'BOOT-INTERNAL'
        ? 'the page bootstrap failed: ' + detailOf(e)
        : code.startsWith('CONFIG-')
          ? 'the deployment document was refused'
          : code.startsWith('INI-')
            ? 'the deployment server.ini was refused'
            : e.message,
    );
    // The player page keeps its source notice on this ending too (the page may end before it asked, above).
    if (!harness) sourceNotice();
  });
}

// index.html starts the page with <script type="module" src="boot.mjs" data-plasma-boot>; any other importer (a test,
// the harness page) calls boot() itself.
if (globalThis.document && document.querySelector('script[type="module"][data-plasma-boot]')?.src === import.meta.url)
  boot();
