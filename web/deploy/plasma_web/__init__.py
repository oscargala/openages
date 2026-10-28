# /*==LICENSE==*
#
# CyanWorlds.com Engine - MMOG client, server and tools
# Copyright (C) 2011  Cyan Worlds, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Additional permissions under GNU GPL version 3 section 7
#
# If you modify this Program, or any covered work, by linking or
# combining it with any of RAD Game Tools Bink SDK, Autodesk 3ds Max SDK,
# NVIDIA PhysX SDK, Microsoft DirectX SDK, OpenSSL library, Independent
# JPEG Group JPEG library, Microsoft Windows Media SDK, or Apple QuickTime SDK
# (or a modified version of those libraries),
# containing parts covered by the terms of the Bink SDK EULA, 3ds Max EULA,
# PhysX SDK EULA, DirectX SDK EULA, OpenSSL and SSLeay licenses, IJG
# JPEG Library README, Windows Media SDK EULA, or QuickTime SDK EULA, the
# licensors of this Program grant you additional
# permission to convey the resulting work. Corresponding Source for a
# non-source form of such a combination shall include the source code for
# the parts of OpenSSL and IJG JPEG Library used as well as that of the covered
# work.
#
# You can contact Cyan Worlds, Inc. by email legal@cyan.com
#  or by snail mail at:
#       Cyan Worlds, Inc.
#       14617 N Newport Hwy
#       Mead, WA   99021
#
# *==LICENSE==*/
"""The browser client's web product: the origin server, the WebSocket gateway and the deployment tooling.

Modules:
  server            the origin server: the player page, the client build, /runtime/config.json, /runtime/join,
                    the game data, fonts and HD texture packs (plasma-web-server)
  asset_catalog, representations
                    the server's asset index and its cached, validated file bodies
  gateway           the WebSocket-to-TCP relay to the shard, with join-token admission and per-client limits
                    (plasma-web-gateway)
  join_token, client_limits
                    the gateway's admission tokens and its per-client rate tables (the origin server uses both)
  deployment        the operator's deployment file: validation, the page's document, the gateway's upstreams
                    (plasma-web-deployment)
  server_ini, origins, manifest
                    the rules the servers and the deployment tooling share: the shard's server.ini grammar, the
                    page origins a server admits, and the .fetch-manifest listing of a fetch tree
  emit_static       a static-host kit for one deployment (plasma-web-emit-static)
  provenance        source.json, the licence files and the notices every kit and server carries
  analyse_gamecode  a python.pak's marshal families, which deployment uses to refuse a pak the build cannot run
                    (plasma-web-analyse-gamecode)

The package runs from a checkout of the repository: the origin server and the static kit serve the checkout's player
page, its runtime configuration module and input modules, and the free fonts. ROOT is that checkout, and the paths
below are the checkout files the package reads. Run it with the package directory (web/deploy) on the import path:
an editable install (pip install -e web/deploy, which also installs the commands named above), PYTHONPATH=web/deploy,
or python -m from web/deploy.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]  # the checkout (web/deploy/plasma_web/__init__.py)
PAGE_DIR = ROOT / "Sources/Plasma/Apps/plClient/emscripten/shell"  # the player page: index.html, boot.mjs
CONFIG_MJS = PAGE_DIR / "config.mjs"  # the page's runtime configuration module
INPUT_MODULES = ROOT / "Sources/Plasma/Apps/plClient/emscripten/input"  # the page's input modules
FONTS_DIR = ROOT / "web/deploy/fonts"  # the free substitute fonts and their licences
TEST_HOOKS_DIR = ROOT / "testhooks"  # the test overlay, which no kit may carry
