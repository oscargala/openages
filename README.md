OpenAges
========

OpenAges runs the client of Myst Online: Uru Live in a web browser. This
repository is [H-uru's Plasma](https://github.com/H-uru/Plasma), the
GPLv3-licensed CyanWorlds.com Engine, with a port of its client to the browser:
the engine compiled to WebAssembly with Emscripten and drawn with WebGPU.

Website: <https://openages.net>

Myst and Uru are trademarks of Cyan Worlds, Inc. OpenAges is an independent
project and is not affiliated with or endorsed by Cyan Worlds.

H-uru's own README is kept unchanged in
[`Docs/H-uru README.md`](Docs/H-uru%20README.md). It lists the native clients'
library dependencies and links H-uru's build and run instructions.


What the port adds
------------------

The browser client is the native client's code. It runs the same engine, the
game's own Python scripts, the same PhysX, and reads the same game files and
speaks the same protocols to the same shards. The port adds what a browser
needs:

- an Emscripten front end for `plClient`: start-up, the frame loop, the
  display, the login dialog, the launcher, input, sound and browser storage;
- `pfWGPUPipeline`, a WebGPU renderer;
- the engine's sockets over WebSockets, and a gateway that relays them to the
  shard's TCP ports, which a browser cannot open itself;
- the page, which checks the browser, reads the deployment's settings and
  starts the client;
- `plasma_web`, the tools that serve a deployment or write a kit for a static
  host, and the gateway as a Cloudflare Worker.

The client builds in two flavours from the same source. They differ only in the
Python interpreter that runs the game's scripts:

| Client | Interpreter | For a shard that serves |
|---|---|---|
| `plClient` | CPython 3.12.9 | H-uru's Python 3 game scripts |
| `plClient-py2` | CPython 2.3.3 | the original Python 2.3 game scripts of Myst Online: Uru Live again (MOULa) |

[`Docs/Web/Architecture.md`](Docs/Web/Architecture.md) describes the components
and where they are; [`Docs/Web/Python 2.3 flavour.md`](Docs/Web/Python%202.3%20flavour.md)
explains the second flavour.

### Game files

The game's Ages, textures and sounds are not in this repository, and a
deployment kit contains none of them. (Like H-uru's repository, this one keeps
the game's scripts and Age descriptions under `Scripts/`.) Like the native
client, the browser client logs in to a shard, downloads the game's files,
scripts included, from the shard's own file server with the game's original
patcher, and keeps them in the browser's storage. An operator can instead serve
a copy of the game files over HTTP. Either way the scripts the client runs come
from the shard or from the operator's game files: in this release a deployment
cannot supply game code of its own.


Status
------

This section lists what has been measured up to v0.1. Everything below was
measured on one Apple-silicon Mac running macOS. The shards were test shards
running [Dirtsand](https://github.com/H-uru/dirtsand).

### Browsers

| Browser | Tested | Result |
|---|---|---|
| Chrome: Chromium 156 (Chrome for Testing) | every check below | works |
| Firefox 157 | once, in October 2026; not in the regular checks | start-up, an online session in Relto and Eder Gira, sound and input work |
| Safari 27 | once, in October 2026; not in the regular checks | start-up, an online session in Relto and Eder Gira, sound and input work |

Before it loads the client, the page checks that the browser has cross-origin
isolation, JavaScript Promise Integration, WebGPU and canvas transfer to a
worker, and lists what is missing
([Deploying.md, "Browser requirements"](Docs/Web/Deploying.md#browser-requirements)).
Windows, Linux, phones and tablets have not been tested.

### Ages and journeys

These were played in Chrome in October 2026, online, on test builds of the
client driven by scripted mouse and keyboard input. Each milestone was read
back from the shard after a fresh login. Where a step could not be done, its
cause was traced in the game's scripts and the shard's state, and the step was
repeated in the native client where that was needed to tell a fault of the
port from the game's or the shard's state.

Before the release, a scripted part of this was played again on the release's
own source, in Chrome on the same test shard: login and the player list,
Relto, the link to Eder Gira, a second login that finds the first session's
state, the intro movies and the game's Quit. The last column says what else
was played again.

| Age or feature | Played in the browser (Python 3 client) | Played again before the release |
|---|---|---|
| A new explorer | to the end: the avatar, the Cleft, the Relto book, Relto | Relto |
| Gahreesen | to the end: the seven cloths, the Bahro cave and the pole | no |
| Teledahn | to the end | no |
| Kadish Tolesa | to the end | no |
| Eder Gira and Eder Kemo | to the end | the link to Eder Gira; both Eder Gira views of the rendering table |
| Ahnonay | to the end; the fourth sphere was played with two players | the link into Ahnonay |
| Minkata and Jalak Dador | to the end | the link into Minkata |
| Phil's Relto | reached through the Spyroom: its journals, blinds, vest and calendar star | no |
| The Path of the Shell | the four poles, the fissure, the Cleft's final vision, K'veer and the Myst library | no |
| Er'cana | the seven cloths and the Bahro cave; the pellet ovens fail with the test shard's game files, in the native client too | its view in the rendering table |
| The Neighborhood | its books, doors, imager and journals; the common room and some items depend on hood state or a KI level the test player did not have | no |
| The Great Zero, the City and the Nexus | as far as the test shard's state lets any client go: the KI marker machine's first upload, 6 of the 15 markers of the first game, the ferry district, the Nexus machine | the Great Zero's view in the rendering table |
| The four Pods | linking, power, lights and Tetsonot's Bahro symbol; the other symbols appear on a schedule and were not played | no |
| The KI | journal notes, pictures and chat; a note kept after the client is closed in the middle of an Age | no |
| Two players | presence, movement, chat, a shared clickable and a Relto visit, with a native client and with a second browser | no |
| Graphics settings | changed in the game's own menu and kept across a reload | no |

The Python 2.3 client was played on a test shard that serves MOULa's own game
code: login, a new explorer, Relto, the Neighborhood, links to Teledahn and
Eder Gira, the KI, and the minigames (Heek with two players, the KI marker
quest, Blue Spiral and Ahnonay's Quabs). The minigame runs logged no Python
tracebacks. Before the release, login, Relto and the link to Eder Gira were
played again there, with no Python tracebacks.

### Rendering compared with the native client

At six fixed camera poses, the browser's frame is compared pixel by pixel with
the native client's (H-uru Plasma on macOS) in the same game state. The frames
are not identical yet. The share of pixels that differ by more than 16 of 255:

| Pose | Pixels that differ by more than 16 |
|---|---|
| Er'cana | 0.6 % |
| Eder Gira, two views | 2.6 % and 3.3 % |
| The Great Zero | 9.8 % |
| Eder Tsogal | 17.9 % |
| Relto | 31.5 % |

Measured again before the release, every pose gave the same frame, pixel for
pixel. The Ages are handcrafted, so any visible difference from the native
client is a bug to fix.

### Frame rate and memory

On the measured route (Relto and Eder Gira, an 800 by 600 view, a 60 Hz
display), the client draws a median of 60 frames per second in every phase and
at least 58.9 in every one-second window inside an Age. The longest frame
inside an Age took 35 ms; while a link loads, one frame can take up to 0.63 s.

Over eight minutes with three links, the client's WebAssembly memory stays at
491 MiB, and the browser's processes for the page peak at about 2.3 GiB
(2.8 GiB in a private window). These numbers were measured again before the
release.

### Not working, or not known yet

- Firefox and Safari are not part of the regular checks. In Firefox, a
  screenshot of the page's canvas sometimes comes back empty while the client
  keeps drawing; whether the window itself stops updating has not been checked.
- Mouse capture (pointer lock) has not been verified in any browser.
- The rendering differences above.
- The parts of the Ages above that the test shards' state closed are untested,
  and so is the Descent: the way to its book needs shard state that the test
  shard does not have.
- On a shard whose game scripts predate H-uru's fix to `Cleft.py`, the Python 3
  client does not load Tomahna after the Cleft's final vision, as H-uru's
  native client does not. The Python 2.3 client skips the missing pages, as
  Cyan's client does.
- The builder of HD texture packs is not part of this release. The pack format
  is documented in [`Docs/Web/HD texture pack format.md`](Docs/Web/HD%20texture%20pack%20format.md).


Building, running and deploying
-------------------------------

- **The native clients** build as H-uru documents:
  [compiling](https://h-uru.github.io/Plasma/building.html) and
  [running](https://h-uru.github.io/Plasma/running.html). The browser code in
  upstream files is guarded, so the native build is unchanged.
- **The browser client:** [`Docs/Web/Building.md`](Docs/Web/Building.md)
  lists the prerequisites on macOS and on Debian or Ubuntu, and the commands:
  Emscripten, the dependency build `web/build/build-deps.sh` and the client
  build `web/build/build-client.sh` for each flavour, with the time and disk
  each takes. `web/build/sources.lock` pins every download by URL and
  SHA-256. `.github/workflows/web.yml` builds both flavours on Linux and runs
  the web tests.
- **Running locally:** a client build, a shard you can reach (your own
  Dirtsand, for example), and the origin server and gateway on loopback:
  [`Docs/Web/Running locally.md`](Docs/Web/Running%20locally.md).
- **Tests:** [`Running locally.md`, "The tests"](Docs/Web/Running%20locally.md#the-tests)
  sets up the Python environment and runs `web/tests/run.py`; none of the tests
  needs a shard.
- **Deploying for a shard:** [`Docs/Web/Deploying.md`](Docs/Web/Deploying.md)
  covers the deployment file, the join key, hosting on your own servers or as a
  static kit with the gateway Worker, TLS, limits, logs and fonts.


Licence and source
------------------

OpenAges is free software under the GNU General Public License, version 3, with
the additional permissions in [`COPYING.txt`](COPYING.txt), as H-uru Plasma is.
[`LICENSE.txt`](LICENSE.txt) holds the licence text.

- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists every third-party
  component compiled into the browser clients or served in the page kit, with
  its version, its licence and the licence text. [`CREDITS.txt`](CREDITS.txt)
  holds the engine's credits and the browser client's.
- The client's debug and loading text uses two Microsoft faces, which may not
  be redistributed. The page kit serves free substitutes instead, Liberation
  Mono and DejaVu Sans Bold ([`web/deploy/fonts/README.md`](web/deploy/fonts/README.md)).
- Whoever serves the client conveys its object code. Every deployment therefore
  serves `LICENSE.txt`, `COPYING.txt`, `THIRD_PARTY_NOTICES.md` and
  `source.json`. The page's "Source code (GPLv3)" link reads `source.json`,
  which names the repository and, for a release build, the commit and tag it
  was built from; the page keeps that link and its "Notices" link however it
  ends, also when the browser cannot run the client. A deployment of a
  modified client names its own repository
  ([Deploying.md, "Source code and licence files"](Docs/Web/Deploying.md#source-code-and-licence-files)).
- The third-party sources are pinned in `web/build/sources.lock`. CPython
  2.3.3 is the tree of OpenUru's CWE-ou-minkata repository, pinned by commit.
  `web/build/notices/source_bundle.py` assembles the pinned sources into one
  bundle. Each release publishes that bundle, and a deployment names its
  address in `source.json`.
- The game's Ages, textures and sounds belong to Cyan Worlds and are not
  covered by this licence. This repository does not contain them.


Relation to H-uru
-----------------

OpenAges is a fork of [H-uru/Plasma](https://github.com/H-uru/Plasma). Its
history is H-uru's, followed by the browser port's commits. Browser code in
H-uru's files sits behind `__EMSCRIPTEN__` in C++, `if(EMSCRIPTEN)` in CMake
and `PLASMA_PIPELINE_WGPU` for the renderer's hooks, and the native macOS
client builds from this tree. Where the browser has no equivalent of a
platform file, a separate file takes its place, as
`pnAcSocketEmscripten.cpp` takes the place of `pnAcSocket.cpp`.

OpenAges is not an H-uru project. Report problems with the browser client
here; the engine and the native clients are H-uru's.


Credits
-------

- Cyan Worlds, Inc., who made Myst Online: Uru Live and released its engine
  under the GNU General Public License.
- The H-uru developers, who maintain the engine this port builds on and
  Dirtsand, the shard server it was tested against.
- OpenUru.org, whose CWE-ou-minkata repository holds the CPython 2.3.3 tree
  that the Python 2.3 flavour embeds.
- The projects listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md),
  among them Emscripten, Dawn, CPython and PhysX.

OpenAges is maintained by Oscar Gala.


How it was written
------------------

The browser port, the code OpenAges adds to H-uru Plasma, was written with AI
coding assistants (Claude and Codex). The native C++ client is its
specification: the port follows the engine's own code paths, and a difference
from the native client counts as a bug. The work is checked against measured
results, not by reading alone: the native and browser builds, the tests in
`web/tests`, the picture comparisons with the native client, frame-rate and
memory runs, and the journeys above, played in the browser and, where that
was needed, in the native client too. The status section lists what those
checks cover and what they do not.

Contributions follow [`CONTRIBUTING.md`](CONTRIBUTING.md) and
[`AGENTS.md`](AGENTS.md).
