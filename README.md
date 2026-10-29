OpenAges
========

OpenAges runs the client of Myst Online: Uru Live in a web browser. This
repository is [H-uru's Plasma](https://github.com/H-uru/Plasma), the
GPLv3-licensed CyanWorlds.com Engine, with a port of its client to the browser:
the engine compiled to WebAssembly with Emscripten and drawn with WebGPU.

The browser client is the native client's code: the same engine, the game's own
Python scripts, PhysX, game files and protocols, and a gateway that relays its
WebSockets to the shard's TCP ports. The game's Ages, textures and sounds are
not in this repository: the client downloads them from the shard, as the native
client does, or reads a copy that the operator serves. It builds in two
flavours, for H-uru's Python 3 game scripts and for the original Python 2.3
scripts of Myst Online: Uru Live again (MOULa).
[`Docs/Web/Architecture.md`](Docs/Web/Architecture.md) describes the components.

Website: <https://openages.net>

Myst and Uru are trademarks of Cyan Worlds, Inc. OpenAges is an independent
project and is not affiliated with or endorsed by Cyan Worlds.

Status
------

Up to v0.1, the browser client was played online in Chrome, on test builds
driven by scripted input, against test shards running Dirtsand, on one
Apple-silicon Mac. A new explorer, Gahreesen, Teledahn, Kadish Tolesa, Eder Gira
and Eder Kemo, Ahnonay, and Minkata and Jalak Dador were played to the end; the
other Ages except the Descent, the KI and two players together were played in
part. The Python 2.3 client was played on a test shard that serves MOULa's own
game code. Firefox and Safari were tried once each; Windows, Linux, phones and
tablets have not been tested. The browser's frames are not identical to the
native client's yet. [`Docs/Web/Status.md`](Docs/Web/Status.md) has the full
status, with what is not working or not known yet.

Building, running and deploying
-------------------------------

- **The native clients** build as H-uru documents:
  [compiling](https://h-uru.github.io/Plasma/building.html) and
  [running](https://h-uru.github.io/Plasma/running.html). H-uru's own README,
  kept unchanged in [`Docs/H-uru README.md`](Docs/H-uru%20README.md), lists
  their library dependencies.
- **The browser client:** [`Docs/Web/Building.md`](Docs/Web/Building.md)
  builds it, [`Docs/Web/Running locally.md`](Docs/Web/Running%20locally.md)
  runs a build and the tests on one machine, and
  [`Docs/Web/Deploying.md`](Docs/Web/Deploying.md) deploys it for a shard.

Licence and source
------------------

OpenAges is free software under the GNU General Public License, version 3, with
the additional permissions in [`COPYING.txt`](COPYING.txt), as H-uru Plasma is.
[`LICENSE.txt`](LICENSE.txt) holds the licence text, and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) the third-party components
with their licences. Whoever serves the client conveys its object code, so
every deployment serves those three files and a `source.json` that names the
source it was built from; each release publishes the pinned third-party sources
([Deploying.md, "Source code and licence files"](Docs/Web/Deploying.md#source-code-and-licence-files)).
The game's Ages, textures and sounds belong to Cyan Worlds and are not covered
by this licence.

Relation to H-uru
-----------------

OpenAges is a fork of [H-uru/Plasma](https://github.com/H-uru/Plasma): H-uru's
history, followed by the browser port's commits. Browser code in H-uru's files
is guarded (`__EMSCRIPTEN__`, `if(EMSCRIPTEN)`, `PLASMA_PIPELINE_WGPU`), and
the native macOS client builds from this tree.

OpenAges is not an H-uru project. Report problems with the browser client
here; the engine and the native clients are H-uru's.

Credits
-------

Cyan Worlds, Inc., who made Myst Online: Uru Live and released its engine under
the GNU General Public License; the H-uru developers, who maintain the engine
and Dirtsand; OpenUru.org, whose CWE-ou-minkata repository holds the CPython
2.3.3 tree of the Python 2.3 flavour; and the projects in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), among them Emscripten,
Dawn, CPython and PhysX. [`CREDITS.txt`](CREDITS.txt) has the full credits.

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
