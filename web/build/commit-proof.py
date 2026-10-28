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
"""Show, without the repository, that a git commit holds a given tree at a given path.

    commit-proof.py make <repository> <commit> <path>   print the proof (reads objects only: git cat-file)
    commit-proof.py check <proof> --commit <id> --path <path> --tree <id>
                                                       exit 1 unless the proof shows <commit>:<path> = <tree>

web/build/sources.lock pins the Cypython 2.3.3 source of the py2 flavour by
the tree's own git tree id, and names the commit and the path that tree comes from in OpenUru's
CWE-ou-minkata repository. The build never contacts that repository, so it cannot ask it whether
the commit holds the tree. A proof is the handful of git objects that link them:
- the commit object, whose first line names the commit's root tree;
- each tree on the path, from the root down to the directory that holds the pinned tree.

Git names every object by the SHA-1 of its type, size and bytes. The check recomputes each
object's id from the proof's text, so a changed byte gives another id. It then follows the path
from the commit to the pinned tree. A proof only checks out for the commit, path and tree it was
made from. The proof's text is git's own: in any clone of the repository,
"git cat-file commit <commit>" prints the commit part and "git ls-tree <id>" each tree part.

Proof format (UTF-8 text; lines starting with "#" are comments):
    commit <id> <size>          then exactly <size> bytes, the commit object, then a newline
    tree <id> <path>            then that tree's entries, one per line, as git ls-tree prints them:
    <mode> <type> <id><TAB><name>
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys

ENTRY = re.compile(r"(\d{6}) (blob|tree|commit) ([0-9a-f]{40})\t(.+)")
OID = re.compile(r"[0-9a-f]{40}")
# names git ls-tree prints unquoted, so that a proof line is exactly its output
PLAIN_NAME = re.compile(r"[\x21\x23-\x5b\x5d-\x7e][\x20\x21\x23-\x5b\x5d-\x7e]*")


class ProofError(Exception):
    pass


def oid(kind, body):
    return hashlib.sha1(b"%s %d\0" % (kind, len(body)) + body).hexdigest()


def tree_body(entries):
    """The tree object's bytes from (mode, type, id, name) entries in their stored order."""
    # a tree's mode is stored without its leading zero (40000); git ls-tree pads it to six digits
    return b"".join(
        b"%s %s\0%s" % (format(int(mode, 8), "o").encode(), name.encode(), bytes.fromhex(ident))
        for mode, _, ident, name in entries
    )


def parse_tree_body(body):
    entries, i = [], 0
    while i < len(body):
        sp = body.index(b" ", i)
        nul = body.index(b"\0", sp)
        mode, name, ident = body[i:sp].decode(), body[sp + 1 : nul].decode("utf-8"), body[nul + 1 : nul + 21].hex()
        i = nul + 21
        kind = {"40000": "tree", "160000": "commit"}.get(mode, "blob")
        entries.append(("%06d" % int(mode), kind, ident, name))
    return entries


def git(repo, *args):
    env = dict(os.environ, GIT_OPTIONAL_LOCKS="0")
    return subprocess.run(["git", "-C", repo] + list(args), check=True, capture_output=True, env=env).stdout


def make(repo, commit, path):
    if not OID.fullmatch(commit):
        raise ProofError("give the full 40-digit commit id")
    body = git(repo, "cat-file", "commit", commit)
    if oid(b"commit", body) != commit:
        raise ProofError("git cat-file commit %s gave another object" % commit)
    out = [b"commit %s %d\n" % (commit.encode(), len(body)), body, b"\n"]
    ident = body.split(b"\n", 1)[0].removeprefix(b"tree ").decode()
    walked = ""
    for part in [p for p in path.split("/") if p]:
        raw = git(repo, "cat-file", "tree", ident)
        entries = parse_tree_body(raw)
        if tree_body(entries) != raw:
            raise ProofError("tree %s does not round-trip" % ident)
        bad = [name for *_, name in entries if not PLAIN_NAME.fullmatch(name)]
        if bad:
            raise ProofError("tree %s has a name git ls-tree would quote: %r" % (ident, bad[0]))
        out.append(b"tree %s %s\n" % (ident.encode(), (walked or ".").encode()))
        out += [b"%s %s %s\t%s\n" % tuple(x.encode() for x in e) for e in entries]
        nxt = [e for e in entries if e[3] == part and e[1] == "tree"]
        if not nxt:
            raise ProofError("no directory %r in %s" % (part, walked or "the root tree"))
        ident = nxt[0][2]
        walked = walked + "/" + part if walked else part
    return b"".join(out), ident


