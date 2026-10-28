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
"""The .fetch-manifest files: the one writer of the listing the client mounts each fetch tree from.

The client's fetch trees (WasmFS fetch directories, Sources/Plasma/Apps/plClient/emscripten/main.cpp MountFetchTree)
are created from a listing that the server publishes beside each tree at <base>/.fetch-manifest, since a fetch directory
cannot list itself. One line per entry, LF-terminated, parents before children:
  D <path>            a directory
  F <mtime> <path>    a file, with its modification time in whole seconds (so Python's .pyc checks behave as on disk)
Paths are relative to the tree's base, with "/" between their parts. The client refuses an empty manifest, so a
listing with no entries is one empty line. The origin server (server.py) writes the data, sound, fonts and game-code
manifests with these functions, and a static kit (emit_static.py) its fonts manifest.
"""

import os


def _text(lines):
    return ("\n".join(lines) + "\n").encode()


def tree_manifest(root, entries):
    """The manifest of root's entries (names directly under root, each a directory or a file): every directory and file
    under them, in sorted order, each path relative to root."""
    lines = []
    for entry in entries:
        top = os.path.join(root, entry)
        if os.path.isdir(top):
            for dirpath, dirnames, filenames in os.walk(top):
                dirnames.sort()
                rel = os.path.relpath(dirpath, root)
                lines.append(f"D {rel}")
                for name in sorted(filenames):
                    full = os.path.join(dirpath, name)
                    lines.append(f"F {int(os.stat(full).st_mtime)} {os.path.join(rel, name)}")
        elif os.path.isfile(top):
            lines.append(f"F {int(os.stat(top).st_mtime)} {entry}")
    return _text(lines)


def subtree_manifest(root):
    """The manifest of everything under root, each path relative to root itself (root's own line left out). A root
    that is a file is listed by its name; a missing root gives the empty listing."""
    lines = [
        line for line in tree_manifest(os.path.dirname(root), [os.path.basename(root)]).decode().split("\n") if line
    ]
    prefix = os.path.basename(root) + "/"
    out = []
    for line in lines:
        kind, _, rest = line.partition(" ")
        if kind == "D":
            if rest != prefix[:-1]:
                out.append(f"D {rest[len(prefix) :]}")
        else:
            mtime, _, path = rest.partition(" ")
            out.append(f"F {mtime} {path[len(prefix) :] if path.startswith(prefix) else path}")
    return _text(out)


def files_manifest(files):
    """The manifest of a flat tree of named files ({served name: the file's path}), in the given order."""
    return _text(f"F {int(os.stat(path).st_mtime)} {name}" for name, path in files.items())
