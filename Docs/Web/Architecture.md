# Architecture of the browser client

The browser client is H-uru's Plasma engine compiled to WebAssembly with
Emscripten. The engine libraries are the same ones the native clients build;
the game runs the same code paths, reads the same game files and speaks the
same protocols to the same shards. What is new is the layer that puts the
engine into a browser:

- an application layer for Emscripten, the browser counterpart of the Windows
  and macOS `plClient` front ends;
- a WebGPU rendering pipeline, `pfWGPUPipeline`;
- a WebSocket backend for the engine's sockets, and a gateway that relays those
  WebSockets to the shard's TCP ports;
- a page that checks the browser, loads the deployment's settings and starts
  the client;
- the deployment tools, which serve the page and the game files or write a kit
  for a static host;
- a second build flavour with CPython 2.3.3 for shards whose game code is
  Python 2.3.

This document names the components and where they are. The other documents in
this folder cover building the client (`Building.md`), running it on one
machine (`Running locally.md`), deploying it for a shard (`Deploying.md`), the
Python 2.3 flavour (`Python 2.3 flavour.md`) and the HD texture pack format
(`HD texture pack format.md`).

## Upstream engine, with guarded browser branches

Most of the engine compiles unchanged. Where the browser needs different
behaviour inside an upstream file, the change is a branch under
`__EMSCRIPTEN__` (or `PLASMA_PIPELINE_WGPU` for the renderer's hooks) that
leaves the native build as it was. Examples are the patcher's use of browser
storage (`pfPatcher.cpp`), the DNS flags the platform's resolver accepts
(`pnAcDns.cpp`), the HD texture pack's hooks in `plClient.cpp` and
`plAgeLoader.cpp`, and the debug text font's lookup of its faces
(`plTextFont.cpp`). Where the platform has no equivalent at all, a whole file is
replaced: `pnAcSocketEmscripten.cpp` takes the place of `pnAcSocket.cpp`
(see Networking).

CMake builds the browser client when it configures with the Emscripten
toolchain; `PLASMA_PIPELINE_WGPU` is then on by default and adds
`pfWGPUPipeline`.

## The application layer

`Sources/Plasma/Apps/plClient/emscripten/` holds the browser's `plClient` front
end. Its parts:

| Component | Files | Native counterpart |
|---|---|---|
| Entry point and start-up | `main.cpp` | `win32/winmain.cpp` `wWinMain` |
| Frame loop | `plEmscriptenFrameLoop.{h,cpp,js}`, `plEmscriptenFrameHooks.{h,cpp}` | the macOS client's display-link run loop |
| Display | `plEmscriptenDisplay.{h,cpp}` | the native window |
| Login dialog | `plEmscriptenLogin.{h,cpp,js}` | the login windows of `winmain.cpp` and the macOS client |
| Launcher (patcher mode) | `plEmscriptenPatcher.{h,cpp,js}` | `plUruLauncher` |
| Browser storage | `plEmscriptenStorage.{h,cpp,js}` | none |
| File system | `plEmscriptenFetchBackend.js`, `plEmscriptenAssetCache.js`, `plEmscriptenAssetCatalog.js`, `plEmscriptenCwd.{h,cpp}`, `plEmscriptenStdio.cpp`, `plEmscriptenPoll.cpp` | the local disk |
| Input | `plEmscriptenInput.{cpp,js}`, `plEmscriptenKeys.h`, `plEmscriptenFullscreen.{cpp,js}`, and the page's modules in `input/` (`capture.mjs`, `transport.mjs`, `fullscreen.mjs`) | the window's input messages |
| Audio | `plEmscriptenOpenAL*.js`, `plEmscriptenNativeAudio.js` | OpenAL |
| Server settings | `plEmscriptenRuntime.{h,cpp}` | `server.ini` on disk |
| Page contract | `pre.js` | the command line |

### Threads

The client is linked with `-sPROXY_TO_PTHREAD`: `main()` runs on a pthread,
the client thread, and the browser's main thread stays free for the work that
must run there (file fetches, audio, screen queries, thread creation). The
start-up order is `wWinMain`'s: arguments, `general.ini`, debug init,
`server.ini`, the display helper and `plClientLoader::Init`, network start-up,
login, then `plClient::StartClient` and the main loop. Before that, the browser
mounts its file systems.

Pthreads need `SharedArrayBuffer`, which a browser grants only to a page that
is cross-origin isolated. The origin server, the static kit's headers and the
Worker (below) therefore send `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`.

### The frame loop

From `plClientLoader::Init` on, start-up and `plClient::MainLoop` run as states
of the frame loop: one tick per `requestAnimationFrame` of the client thread.
What the thread draws reaches the canvas only when the thread returns to its
event loop, so the loop never blocks between frames.

The native client presents frames from inside long calls: the progress bar
during an Age load, the intro movie. The browser client is linked with
`-sJSPI` (JavaScript Promise Integration) for these. Each tick is entered
through `WebAssembly.promising`, and a long call can suspend until the next
animation frame and resume (`plEmscriptenPresentYield`). Such a suspension can
happen inside a Python frame, which is legal only because the embedded CPython
is built without call trampolines; the build refuses a CPython 3 library that
has them.

Platform pieces that run once per frame (input, audio) register a frame hook
(`plEmscriptenFrameHooks`) instead of editing the loop.

## Rendering: pfWGPUPipeline

`Sources/Plasma/FeatureLib/pfWGPUPipeline/` is the WebGPU pipeline. Its
structure follows `pfMetalPipeline`: the same base class, members and split of
work between pipeline and device. Its behaviour follows the D3D9 pipeline,
`plDXPipeline`, which is the reference for how the game's content looks.

- **The emulated D3D9 device.** Spans, materials, lights, fog, bump mapping,
  projected lights, shadows, avatar texture composition, vertex and pixel
  shader programs, render requests, gamma and screen capture run `plDXPipeline`'s
  code against an emulated D3D9 device (`plWGPUD3DDevice`). At each draw,
  `plWGPUDevice` turns that device's state into WebGPU pipelines, uniforms and
  bind groups (`plWGPUDevice::DrawFixedFunction`, `DrawShader`).
- **Fixed-function draws** run one WGSL program, `ShaderSrc/FixedPipelineShaders.wgsl`,
  which implements the D3D9 fixed-function pipeline as Direct3D 9's
  documentation defines it: vertex lighting, texture coordinate generation and
  transforms, the texture stage operations, specular, fog, the 8-bit alpha test,
  and pixel centres on integer window coordinates. The number of stages,
  lighting, fog mode and vertex blending are pipeline-overridable constants.
- **Shader programs.** The game's vertex and pixel shader programs (D3D9
  `vs_1_1` and `ps_1_1`) are translated from their D3D9 bytecode, the
  `plSurface` headers the shader table hands to the device, into WGSL by
  `ShaderSrc/gen/d3d9_to_wgsl.py`. Each instruction becomes one WGSL statement;
  the D3D9 instruction semantics (`rcp`, `rsq`, `expp`, `frc`, the `ps_1_1`
  range and the output clamps) live in `ShaderSrc/ShaderPrelude.wgsl`. Only the
  programs that shipped data uses are translated.
