# AGENTS.md

Read this before you change anything here, whether you are a person or a coding
agent. It says how to build and test the browser client, and the rules a change
has to follow.

This repository is H-uru's Plasma, the engine of Myst Online: Uru Live, with a
port of its client to the browser: WebAssembly built with Emscripten, rendered
with WebGPU. The engine, the game's Python, PhysX, the game files and the
network protocols are the native client's. What the port adds is the layer that
puts them in a browser: the application front end, the WebGPU pipeline, sockets
over WebSockets, browser storage, and the tools that serve a deployment.

Written with AI coding assistants (Claude and Codex). You may use them too; you
answer for every line you submit, whoever wrote it.

## The native C++ client is the specification

The browser client must behave and look like the native client. A difference
from native is a bug, unless the platform forces it (no TCP sockets, no
blocking on the browser's main thread, no direct access to local files). Where
it does, the code says which constraint forces the difference, next to the
difference. Port the engine's code paths; do not write simplified substitutes,
stubs or approximations. Rendering follows `plDXPipeline` (Direct3D 9) for how
the game looks and `pfMetalPipeline` for how a modern pipeline is structured.

## Where things are

| Path | What |
|---|---|
| `Sources/Plasma/` | the engine, upstream's, with guarded browser branches |
| `Sources/Plasma/Apps/plClient/emscripten/` | the browser front end of `plClient`; `shell/` is the page (`index.html`, `boot.mjs`, `config.mjs`) |
| `Sources/Plasma/FeatureLib/pfWGPUPipeline/` | the WebGPU pipeline; `ShaderSrc/gen/` translates the game's D3D9 shader programs to WGSL |
| `Sources/Plasma/NucleusLib/pnAsyncCore/` | the engine's sockets, over WebSockets on Emscripten |
| `cmake/emscripten/`, `Scripts/Triplets/wasm32-emscripten-plasma.cmake`, `Scripts/Ports/physx/` | the Emscripten toolchain, the vcpkg triplet, the PhysX port |
| `web/build/` | the dependency and client builds (`build-deps.sh`, `build-client.sh`, `python2/` for CPython 2.3.3), the pins of every download (`sources.lock`), the licence notices (`notices/`) and the build's checks |
| `web/deploy/` | `plasma_web`: the origin server, the WebSocket gateway, the deployment and static-kit tools; `worker/`, the gateway as a Cloudflare Worker; `fonts/`, the free substitute fonts |
| `web/tests/` | the tests of `plasma_web`; `run.py` runs them |
| `Docs/Web/` | the documents: `Building.md`, `Running locally.md`, `Deploying.md`, and the designs, `Architecture.md`, `Python 2.3 flavour.md`, `HD texture pack format.md` |

Start with `Docs/Web/Architecture.md`. It describes the components, the page's
contract with the client (`plasma`) and the test-hook seams. A change to
a design keeps its document true in the same change.

## Building

The native clients build as upstream documents
(<https://h-uru.github.io/Plasma/building.html>); a browser change must keep
them building. The unit tests are upstream's:

```sh
cmake -S . -B build -DPLASMA_BUILD_TESTS=ON      # plus upstream's options for your platform
cmake --build build && cmake --build build --target check
```

The browser client needs the Emscripten release that `web/build/sources.lock`
pins (`emsdk install 6.0.12 && emsdk activate 6.0.12`), CMake, Ninja, Python
3.13 and the vcpkg submodule (`git submodule update --init vcpkg`). Every
download is pinned with its SHA-256 in `sources.lock` and checked before use.
Keep build and dependency directories outside the checkout.
`Docs/Web/Building.md` lists the prerequisites on macOS and on Debian or
Ubuntu, and what each step builds and takes.

```sh
export EMSDK=<emsdk checkout> PLASMA_WASM_DEPS=<dependency prefix>
web/build/build-deps.sh           # once: vcpkg ports, CPython 3.12.9 and 2.3.3, OpenAL, movie codecs
PLASMA_WASM_BUILD=<build dir> web/build/build-client.sh clean configure build
PLASMA_PYTHON_ABI=2 PLASMA_WASM_BUILD=<py2 build dir> web/build/build-client.sh clean configure build
```

The first client is `plClient.{js,wasm}`, with CPython 3 for H-uru shards; the
second is `plClient-py2.{js,wasm}`, with CPython 2.3.3 for shards that run the
original Python 2.3 game code (`Docs/Web/Python 2.3 flavour.md`). Both land in
`<build dir>/Sources/Plasma/Apps/plClient/`. Each flavour uses its own build
directory. `build-client.sh` lists its other variables at the top of the file.

The WGSL in `pfWGPUPipeline/ShaderSrc/` that the generator writes is checked
in. After changing the generator, regenerate; before a commit, check:

```sh
python3 -B Sources/Plasma/FeatureLib/pfWGPUPipeline/ShaderSrc/gen/d3d9_to_wgsl.py --check
```

## Tests without private data

None of these needs a shard, game files or an account.

```sh
python3 -m venv <venv> && <venv>/bin/python -m pip install -r web/tests/requirements.txt
<venv>/bin/python -m playwright install chromium
<venv>/bin/python web/tests/run.py --build-dir <build dir>
```

Keep `<venv>` outside the checkout, as the build directories: a release build
refuses a checkout with untracked files. `Docs/Web/Running locally.md`, "The
tests", says what each prerequisite below enables.

`run.py` runs every suite, each in its own interpreter with bytecode off, and
prints one PASS or FAIL line per suite; `--help` lists the suites and what each
needs. Name suites to run only those. The WGSL check above is one of them
(`wgsl_regen`).

A suite skips the checks whose prerequisite is missing, and says which: `node`
on `PATH` (the page's modules), `openssl`, Playwright's Chromium, the Worker's
pinned `wrangler` (`PLASMA_WRANGLER`, the release `WRANGLER_VERSION` names in
`.github/workflows/web-build.yml`; without it the gateway contract runs
against `gateway.py` alone), and a built client (`--build-dir`, the build
directory of `build-client.sh`; three page checks read it). `--require all`
makes a missing prerequisite an error instead, as the CI runs it.

A change to `plasma_web` comes with tests in `web/tests/`, in `unittest`, with
no network access beyond loopback and no files outside a temporary directory.

## Code

- **Guards.** Browser code in an upstream file is guarded, so that the native
  build is unchanged: `#ifdef __EMSCRIPTEN__` in C++, `if(EMSCRIPTEN)`
  in CMake. The renderer's hooks outside `pfWGPUPipeline` use
  `PLASMA_PIPELINE_WGPU`, which CMake turns on with the Emscripten toolchain.
  The Python 2.3 flavour's files build only when `PLASMA_PYTHON_ABI` is 2. Where
  the platform has no equivalent at all, replace the whole file and select it
  in CMake, as `pnAcSocketEmscripten.cpp` replaces `pnAcSocket.cpp`.
- **Test hooks.** Instruments for tests (state readouts, time and camera pins,
  scripted logins) are compiled only with the CMake option `PLASMA_TEST_HOOKS`,
  which is off by default; their overlay is not in this repository, so a
  product build never contains them. The seams are headers that declare
  nothing without `PLASMA_TEST_HOOKS`, and every call site is under
  `#ifdef PLASMA_TEST_HOOKS`. Do not add a test-only parameter, branch or
  console line outside that guard, in the client or in the page.
- **Style and naming** are upstream's: `.clang-format`, `.clang-tidy` and
  `Docs/Preferred code style.md` (classes with a prefix such as `pl`, `f` for
  members, `s` for statics, `g` for globals). Follow the surrounding code
  where the document is silent: the engine names a class's internal methods
  with `I` and its constants with `k`. Browser files take the engine's prefix
  and name their platform: `plEmscripten*`, `pnAc*Emscripten`, `plWGPU*`,
  `pfWGPU*`. Format only the lines you change, with clang-format 21 or later
  (`git clang-format` formats the lines a commit changes). The port's own
  Python and JavaScript are formatted whole, by the versions
  `web/build/formatters/` pins: `ruff format` for the Python under `web/` and
  the shader generators, prettier for the page's scripts, its input modules
  and the gateway Worker, and eslint's correctness rules on the same scripts
  (`ruff.toml`, `.prettierrc`, `.prettierignore`, `eslint.config.mjs`).
  `Docs/Web/Building.md`, "Formatting", installs and runs them; run all three
  before a commit that changes those files. Upstream's Python (`Scripts/`),
  the JavaScript Emscripten links into the client (`--js-library`,
  `--pre-js`) and the page's HTML (`shell/index.html`) are not formatted.
- **Comments explain the code**: what it does and why, in plain sentences.
  Where code follows the native client, cite the file and symbol: the
  function, method, class, constant, enum value, macro or member the cited
  code holds or sits in (`plDXPipeline.cpp` `plDXPipeline::IHandleStageBlend`);
  add the line when it points at one statement, and check that it still does
  when you change the cited file or merge upstream.
  No change history, ticket ids, issue numbers, names of contributors or
  assistants, or test-run narration in code or comments; the history belongs
  in the commit message.
- **No host data** anywhere in the tree: no absolute paths from your machine,
  no server addresses, account or player names, credentials or keys. Examples
  use `example.com`, `example.invalid` and placeholders such as `<build dir>`.
- **Game data** from a game installation or a shard (`.prp` pages, sounds,
  textures, packed scripts) stays out of the repository. Tests build the small
  inputs they need.

## Licence header

Every source file the port adds carries upstream's GPLv3 licence block, with
the Cyan Worlds copyright line unchanged and no copyright line of its own.

- C++, Objective-C++, WGSL, JavaScript: the first 41 lines of
  `Sources/Plasma/Apps/plClient/plClient.cpp`, verbatim.
- Python, shell and CMake: the same text with each line behind `# ` (`#` alone
  for an empty line), after any `#!` and coding line, as upstream's `.py`
  files carry it.
- HTML: `<!doctype html>` first, then the C form inside `<!-- -->`.
- A file that contains code from Emscripten keeps Emscripten's MIT notice
  after the block, with a line that names the source file.

An upstream file you modify keeps its own block as it is. Third-party files
(PhysX patches, CPython's configuration, vcpkg-derived triplets) keep their own
licences. Documents and data files carry no header.

## Commits and pull requests

Upstream's `CONTRIBUTING.md` applies. Keep commits small, and say in the
message what changed and why, citing the native code it follows. A pull
request states which builds and tests you ran.
