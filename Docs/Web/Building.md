# Building the browser client

This document builds the browser client from a checkout of this repository, on
macOS or Linux. The client builds in two flavours from the same source:

- `plClient` embeds CPython 3.12.9 and runs the Python 3 game scripts of H-uru
  shards;
- `plClient-py2` embeds CPython 2.3.3 and runs the original Python 2.3 game
  scripts of Myst Online: Uru Live (see `Python 2.3 flavour.md`).

`Running locally.md` serves a build on your own machine and runs the tests,
`Deploying.md` deploys one for a shard's players, and `Architecture.md`
describes the components. The native clients build as H-uru documents
(<https://h-uru.github.io/Plasma/building.html>); nothing here is needed for
them.

## Overview

The build has two stages:

1. `web/build/build-deps.sh` builds every library the client links, for
   WebAssembly, into one folder: the dependency prefix. You build it once, and
   every client build reads it.
2. `web/build/build-client.sh` configures and builds one client against that
   prefix, in a build directory of its own. You run it once for each flavour.

Both scripts take their inputs from environment variables only, write nothing
into the checkout, refuse any Emscripten release but the pinned one, and check
every file they download against its SHA-256 in `web/build/sources.lock`
before they use it. Keep the prefix and the build directories outside the
checkout, on paths without spaces.

What each stage took on a 15-core Apple-silicon Mac, with the Emscripten SDK
installed and nothing else built:

| Stage | Time | Disk | Downloads |
|---|---|---|---|
| Dependency prefix | 5 min 39 s | 2.3 GB, 1.7 GB of it build trees | about 340 MB of source archives, and CPython 2.3.3's tree (below) |
| `plClient` | 36 s | 550 MB | none |
| `plClient-py2` | 35 s | 545 MB | none |

The repository's CI workflow, `.github/workflows/web.yml`, runs
`.github/workflows/web-build.yml` on GitHub's `ubuntu-24.04` runner (x86-64):
it builds the prefix and both clients, then runs the web tests
(`Running locally.md`) against `plClient`. It caches the Emscripten SDK, the
vcpkg ports, and the CPython and movie codec builds, so a later run builds
only the two clients. On a 2-core runner (public repositories get 4 cores), a
run with nothing cached took 41 minutes to build, 21 of them for the prefix;
with every cache filled, a run took 25 minutes: 9 minutes for each client and
6 for the tests. A second job checks the format of the port's Python and
JavaScript ("Formatting" below).

## Prerequisites

The scripts need bash 3.2 or newer, a C and C++ compiler, make, perl, git,
curl, tar, CMake 3.20 or newer, Ninja, and Python 3.12 or newer, plus the
pinned Emscripten SDK and the vcpkg submodule. They need no Python packages
and no libraries of the host: every library the client links is built from
source for WebAssembly. The hosts the scripts know are macOS (Apple silicon or
Intel) and Linux (x86-64 or arm64).

The web tools and their tests (`Running locally.md`) need Python 3.13, so a
Python 3.13 serves both.

### macOS

macOS itself has bash, perl, curl and tar. Install the Xcode Command Line
Tools (clang, make and git) and, with Homebrew, CMake, Ninja and Python:

```sh
xcode-select --install
brew install cmake ninja python@3.13
```

The `python3` of macOS itself is too old. A Python 3.13 from python.org works
as well as Homebrew's; the scripts use the `python3` on `PATH`, or the one
`HOST_PYTHON` names.

The repository's `Brewfile` is H-uru's list for the native clients. The
browser build needs none of its libraries.

### Debian and Ubuntu

```sh
sudo apt-get install build-essential git curl ca-certificates tar xz-utils \
    unzip python3 cmake ninja-build
```

`build-essential` brings the compiler, make and perl; `xz-utils` lets `tar`
unpack the `.tar.xz` archives. Ubuntu 24.04's `python3` is 3.12, which is
enough to build; the web tools need 3.13 (Debian 13's `python3`, or another
Python 3.13 installation).

The CI builds on GitHub's `ubuntu-24.04` runner image, which has these
packages and many more. A build on a minimal installation from this list alone
has not been tried. The CI takes CMake 4.4.3 and Ninja 1.13.2 from
`sources.lock` instead of the distribution's packages:

```sh
web/build/sources.sh fetch cmake-4.4.3-linux-x86_64 <downloads>
web/build/sources.sh fetch ninja-1.13.2-linux-x86_64 <downloads>
```

`sources.sh fetch` downloads a pinned file into a folder, checks its SHA-256
and prints its path. Unpack the two archives (the second is a `.zip`, hence
`unzip`) and put the folders that hold `cmake` and `ninja` first on `PATH`.

### Emscripten

The client and every library in the prefix must come from one compiler,
Emscripten 6.0.12, installed with emsdk at the commit `sources.lock` pins:

```sh
git clone https://github.com/emscripten-core/emsdk.git <emsdk>
git -C <emsdk> checkout "$(web/build/sources.sh get emsdk commit)"
cd <emsdk> && ./emsdk install 6.0.12 && ./emsdk activate 6.0.12
```

The build scripts source `<emsdk>/emsdk_env.sh` themselves; you only set
`EMSDK=<emsdk>`. On macOS the installed SDK takes about 1.2 GB. Emscripten
compiles its own system libraries the first time a build needs them and keeps
them in a cache inside `<emsdk>`, which grows by about 0.9 GB.

emsdk does not check what it downloads. On Linux x86-64, `sources.lock` pins
the two archives emsdk fetches; the CI places them, checked, where emsdk looks
for them first:

```sh
web/build/sources.sh fetch emsdk-6.0.12-linux-x86_64 <emsdk>/downloads
web/build/sources.sh fetch emsdk-node-24.19.0-linux-x86_64 <emsdk>/downloads
cd <emsdk> && EMSDK_KEEP_DOWNLOADS=1 ./emsdk install 6.0.12
./emsdk activate 6.0.12
```

### vcpkg

The libraries come from vcpkg's ports at the commit that `sources.lock` pins,
which is also the `builtin-baseline` of `vcpkg.json` and the commit of the
`vcpkg` submodule:

```sh
git submodule update --init vcpkg
```

To use another vcpkg checkout, set `VCPKG_ROOT` to it; it must be at that
commit. The scripts use the CMake, Ninja and git on `PATH`, never copies that
vcpkg would download.

## The dependency prefix

From the root of the checkout:

```sh
export EMSDK=<emsdk>
export PLASMA_WASM_DEPS=<prefix>
web/build/build-deps.sh
```

The script runs these steps, in this order (times from the same Mac):

| Step | Builds | Into `<prefix>/` | Time |
|---|---|---|---|
| `tools` | GNU m4, autoconf, automake, libtool and autoconf-archive, which the Speex port's build runs, and the pinned vcpkg tool | `tools/` | 68 s |
| `vcpkg` | the vcpkg ports of the root `vcpkg.json` for the `wasm32-emscripten-plasma` triplet: PhysX (with the `Scripts/Ports/physx` overlay), OpenSSL, FreeType, libpng, libjpeg-turbo, zlib, bzip2, brotli, expat, Ogg, Vorbis, Opus, Speex, asio, string_theory and tl-expected | `vcpkg_installed/wasm32-emscripten-plasma/` | 2 min 4 s |
| `python` | CPython 3.12.9, as a static library with its standard library | `python/` | 2 min 2 s |
| `python2` | CPython 2.3.3 for the py2 flavour (`web/build/python2/build.sh`) | `python2/` | 3 s, with its tree in place |
| `openal` | the CMake package file of Emscripten's own OpenAL | `emscripten-openal/` | under 1 s |
| `movies` | libvpx and libwebm, the movie codecs | `movies/` | 16 s |
| `check` | three checks: every object carries the client's compiler flags, a test program links, and CMake finds the libraries with the client's find modules | `build/` | 6 s |

Its downloads go to `<prefix>/downloads` (CPython 3.12.9's source to
`<prefix>/src`): the files `sources.lock` pins, checked by their SHA-256, and
the ports' source archives, which vcpkg checks against each port's SHA-512.
CPython 2.3.3's source is the tree that OpenUru's CWE-ou-minkata repository
holds. When `<prefix>/cypython-2.3.3` is missing, the `python2` step rebuilds
it there from Cyan's 2011 tree in the first commit of H-uru's repository
(fetched with git over HTTPS) and the delta kept in `web/build/python2/`, and
checks the result against the pinned tree (`Python 2.3 flavour.md`). The
build trees go to `<prefix>/build`, the logs to `<prefix>/logs`. A failed step
prints the end of its log and the log's path.

Name steps to run only those, for example `web/build/build-deps.sh python2`.
A second run reuses the host tools and the installed ports, and runs the other
steps again.

The `vcpkg` step does not change the ports a prefix already holds. A checkout
whose triplet, chainload toolchain or ports differ from the ones the prefix
was built with gives every port another ABI hash, and vcpkg would remove them
all before building them again, so a run stopped half way would leave the
prefix without them. The step first asks vcpkg for its plan
(`<prefix>/logs/vcpkg-plan.log`); when the plan rebuilds or removes a port, it
lists those ports and stops. Build another prefix, or rebuild them in place
with `--rebuild`:

```sh
web/build/build-deps.sh --rebuild vcpkg
```

Other settings:

| Variable | Meaning |
|---|---|
| `VCPKG_BINARY_SOURCES` | vcpkg's binary cache, passed through (the CI uses `clear;files,<folder>,readwrite`). Without it the cache is off and every port builds from source |
| `VCPKG_DEFAULT_HOST_TRIPLET` | the host triplet, when the detected one (`arm64-osx`, `x64-osx`, `x64-linux`, `arm64-linux`) is wrong |
| `HOST_PYTHON` | the Python 3.12 or newer of the helper scripts; default `python3` |

Every step builds with one fixed build time, 2026-07-30 01:36:33 UTC
(`SOURCE_DATE_EPOCH` and `TZ=UTC`, set by `use_build_date` in
`web/build/common.sh`). Three libraries record when they are built: CPython
3.12.9 and 2.3.3 in `sys.version`, libjpeg-turbo in its version string and
OpenSSL in its "built on" line. They record that time instead, so a prefix
built on another day, or on another host, holds the same dates. The log lines
of `build-deps.sh` give their times in UTC too.

The prefix may be moved or copied. The builds map its path to a fixed name, so
it does not reach the client, and a copy at another path builds the same
client. The host tools in `tools/` name their own path; the next `tools` step
rebuilds them for the new one.

## The client

With `EMSDK` and `PLASMA_WASM_DEPS` set as above, from the root of the
checkout:

```sh
PLASMA_WASM_BUILD=<build dir> web/build/build-client.sh
PLASMA_PYTHON_ABI=2 PLASMA_WASM_BUILD=<py2 build dir> web/build/build-client.sh
```

The first command builds `plClient`, the second `plClient-py2`. Each flavour
needs a build directory of its own. The steps are `clean`, `configure` and
`build`, all three by default:

- `clean` removes what CMake and Ninja generated in the build directory, and
  nothing else;
- `configure` runs CMake with the Emscripten toolchain
  (`cmake/emscripten/Toolchain.cmake`) and the options of a browser build;
- `build` builds `plClient` with Ninja on every core, and collects every error
  (`-k 0`).

After a source change, `web/build/build-client.sh build` rebuilds only what
changed.

The output is `<build dir>/Sources/Plasma/Apps/plClient/plClient.js` and
`plClient.wasm` (`plClient-py2.js` and `plClient-py2.wasm` for the py2
flavour), with the logs in `<build dir>/logs/configure.log` and `build.log`.
On the Mac that timed the stages above:

| Flavour | Configure and build | `.wasm` | `.js` |
|---|---|---|---|
| `plClient` | 9 s and 27 s | 19.4 MB | 284 KB |
| `plClient-py2` | 10 s and 25 s | 13.9 MB | 281 KB |

Two builds of one commit give the same bytes, whichever prefix they read and
whichever host builds them: two prefixes built from one commit, at different
times and paths, give byte-identical clients of both flavours, and the CI's
clients, built on Linux, are byte-identical to those of a Mac. The client
depended on its prefix only through the dates that CPython and libjpeg-turbo
record, and the fixed build time above makes those the same.

Other settings:

| Variable | Meaning |
|---|---|
| `BUILD_TYPE` | CMake's build type, default `Release`, the build that ships. `Debug` and `RelWithDebInfo` keep function names and stack traces |
| `RELEASE=1` | a release build: the client records the commit and tag it is built from, and a successful build writes `<client>.release.json` beside the client, which the deployment tools turn into the deployment's `source.json` (`Deploying.md`, "Source code and licence files"). It refuses a checkout with any change that is not committed, untracked files included |
| `PLASMA_WASM_CMAKE_ARGS` | more CMake arguments, split at spaces, for example `-DPRODUCT_EMBED_BUILD_TIME=OFF`, which the CI passes |
| `PLASMA_WASM_PYTHON2` | the CPython 2.3.3 prefix of the py2 flavour; default `<prefix>/python2` |
| `VCPKG_ROOT` | as for the prefix |

A build directory holds one kind of build: the script refuses one that was
configured for the other kind (a release build or not). The script also knows
`HARNESS=1`, a build with test instruments whose sources are not part of this
repository: in this tree its configure step stops and says so. A build
without it, the default, is the product build that `Deploying.md` deploys.

## Formatting

The port's C++ follows upstream's `.clang-format` (`AGENTS.md`). Its own
Python and JavaScript have one format each, which three pinned tools check:

| Tool | Configuration | Files |
|---|---|---|
| ruff 0.17.1, `ruff format` | `ruff.toml` | the Python under `web/` and the shader generators (`Sources/Plasma/FeatureLib/pfWGPUPipeline/ShaderSrc/gen/`) |
| prettier 3.9.10 | `.prettierrc`, `.prettierignore` | the page's scripts (`shell/boot.mjs`, `shell/config.mjs`), its input modules (`input/*.mjs`), the gateway Worker (`web/deploy/worker/src/`) and `eslint.config.mjs` |
| eslint 10.13.0 | `eslint.config.mjs` | the same scripts, with correctness rules only (no style rule) |

`shell/` and `input/` are in `Sources/Plasma/Apps/plClient/emscripten/`. The
other JavaScript there is linked into the client by Emscripten
(`--js-library` and `--pre-js`), whose preprocessor reads `{{{ }}}` macros and
`#if` lines in it, so no formatter touches it. Neither does one touch the
page, `shell/index.html`: prettier re-wraps its text, which renders the same
but changes the text the page's scripts and tests read. Upstream's Python
(`Scripts/`) keeps upstream's format. ruff has no lint rule selected: it
formats only.

`web/build/formatters/` pins the three, with the hash of every file they
install (`requirements.txt` for pip, `package-lock.json` for npm). Install
them once into a folder outside the checkout, with Python 3.12 or newer and
node 20.19 or newer with npm on the `PATH`. Emscripten's node serves (it is
not on the `PATH` after `emsdk_env.sh`):

```sh
export PATH="$(echo "$EMSDK"/node/*/bin):$PATH"
web/build/formatters/install.sh <formatters dir>
```

`install.sh` takes the wheels and packages from PyPI and the npm registry,
checks every hash before it installs, and runs no package's install script.
Then, from the root of the checkout, with `F=<formatters dir>`:

```sh
"$F/venv/bin/ruff" format --no-cache --check
node "$F/node_modules/prettier/bin/prettier.cjs" --check .
NODE_PATH="$F/node_modules" node "$F/node_modules/eslint/bin/eslint.js" .
```

Each prints the files that need a change and fails; `ruff format --no-cache`
and `prettier --write .` rewrite them. Format a file you change before you
commit it. eslint finds the one package `eslint.config.mjs` loads, `globals`,
through `NODE_PATH`, because the formatters live outside the checkout. None of
the three runs in a build; the CI's format job (`web-build.yml`) installs them
with `install.sh` and runs these three commands.

## Next

`Running locally.md` serves the client you built on your own machine against a
shard, and runs the web tests. Before a commit that changes the WGSL
generator, check the generated shaders as `AGENTS.md` describes.