- **Embedding.** The WGSL files are compiled into the client as C++ string
  literals (`EmbedWGSL.cmake`).
- **The device and the canvas.** WebGPU comes from Dawn's `webgpu.h` for
  Emscripten (the `emdawnwebgpu` port). The page's canvas is transferred to the
  client thread as an `OffscreenCanvas`. There is no present call: a frame
  reaches the canvas when the client thread's task ends. Pipeline states are
  created synchronously, since the single client thread cannot wait for an
  asynchronous build. Depth persists across passes and frames, and clears
  follow D3D9's `Clear`.
- **Textures.** DXT textures upload as BC1 or BC3 when the device supports
  BC compression; otherwise they are decompressed on the CPU. The level chain is
  the one `plDXPipeline` would upload.
- **HD texture packs** are optional replacements for shipped textures, matched
  by key and checked byte for byte (`plWGPUTexturePack`; see
  `HD texture pack format.md`).

## Networking

A browser cannot open TCP sockets. `pnAsyncCore` on Emscripten
(`pnAcSocketEmscripten.cpp`, with its JavaScript half `pnAcWebSocket.js`)
keeps the native socket interface and its callback semantics, and carries each
of the engine's TCP connections over one WebSocket to a gateway:
`<gateway>/connect?host=<address>&port=<port>`. The WebSocket is a raw byte
pipe, so the auth, game, file and gatekeeper protocols, and their encryption,
are the native client's. One pthread, the socket thread, owns all WebSockets.

