# The Python 2.3 flavour

The browser client is built in two flavours from the same source. They differ
only in the Python interpreter that runs the game's scripts:

| Flavour | Interpreter | Client files | Runs |
|---|---|---|---|
| default | CPython 3.12.9 | `plClient.js`, `plClient.wasm` | the game code of a shard that serves Python 3 scripts (an H-uru shard) |
| py2 | CPython 2.3.3 | `plClient-py2.js`, `plClient-py2.wasm` | the game code of a shard that serves Python 2.3 scripts: Myst Online: Uru Live again (MOULa) |

This document explains why the py2 flavour exists, how its interpreter is
pinned, what its glue files do, and how a build and a deployment select it.

## Why MOULa's game code needs CPython 2.3.3

A shard sends its game code to the client through its file server: the
compiled scripts in one `python.pak`, and the SDL descriptors. The client
installs them as the native client does (`plResPatcher`). Each entry of a
`python.pak` is a code object in CPython's `marshal` format, which the client
turns back into code with `PyMarshal_ReadObjectFromString`
(`plPythonPack.cpp`). The marshal format and the bytecode inside it belong to
the interpreter version that wrote them: CPython 3.12 cannot load a code object
written by CPython 2.3, and the reverse is just as true.

MOULa's `python.pak` is Python 2.3 marshal data. Cyan's client, published as
the CyanWorlds.com Engine (CWE) and maintained by OpenUru, embeds a lightly
modified CPython 2.3.3, and MOULa's scripts call the script API of Cyan's glue.
H-uru's Plasma, which this client is built from, moved to Python 3 and changed
parts of that API. To run MOULa's own scripts unchanged, as they arrive from
the shard, the py2 flavour embeds the same CPython 2.3.3 and puts the API those
scripts expect on top of H-uru's glue.

The pak format itself is the same in both clients; only the marshal data in it
differs. The game-code analyser in the deployment tools
(`web/deploy/plasma_web/analyse_gamecode.py`) reports, for a captured pak, which
marshal family and Python version wrote each entry.

## The interpreter and its pin

The interpreter is the `Cypython-2.3.3` tree of OpenUru's CWE-ou-minkata
repository at commit `7978641e0cd7`
(`Build/VS2010/SDKs/XPlatform/Cypython-2.3.3`). The python.org release of 2.3.3
cannot stand in for it: of the tree's 442 files, 49 carry code changes against
the release and 16 are not in the release at all.

`web/build/sources.lock` pins the tree three ways: the commit, the git tree id
of that path at the commit, and a SHA-256 manifest of its files
(`web/build/tree-hash.py`). A vendored proof holds the commit object and the
trees on the path, and `web/build/commit-proof.py` follows them by their git
ids, so no other commit, path or tree passes. The build checks the pin before
every step and refuses any other tree.

The build never contacts OpenUru. It rebuilds the tree from public sources:
Cyan's 2011 CWE release, which H-uru's Plasma repository carries as its first
commit, with every CRLF line ending converted to LF, plus OpenUru's delta (six
source edits and three Visual Studio project files), which is vendored with
its own SHA-256. The result must match the pinned tree id and manifest.

The build then compiles the tree into a static `libpython2.3.a` for
WebAssembly:

- with the client's own compiler flags (pthreads, WebAssembly exceptions), so
  that the archive links into the client;
- with a hand-written `config.c` and `pyconfig.h` for Emscripten, filled in from
  CPython's templates;
- with four patches, each explaining itself: a name clash in `posixmodule.c`,
  callees that take two parameters (a call through a function pointer of the
  wrong signature traps in WebAssembly), the `os` module named `nt` as in
  Cyan's Windows client, and Windows path separators in that module;
- from the translation units of Cyan's static Windows project, with the stock
  `import.c` in place of Cyan's pak import (the client serves the pak itself,
  below), no dynamic loading and no threads.

CPython 2.3.3 is under the Python Software Foundation licence, not the GPL. Its
`LICENSE` travels with the library: the build installs it beside the archive and
refuses a copy that differs from the tree's.

A release's third-party source bundle carries the pinned tree itself (checked
against its tree id and manifest), the proof, OpenUru's delta and the PSF
licence, so the interpreter's source is offered with every release.

## The glue

One glue source compiles for both interpreters. Every `pfPython` unit includes
Python through `pyPy2Compat.h`: with CPython 3 it is exactly `<Python.h>`; with
CPython 2.3 it adds the Python 3 C-API names H-uru's glue uses and maps them
onto 2.3 with the conventions MOULa's scripts were written for. The files that
exist only for the py2 flavour, all compiled only when `PLASMA_PYTHON_ABI` is 2
(except where noted):