def parse(data):
    """{id: (kind, body)} of the objects in a proof, each checked against its id; the commit's id first."""
    objects, first, i = {}, None, 0
    while i < len(data):
        end = data.find(b"\n", i)
        end = len(data) if end < 0 else end
        line = data[i:end].decode("utf-8")
        i = end + 1
        if not line or line.startswith("#"):
            continue
        m = re.fullmatch(r"commit ([0-9a-f]{40}) (\d+)", line)
        if m:
            size = int(m.group(2))
            body = data[i : i + size]
            if len(body) != size or data[i + size : i + size + 1] != b"\n":
                raise ProofError("commit %s: the proof ends inside the object" % m.group(1))
            i += size + 1
            ident, kind = m.group(1), b"commit"
        else:
            m = re.fullmatch(r"tree ([0-9a-f]{40}) (\S+)", line)
            if not m:
                raise ProofError("unexpected line: %r" % line[:80])
            entries = []
            while i < len(data) and not data.startswith((b"tree ", b"commit "), i):
                end = data.find(b"\n", i)
                end = len(data) if end < 0 else end
                text = data[i:end].decode("utf-8")
                i = end + 1
                if not text or text.startswith("#"):
                    continue
                e = ENTRY.fullmatch(text)
                if not e:
                    raise ProofError("tree %s: unexpected entry %r" % (m.group(1), text[:80]))
                entries.append(e.groups())
            ident, kind, body = m.group(1), b"tree", tree_body(entries)
        got = oid(kind, body)
        if got != ident:
            raise ProofError("the proof's %s %s hashes to %s: its text was changed" % (kind.decode(), ident, got))
        if ident in objects:
            raise ProofError("%s appears twice" % ident)
        objects[ident] = (kind, body)
        first = first or ident
    return objects, first


def check(data, commit, path, tree):
    objects, first = parse(data)
    if first != commit or objects[first][0] != b"commit":
        raise ProofError("the proof is for commit %s, not %s" % (first, commit))
    head = objects[commit][1].split(b"\n", 1)[0]
    if not head.startswith(b"tree "):
        raise ProofError("commit %s names no tree" % commit)
    ident, used, walked = head[5:].decode(), {commit}, ""
    for part in [p for p in path.split("/") if p]:
        if ident not in objects or objects[ident][0] != b"tree":
            raise ProofError("the proof lacks tree %s (%s)" % (ident, walked or "the root"))
        used.add(ident)
        nxt = [e for e in parse_tree_body(objects[ident][1]) if e[3] == part and e[1] == "tree"]
        if not nxt:
            raise ProofError("commit %s has no directory %s" % (commit, (walked + "/" if walked else "") + part))
        ident = nxt[0][2]
        walked = walked + "/" + part if walked else part
    if ident != tree:
        raise ProofError("%s:%s is tree %s, not %s" % (commit, path, ident, tree))
    extra = sorted(set(objects) - used)
    if extra:
        raise ProofError("the proof holds objects off the path: %s" % ", ".join(extra))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    mk = sub.add_parser("make", help="print the proof of <commit>:<path> from a local clone")
    mk.add_argument("repository")
    mk.add_argument("commit")
    mk.add_argument("path")
    mk.add_argument("--header", help="a file whose lines are written first, as comments")
    ck = sub.add_parser("check", help="check a proof against the pin")
    ck.add_argument("proof")
    ck.add_argument("--commit", required=True)
    ck.add_argument("--path", required=True)
    ck.add_argument("--tree", required=True)
    a = ap.parse_args()
    try:
        if a.cmd == "make":
            proof, tree = make(a.repository, a.commit, a.path)
            check(proof, a.commit, a.path, tree)
            head = b""
            if a.header:
                head = b"".join(
                    b"# " + l.rstrip().encode() + b"\n" if l.strip() else b"#\n"
                    for l in open(a.header, encoding="utf-8")
                )
            sys.stdout.buffer.write(head + proof)
            print("commit-proof: %s:%s is tree %s" % (a.commit, a.path, tree), file=sys.stderr)
        else:
            check(open(a.proof, "rb").read(), a.commit, a.path, a.tree)
            print("commit-proof: %s:%s is tree %s" % (a.commit, a.path, a.tree))
    except (ProofError, UnicodeDecodeError, ValueError) as e:
        print("commit-proof: %s" % e, file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as e:
        print("commit-proof: git failed: %s" % e.stderr.decode(errors="replace").strip(), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