The gateway is part of the deployment. When the deployment uses admission
tokens, the client fetches a single-use token from the page's own origin
(`POST /runtime/join`) before each socket and offers it as the WebSocket
subprotocol `plasma.join.<token>`, so it never appears in a URL. The gateway:

- admits a handshake only from the deployment's page origins, and with a valid
  token when the deployment uses them;
- connects only to the hosts and the port of the shard's own `server.ini`, and
  to any further hosts the deployment names;
- closes a connection whose first byte is not one of the client's connection
  types (auth, game, file, gatekeeper) before it reaches the shard;
- applies per-client and per-process limits.

There are two implementations of the same rules: `gateway.py` in the deployment
package, for a server of one's own, and a Cloudflare Worker (`web/deploy/worker`)
that also serves the page kit and the token endpoint.

The client reads the shard's `server.ini` with the original `pfServerIni`. The
page fetches the file from its own origin, checks it against the SHA-256 that
the deployment document pins, and passes its text to the client
(`plEmscriptenRuntime.cpp`), which writes it to a memory file and loads it as
the native client loads its own.

## Files and storage

The client uses WasmFS. Its file systems:

- **Game data, `http` mode.** The game's files are served as a tree and mounted
  with a WasmFS fetch backend (`main.cpp` `MountFetchTree`). A fetch directory
  cannot list itself, so the server publishes a manifest beside each tree,
  `<base>/.fetch-manifest` (`D <path>` for a directory, `F <mtime> <path>` for a
  file). The JavaScript half of the backend (`plEmscriptenFetchBackend.js`) replaces
  Emscripten's: the first `stat` of a file is one HEAD request, reads fetch only
  the aligned HTTP Range chunks they touch, a strong ETag pins each file so that
  a read never mixes versions, and an LRU cap bounds the chunks held in memory.
  Verified chunks are kept between sessions in the origin private file system
  (OPFS) with an IndexedDB index (`plEmscriptenAssetCache.js`).
- **Game data, `patcher` mode.** The original patcher (`pfPatcher`) fills a
  per-deployment OPFS install directory from the shard's file server, as
  `plUruLauncher` fills an installation, behind a page version of the launcher
  dialog. A deployment may add a client package, the files a native installer
  carries beyond the shard's manifests; missing or changed files are copied in
  before the client starts. The shard's game code comes the same way.
- **Fonts.** The two faces of the debug and loading text are mounted read-only
  at `/fonts`.
- **User data.** `plFileSystem::GetUserDataPath()` (the profile: `Init`, logs,
  the remembered login) is on OPFS, or in memory when OPFS is unavailable.
- **Working directory.** In `http` mode the client's working directory is a
  view that merges the immutable game files with a private writable directory
  for the files the game saves; the game files' names always win
  (`plEmscriptenCwd.cpp`).

The client asks the browser for persistent storage once, when the player
starts a login (`navigator.storage.persist()`), never on page load.

## The page

