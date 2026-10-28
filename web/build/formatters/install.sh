#!/usr/bin/env bash
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

# Install the formatters of the port's own Python and JavaScript, at the versions this folder pins, into <dir>:
#   <dir>/venv/bin/ruff                ruff (requirements.txt: pip --require-hashes, wheels only)
#   <dir>/node_modules/.bin/prettier   prettier and eslint, with the globals package eslint.config.mjs reads
#   <dir>/node_modules/.bin/eslint     (package.json, package-lock.json: npm ci checks every package's integrity hash)
#
#   web/build/formatters/install.sh <dir>
#
# Keep <dir> outside the checkout, as the build and dependency directories. The tools only check and rewrite the
# working tree; no build step runs them. Needs Python 3.12 or newer and node 20.19 or newer with npm on the PATH
# (Emscripten's are in <emsdk>/node/<version>/bin). PYTHON and NPM name other ones. npm runs with --ignore-scripts, so
# no package's install script runs. Docs/Web/Building.md, "Formatting", says how to run them.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
[ $# -eq 1 ] || { echo "usage: $0 <dir>" >&2; exit 64; }
mkdir -p "$1"
dir=$(cd "$1" && pwd)
python=${PYTHON:-python3}
npm=${NPM:-npm}

"$python" -m venv "$dir/venv"
"$dir/venv/bin/python" -m pip install --isolated --disable-pip-version-check --no-input \
    --require-hashes --only-binary :all: --no-deps -r "$here/requirements.txt"
cp "$here/package.json" "$here/package-lock.json" "$dir/"
(cd "$dir" && "$npm" ci --ignore-scripts --no-audit --no-fund)

"$dir/venv/bin/ruff" --version
node "$dir/node_modules/prettier/bin/prettier.cjs" --version | sed 's/^/prettier /'
node "$dir/node_modules/eslint/bin/eslint.js" --version | sed 's/^v*/eslint /'
