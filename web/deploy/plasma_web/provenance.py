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
"""What a page kit says about its own source: source.json, the licence files and the kit's notices.

The browser client and its page are under the GNU GPL version 3 (LICENSE.txt) with the additional permissions in
COPYING.txt. Whoever serves them conveys object code, so every kit carries the licence, the third-party notices and a
pointer to the exact source. emit_static.py writes these into each kit and server.py serves the same documents:

  LICENSE.txt             the GPL version 3, the repository's own file
  COPYING.txt             the engine's licence notice and its section 7 permissions, the repository's own file
  THIRD_PARTY_NOTICES.md  the repository's top-level notices (generated from web/build/sources.lock), then a section on
                          the fonts this kit serves
  source.json             where the source of this page and client is published (below)

source.json (schema 1):
  repository   the public repository, from the deployment's source.repository; null when the deployment names none
  commit       the commit the client and the page files were built from: only a release build records one
               (build-client.sh RELEASE=1 writes <client>.release.json beside the client); otherwise null
  tag          the tag at that commit: the release record's (git describe --exact-match --tags at build time), or the
               deployment's source.tag, which must name that commit in this checkout; null when there is none
  url          the page's "Source code (GPLv3)" link: <repository>/tree/<tag, else commit>; for a kit without a
               release build, the repository itself; null without a repository
  sourcesLock  the SHA-256 of web/build/sources.lock, which pins every third-party source of the build
  build        the client the kit serves: whether it is a release build, its name, Python ABI and file digests
  notes        what the document cannot say, in words (no tag, no commit, no repository)

Nothing is claimed that is not checked. A release record must describe the client files beside it byte for byte,
and the checkout that supplies the page files must be at the record's commit with no change that is not committed:
otherwise the kit is refused, since its page would not be the published source either. A tag named by a deployment
must resolve to the record's commit, and without a release record no tag is accepted at all.
"""

import hashlib
import json
import re
import subprocess
from pathlib import Path

from plasma_web import ROOT

SCHEMA = 1
LICENCE = "GPL-3.0-or-later, with the additional permissions of GPL section 7 in COPYING.txt"
LICENCE_FILES = ("LICENSE.txt", "COPYING.txt")
NOTICES = "THIRD_PARTY_NOTICES.md"
LOCK = "web/build/sources.lock"
RELEASE_SUFFIX = ".release.json"
COMMIT = re.compile(r"[0-9a-f]{40}")
TAG = re.compile(r"[A-Za-z0-9][A-Za-z0-9._/+-]{0,127}")


class ProvenanceError(ValueError):
    pass


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _git(root, *args):
    r = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)
    return r.returncode, r.stdout.strip()


def checkout_state(root=ROOT):
    """{commit, clean, status}: HEAD of the checkout at root and whether every change is committed (untracked files
    count, ignored ones do not), as build-client.sh RELEASE=1 and cmake/BuildInfo.cmake test it. commit is None
    outside a git checkout."""
    rc, head = _git(root, "rev-parse", "--verify", "HEAD")
    if rc or not COMMIT.fullmatch(head):
        return {"commit": None, "clean": False, "status": ["not a git checkout"]}
    rc, status = _git(root, "status", "--porcelain", "--untracked-files=normal", "--ignore-submodules=none")
    if rc:
        return {"commit": head, "clean": False, "status": ["git status failed"]}
    lines = status.splitlines()
    return {"commit": head, "clean": not lines, "status": lines}


def release_record(build_files):
    """The release record beside a client (build-client.sh RELEASE=1), or None for an ordinary build. build_files are
    the client's two files (deployment.client_build_files). A record that does not describe them byte for byte is
    refused: the build changed after it, or it belongs to another build."""
    if not build_files:
        return None
    js = next(f for f in build_files if f.suffix == ".js")
    path = js.with_name(js.stem + RELEASE_SUFFIX)
    if not path.is_file():
        return None
    try:
        rec = json.loads(path.read_bytes())
    except ValueError as e:
        raise ProvenanceError(f"{path}: not JSON ({e})")
    if not isinstance(rec, dict) or rec.get("schema") != SCHEMA or not COMMIT.fullmatch(str(rec.get("commit"))):
        raise ProvenanceError(f"{path}: not a schema-1 release record with a commit")
    if rec.get("tag") is not None and not TAG.fullmatch(str(rec["tag"])):
        raise ProvenanceError(f"{path}: invalid tag {rec['tag']!r}")
    files = rec.get("files")
    if not isinstance(files, dict) or set(files) != {f.name for f in build_files}:
        raise ProvenanceError(f"{path}: names other files than {sorted(f.name for f in build_files)}")
    for f in build_files:
        want = files[f.name]
        if not isinstance(want, dict) or want.get("bytes") != f.stat().st_size or want.get("sha256") != sha256(f):
            raise ProvenanceError(
                f"{path}: {f.name} is not the file this release record describes; the build "
                "changed after its release record was written"
            )
    lock = rec.get("sourcesLock")
    if (
        not isinstance(lock, dict)
        or lock.get("path") != LOCK
        or not re.fullmatch(r"[0-9a-f]{64}", str(lock.get("sha256")))
    ):
        raise ProvenanceError(f"{path}: no SHA-256 of {LOCK}")
    rec["path"] = str(path)
    return rec