The page is three files in `Sources/Plasma/Apps/plClient/emscripten/shell/`:
`index.html` (the markup and styles), `boot.mjs` (the page's logic) and
`config.mjs` (the deployment document's rules). The page:

1. checks the browser: cross-origin isolation, JavaScript Promise Integration
   (`WebAssembly.Suspending` and `promising`), WebGPU, and `OffscreenCanvas`
   transfer. A missing feature stops the page with a list of what is missing;
2. loads the deployment document, `/runtime/config.json`, from its own origin,
   and refuses it unless it follows `config.mjs`'s rules (the same rules the
   deployment tools apply);
3. builds the client's configuration from it and loads the client script,
   `build/plClient.js` or, for a Python 2.3 shard, `build/plClient-py2.js`.

The player logs in through the client's own login dialog. The page shows a
status line until the client's first frame is on the canvas, and offers the
display scale, fullscreen and, when the deployment has packs, the HD texture
setting.

### The page and the module: `plasma`

The client script is built with Emscripten's `MODULARIZE`, for the browser
alone (`ENVIRONMENT=web,worker`): it defines two globals, the factory
`createPlasmaClient(moduleArg)` and Emscripten's `isPthread`, and the runtime
(its memory, its environment, its libraries) lives inside the factory, out of
reach of the page's other scripts. Each pthread worker loads the same script,
and `isPthread` has it start its own copy of the runtime. The factory returns
the page's `moduleArg` as the client's module; the page reads one thing from
it, the size of the wasm memory when the client exits, which the page's exit
line names.

The page and the client meet in one object, `plasma`. The page creates it
before anything else, fills the page's members, seals it, loads the client
script and calls the factory with the module's arguments, the object among
them (`boot.mjs`). The client reads it as `Module.plasma`, and its pre-js
(`pre.js`) fills the client's members. The page keeps the object as
`globalThis.plasma`, read-only, where a test runner reads the run's state; it
is the page's only global besides the client script's two. Its members:

| Member | Provided by | Meaning |
|---|---|---|
| `config` | page | the client's configuration: one JSON value, or null |
| `state` | page | the run's state: done, exit code, abort cause, first-frame time |
| `display` | page | the player display that the client reads its window size from, or null on a fixed canvas |
| `canvasBacking(w, h)` | page | called by the client when it resets its display to a new backing size |
| `setStage(w, h)` | page | a fixed canvas's fit box |
| `setFullscreenStage(active, w, h)` | page | the fullscreen layer's seam |
| `bootFailure(missing, detail, source)` | page | a browser requirement is missing (the preflight, or the client's WebGPU device) |
| `firstFrame()` | page | called by the client when its first frame is on the canvas |
| `storage` | client | the persistent-storage request at login |
| `fetch` | page | the data backend's bounds (cache bytes, write-behind bytes, read-ahead chunks), or null for the defaults |
| `login` | client | the open login dialog, or null |
| `launcher` | client | the open launcher dialog of a patcher deployment, or null |
| `input` | client | the input layer's handle (readiness, status, pointer lock and fullscreen requests), or null until the client attaches its input |
| `fullscreen`, `fullscreenStatus` | client | the fullscreen layer and its last status, or null until the client sets its first display mode |
| `audioBackend`, `audioActivationError` | client | the native-audio build's playback: its backend's name, and why the browser refused to resume its audio context; null in every other build |

The configuration is the client's only input from the page. `pre.js` turns it
into the client's environment and replaces whatever the environment held; the
module does not export `ENV`. Its keys:

| Key | Environment variable | Meaning |
|---|---|---|
| `data` | `PLASMA_DATA_URL` | the game data's base URL (the served tree, or the client package) |
| `files` | `PLASMA_DATA_FILES` | `http` or `patcher` |
| `fonts` | `PLASMA_FONTS_URL` | the fonts' base URL |
| `sound` | `PLASMA_SOUND_URL` | the sound files' base URL, or null |
| `gateway` | `PLASMA_WS_PROXY` | the WebSocket gateway |
| `join` | `PLASMA_WS_JOIN` | the gateway's token endpoint, or null |
| `serverIniText` | `PLASMA_RUNTIME_SERVER_INI` | the shard's `server.ini`, verified against its pin |
| `name` | `PLASMA_DEPLOYMENT_NAME` | the deployment's name (the remembered login's key) |
| `status` | `PLASMA_RUNTIME_STATUS_URL` | the server status relay for the login dialog, or null |
| `hdPack` | `PLASMA_TEXTURES_HDPACK_URL`, `PLASMA_TEXTURES_HDPACK_AGES` | the HD texture packs `{base, ages}`, or null |
| `assetOrigins` | `PLASMA_RUNTIME_ASSET_CACHE_ORIGINS` | the asset origins the cache pins |

An unknown key, or a value of the wrong kind, stops the client before `main()`
runs.

## Deployment

The deployment tools are one Python package, `web/deploy/plasma_web`. A
deployment is one operator file (`deployment.py`) that names the shard by its
own `server.ini`, the local trees of game data, fonts and sound, the gateway's
policy and the page's capabilities. Its readers:

- `server.py`, the origin server: the page, the client build, the game trees
  with strong ETags and Range support, the page document `/runtime/config.json`
  and the shard's `server.ini`, the token endpoint, a status relay for the login
  dialog, and the HD packs;
- `gateway.py`, the WebSocket gateway;
- `emit_static.py`, which writes a kit for a static host: the page, the client
  (product builds only), the page document, the `server.ini`, a `_headers` file
  with the isolation headers, the Content-Security-Policy and the cache policy,
  the fonts, and the licence files with `source.json`, which says where the
  source of that client is published.

The tests of the package are in `web/tests`. `Deploying.md` describes the
deployment file, the hosting choices and the gateway's admission for an
operator.

## Python

The game's scripts run in an embedded CPython, as in the native client. The
default flavour embeds CPython 3.12.9 and runs the game code of shards that
serve Python 3 scripts (H-uru shards). The py2 flavour, `plClient-py2`, embeds
CPython 2.3.3 to run the game code of MOULa unchanged. The deployment selects
the flavour; see `Python 2.3 flavour.md`. In both, the game code a release runs
is the shard's own, delivered by the shard's file server.

## Test hooks

Tests and comparisons need instruments that a player never should: render and
state readouts, pins of time and camera, a scripted login. They live in a
test-hook overlay that is not part of this repository, and only a harness build
compiles them:

- the CMake option `PLASMA_TEST_HOOKS` is off by default. Switching it on
  defines `PLASMA_TEST_HOOKS` and adds the overlay's directory; configuring stops
  when the overlay is missing;
- the engine meets the overlay at a few seams, each a header that declares
  nothing unless `PLASMA_TEST_HOOKS` is defined, and whose every call site is
  under `#ifdef PLASMA_TEST_HOOKS`: the client seam
  (`plEmscriptenTestHooks.h`), the pipeline observer
  (`plWGPUPipelineObserver.h`), the resource patcher's observer and local game
  code (`plResPatcherObserver.h`, `plResPatcherLocalGameCode.h`), the Python
  initialisation seam (`plPythonInitHooks.h`) and the SDL time codec
  (`plSDLTimeCodec.h`). In a harness build each seam's pointer stays null until
  the overlay installs it;
- the page has one entry point for a test harness page, `boot(harness)`. The
  player page passes nothing, and the page reads no test parameter itself. A
  harness page's own deployment rules (its test modes of game code) reach
  `config.mjs` through that entry point, and the deployment tools take them only
  when the harness's own tools register them (`deployment.register`);
- the runtime's own objects (its memory, OpenAL, SDL2, WebGPU, the pthreads)
  stay inside the factory. A harness build adds a pre-js that hands them to the
  harness page's instruments as `plasma.harness.runtime`, and in a pthread
  worker as the worker's `plasmaHarnessRuntime`; a product build has no such
  pre-js;
- `emit_static.py` refuses a client built with test hooks, and any file of the
  overlay, so a kit only ever carries a product build.

A product build (`PLASMA_TEST_HOOKS` off) is the only build that is deployed or
published.
