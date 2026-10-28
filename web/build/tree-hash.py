#!/usr/bin/env python3
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
"""Identify a source tree by content: its git tree id and a SHA-256 file manifest.

    tree-hash.py <dir>                     print "tree=<git tree id> manifest-sha256=<digest> files=<n>"
    tree-hash.py <dir> --manifest <out>    also write the manifest
    tree-hash.py <dir> --expect-tree <id> --expect-manifest <digest>
                                           exit 1 unless both match

Some build inputs are not release archives but directories inside a git
repository (the Cypython 2.3.3 tree of the py2 flavour). web/build/sources.lock pins such a
directory by two digests, and the build verifies both before it reads the tree:
- the git tree id, which is what `git rev-parse <commit>:<path>` prints for the pinned
  commit, so anyone with the repository can check the pin without this script;
- the SHA-256 of a manifest with one line per file, "<git mode> <sha256> <path>", in byte
  order of the path, so the pin does not rest on SHA-1 alone.

Git's rules are followed: files are 100644, or 100755 when the owner may execute them;
empty directories do not count; anything that is not a regular file or a directory is
refused, so a symbolic link cannot point the build at another tree.
"""

import argparse
import hashlib
import os
import stat
import sys


def scan(root):
    """Return (git tree id, manifest lines) for the directory root."""
    lines = []

    def tree(path, rel):
        entries = []
        for name in os.listdir(path):
            full = os.path.join(path, name)
            st = os.lstat(full)
            relname = rel + name
            if stat.S_ISDIR(st.st_mode):
                sub = tree(full, relname + "/")
                if sub is not None:
                    entries.append((name + "/", b"40000 " + name.encode() + b"\0" + sub))
            elif stat.S_ISREG(st.st_mode):
                mode = b"100755" if st.st_mode & stat.S_IXUSR else b"100644"
                data = open(full, "rb").read()
                blob = hashlib.sha1(b"blob %d\0" % len(data) + data).digest()
                entries.append((name, mode + b" " + name.encode() + b"\0" + blob))
                lines.append(
                    (
                        relname.encode(),
                        b"%s %s %s" % (mode, hashlib.sha256(data).hexdigest().encode(), relname.encode()),
                    )
                )
            else:
                raise SystemExit("tree-hash: not a regular file or directory: " + full)
        if not entries:
            return None
        # git orders tree entries by name, a directory's name compared as if it ended in "/"
        body = b"".join(e for _, e in sorted(entries, key=lambda e: e[0].encode()))
        return hashlib.sha1(b"tree %d\0" % len(body) + body).digest()

    top = tree(root, "")
    if top is None:
        raise SystemExit("tree-hash: no files under " + root)
    manifest = b"".join(line + b"\n" for _, line in sorted(lines))
    return top.hex(), manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--manifest", help="write the manifest to this file")
    ap.add_argument("--expect-tree")
    ap.add_argument("--expect-manifest")
    a = ap.parse_args()
    if not os.path.isdir(a.dir):
        ap.error("not a directory: " + a.dir)
    tree_id, manifest = scan(a.dir)
    digest = hashlib.sha256(manifest).hexdigest()
    if a.manifest:
        with open(a.manifest, "wb") as f:
            f.write(manifest)
    print("tree=%s manifest-sha256=%s files=%d" % (tree_id, digest, manifest.count(b"\n")))
    bad = [
        what
        for what, want, got in (("tree", a.expect_tree, tree_id), ("manifest", a.expect_manifest, digest))
        if want and want != got
    ]
    if bad:
        print("tree-hash: %s does not match the pin (%s)" % (a.dir, ", ".join(bad)), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