| File | What it does |
|---|---|
| `pfPython/pyPy2Compat.h`, `.cpp` | The C-API layer. Text: the engine's UTF-8 strings reach a script as 8-bit `str` in Windows-1252, and the converters accept such a `str` or a `unicode`. Integers: `PyLong_FromLong` makes an `int`, as Cyan's glue did. Booleans: returned as the integers 0 and 1, as Cyan's glue returned them. |
| `pfPython/pyPy2Text.h`, `.cpp` | The UTF-8 and Windows-1252 text codec. It holds no Python, so it is built and unit-tested on its own (`test_pfPython2`) in either flavour. |
| `pfPython/cyPythonModule302.cpp` | The import of modules from `python.pak`: a PEP 302 finder on `sys.meta_path`, in place of the default flavour's PEP 451 finder (`cyPythonModule451.cpp`). It answers for top-level names only, after the builtin and frozen modules, as Cyan's patched import did, and a module's `__file__` is its name. |
| `pfPython/pyEnum2.cpp` | Cyan's own script enum types (`EnumValue`, `Enum`), ported from CWE's `pyEnum.cpp`. |
| `pfPython/pyCWEOverlay2.h`, `.cpp` | The CWE surface overlay: Cyan's names, argument orders and result types, put back on top of H-uru's `Plasma` module after the builtin modules exist and before any script runs. Each entry cites the CWE glue it reproduces. |
| `pfPython/pyScoreMgr2.h`, `.cpp` | Cyan's synchronous `ptScoreMgr` and `ptGameScore`: each call blocks until the auth server answers, built on H-uru's score transactions in `plNetGameLib`. H-uru replaced this API with an asynchronous one. |
| `pfPython/pyGameMgr2.h` | Cyan's GameMgr (mini-game) glue, which MOULa's scripts use for the Heek table, the KI marker games, the Eder Delin Blue Spiral and the Ahnonay quabs. Its game clients post each server message to the script as a `ptGameCliMsg`; H-uru replaced them with handler-based clients and has no Heek client. |
| `pfPython/pyGameMgrWire2.h`, `.cpp` | The GameMgr wire side, with no Python: the two messages the game manager posts to a script, the game types, the game clients by game id, and the requests Cyan's glue builds. Unit-tested with `test_pfPython2` in either flavour. |
| `pfPython/pyGameCli2.cpp` | The Python game-client classes, the `PlasmaGame` functions and their enums. |
| `pfPython/pyGameMsg2.cpp` | The Python message classes. |
| `pfPython/pyGameGlue2.h` | What `pyGameCli2.cpp` and `pyGameMsg2.cpp` share: the glue classes and the result conversions with Cyan's types. |
| `pfPython/pfPython2Creatable.h` | Registers the two GameMgr messages as classes (never read or written). |
| `pfGameMgr/pfGmHeek.h`, `.cpp` | The Heek game client class over H-uru's GameMgr transport, with the four requests a script sends. The GameMgr glue relays its server messages and builds its join, so it has no handler of its own. In `pfGameMgr` only for the py2 flavour; the default flavour builds it only for its unit test. |
| `pfGameMgr/pfGameMgr2Creatable.h` | Registers the Heek client's classes. |

Shared files carry small py2 branches of their own:

- `cyPythonInterface.cpp` (`PythonInterface::initPython`) starts CPython 2.3 the
  way Cyan's client did, in this order: no `site` at start-up, the six builtin
  `Plasma*` modules, the overlay, the pak finder, then `import site` from the
  pak, and finally `sys.path` set to the three loose-script folders Cyan's
  client used.
- `plPythonFileMod.cpp` passes the arguments of the script handlers in Cyan's
  shapes and number types, and delivers the GameMgr messages to
  `OnGameMgrMsg` and `OnGameCliMsg`.
- `pyGlueHelpers.h` and `plPythonConvert.h` return booleans as integers, and
  end the type objects where CPython 2.3's `PyTypeObject` ends.

## Selecting the flavour

### At build time

The CMake cache variable `PLASMA_PYTHON_ABI` selects the interpreter: `3` (the
default) or `2`. The value `2` is accepted only for Emscripten builds, and it
needs a CPython 2.3 prefix in `PLASMA_WASM_PYTHON2` that holds
`lib/libpython2.3.a` and `include/python2.3/`. The dependency build's `python2`
step installs one. With `2`:

- `pfPython` builds the py2 files above in place of the PEP 451 finder;
- `pfGameMgr` adds the Heek client;
- `plClient` is written as `plClient-py2.js` and `plClient-py2.wasm`;
- no CPython 3 headers or library are needed.

The sources test the same name: the root `CMakeLists.txt` defines
`PLASMA_PYTHON_ABI` (3 or 2) on the Python target the build links, and every
flavour branch in `pfPython` is an `#if PLASMA_PYTHON_ABI` test, never one of
`PY_MAJOR_VERSION`. `pyPy2Compat.h` stops the build when the definition is
missing or disagrees with the Python headers.

The client build script takes the same variable (`PLASMA_PYTHON_ABI=2`), and a
py2 build uses its own build directory.

The default flavour's CMake refuses a libpython 3 built with
`PY_CALL_TRAMPOLINE`, because the frame loop suspends inside Python frames and
cannot suspend across the JavaScript frame of a trampoline. CPython 2.3 calls C
functions directly, so the check does not apply to the py2 flavour.

### At deployment time

The flavour is the deployment's choice, never the page's. In the deployment
file, `data.gameCode` names where the game code comes from:

- `server`: the shard's own Python and SDL, run by the default flavour;
- `server-py2`: the shard's own Python and SDL, exactly as `server` (the client
  asks the shard for both), run by the py2 flavour. It is accepted only for
  patcher deployments (`data.files` `patcher`), in which the original patchers
  fetch the game's files from the shard.

For `server-py2` the deployment tools write `gameCode: "server-py2"` and
`build: "py2"` into the page document. The page's `config.mjs` refuses a
document in which the two do not go together, and the page then loads
`build/plClient-py2.js`. The static kit for such a deployment carries the py2
client files in place of the default ones.

## Scope of this release

The py2 flavour runs only game code that the shard itself delivers. A release
deployment may name only `server` or `server-py2`. The modes in which a
deployment ships game code of its own (`client`, and `client-python`) are
not part of the product: only the private test harness accepts them,
for a local test deployment, and every product tool refuses them. Their
`python.pak` is packed by a tool with a change that is not in this source
tree, so serving one would mean serving object code whose build tool is not
published.