def _tag_commit(root, tag):
    rc, commit = _git(root, "rev-parse", "--verify", "--quiet", f"refs/tags/{tag}^{{commit}}")
    return commit if rc == 0 and COMMIT.fullmatch(commit) else None


def source_document(
    repository, tag, build_files, build_name=None, python_abi=None, third_party_sources=None, root=ROOT
):
    """source.json for a kit or a served page (see the module docstring). repository, tag and third_party_sources are
    the deployment's source entry (deployment.py); build_files the client files served, or () for a kit without
    one. root is the checkout that supplies the page files. Raises ProvenanceError for a claim that does not hold."""
    record = release_record(build_files)
    notes = []
    commit = None
    if record:
        state = checkout_state(root)
        if state["commit"] != record["commit"]:
            raise ProvenanceError(
                f"the release build {record['path']} is of commit {record['commit']}, but the page files come from "
                f"{root} at {state['commit'] or 'no commit'}: a release kit takes its page from the same commit"
            )
        if not state["clean"]:
            raise ProvenanceError(
                f"{root} has changes that are not committed, so the page files are not those of release commit "
                f"{record['commit']}: " + "; ".join(state["status"][:8])
            )
        if sha256(Path(root) / LOCK) != record["sourcesLock"]["sha256"]:
            raise ProvenanceError(f"{LOCK} is not the one the release build recorded")
        commit = record["commit"]
        if record.get("tag") and tag and tag != record["tag"]:
            raise ProvenanceError(f"source.tag {tag} is not the tag {record['tag']} the release build was made at")
        if tag and not record.get("tag") and _tag_commit(root, tag) != commit:
            raise ProvenanceError(f"source.tag {tag} does not name the release commit {commit} in {root}")
        tag = tag or record.get("tag")
        lock_sha = record["sourcesLock"]["sha256"]
        if not tag:
            notes.append("This release has no tag: the source link names its commit.")
    else:
        if tag:
            raise ProvenanceError(
                f"source.tag {tag}: only a release build (build-client.sh RELEASE=1) records the "
                "commit a tag must name, and this kit has none"
            )
        lock_sha = sha256(Path(root) / LOCK)
        notes.append(
            "This kit has no release build (build-client.sh RELEASE=1), so it records no commit or tag; the "
            "source link names the repository only."
            if build_files
            else "This kit carries no client build, so it records no commit or tag; the source link names the "
            "repository only."
        )
    if repository:
        url = f"{repository}/tree/{tag or commit}" if commit else repository
    else:
        url = None
        notes.append(
            "The deployment names no repository (source.repository), so this page links to no source. Whoever "
            "serves it must offer the Corresponding Source another way (GPL version 3, section 6)."
        )
    build = None
    if build_files:
        build = {
            "release": bool(record),
            "client": build_name,
            "pythonAbi": python_abi,
            "files": {f.name: {"bytes": f.stat().st_size, "sha256": sha256(f)} for f in build_files},
        }
        if record:
            build["productTag"] = record.get("productTag")
    doc = {
        "schema": SCHEMA,
        "licence": LICENCE,
        "repository": repository,
        "tag": tag,
        "commit": commit,
        "url": url,
        "sourcesLock": {"path": LOCK, "sha256": lock_sha},
        "build": build,
        "thirdPartySources": third_party_sources,
        "notes": notes,
    }
    return doc


def kit_notices(fonts_source, fonts_url, font_files, free_font_notices, font_file_name, root=ROOT):
    """THIRD_PARTY_NOTICES.md of a kit: the repository's file, unchanged, then what this kit serves of the "Page kit"
    fonts (the deployment's fonts: the free substitutes, the operator's own files, or none, read from fonts.url)."""
    text = (Path(root) / NOTICES).read_text().rstrip("\n") + "\n"
    lines = [
        "",
        "## This kit",
        "",
        'This kit carries the page files and, when it has one, the client build described under "Browser client" '
        "above, with `LICENSE.txt`, `COPYING.txt` and `source.json` (where their source is published). Its fonts:",
        "",
    ]
    if fonts_url:
        lines += [
            f"- This page reads the fonts of its debug and loading text from {fonts_url}, which the operator "
            "serves; they are not part of this kit.",
            "",
        ]
    elif fonts_source == "free":
        for n in free_font_notices:
            lines += [
                f"### fonts/{font_file_name(n['face'])}: {n['font']}",
                "",
                f"- Font: {n['font']}, unmodified file `{n['file']}` of {n['release']} ({n['url']}), served "
                f"under the name the client asks for. It is not {n['face']}.",
                f"- Copyright: {n['copyright']}",
                f"- Licence: {n['licence']}. The full text is in fonts/{n['licence_file']}.",
                "",
            ]
    elif fonts_source == "host":
        lines += [
            "- This test harness page serves this host's own copies of the original faces (fonts.hostCopies), "
            "which are licensed for this machine and never redistributed.",
            "",
        ]
    else:
        lines += [
            'This kit does not serve the free fonts described under "Page kit" above; it serves the operator\'s '
            "own files:",
            "",
        ]
        for served, src in font_files.items():
            lines += [
                f"- fonts/{served}: the operator's file `{Path(src).name}`, under the operator's own licence.",
                "",
            ]
    return text + "\n".join(lines)
