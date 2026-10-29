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
"""The static-host kit of one deployment (e.g. Cloudflare Pages). Writes files only; publishes nothing.

  emit_static.py <deployment.json> <out dir> --gateway-url wss://<gateway>/ [--build-dir <plClient build dir>]

out/
  index.html, boot.mjs, runtime/config.mjs,       the page and its logic, unchanged (the page loads /runtime/config.json)
  input/*.mjs
  build/plClient.{js,wasm}                          the client, when --build-dir is given (a deployment with
                                                    data.gameCode server-py2 ships build/plClient-py2.{js,wasm}, the
                                                    PLASMA_PYTHON_ABI=2 build, instead; the page loads that name).
                                                    Only a product build: a client built with PLASMA_TEST_HOOKS=ON
                                                    (build-client.sh HARNESS=1) is refused
  runtime/config.json, runtime/server.ini           the deployment document and the shard's own server.ini
                                                    (same bytes the SHA-256 pins; public values only). When the ini
                                                    names a Server.Status URL, the document's shard.status is
                                                    /runtime/status on the page's own origin, the relay the gateway
                                                    Worker answers (as server.py does), so the Content-Security-Policy
                                                    needs no other origin for it
  _headers                                          COOP/COEP/CORP, the Content-Security-Policy (deployment.py
                                                    content_security_policy) and cache policy in the Pages
                                                    _headers format
  fonts/                                            the fonts tree of the debug and loading text, when the page reads
                                                    it from its own origin (no fonts.url): each face as <face>.ttf
                                                    ("Courier New.ttf", "Trebuchet MS Bold.ttf") from the deployment's
                                                    face -> file map, with its .fetch-manifest; by default the free
                                                    substitutes (web/deploy/fonts) and their licence files
  LICENSE.txt, COPYING.txt                          the GPL version 3 and the engine's licence notice
                                                    with its section 7 permissions (the repository's own files)
  THIRD_PARTY_NOTICES.md                            the repository's third-party notices (every component of the
                                                    client and the page kit, with its licence text), then the fonts
                                                    this kit serves (provenance.py kit_notices)
  source.json                                       where the source of this page and client is published: the
                                                    deployment's source.repository, and, for a release build
                                                    (build-client.sh RELEASE=1), its commit and tag, which the page's
                                                    "Source code (GPLv3)" link names (provenance.py). A release build
                                                    is refused unless this checkout is at its commit with every
                                                    change committed: the page files must be that commit's too
and, next to <out dir> (not inside the kit), <out dir>.publish-report.json: every kit file's size against the
25 MiB per-file limit of Pages, and the size classes of the deployment's game trees.

The page document is the same derivation server.py --deployment serves (deployment.py public_document); the page
fetches it from its own origin, no server-side templating. Game data and sound are not copied: they come through the
original patcher over the shard's file server (data.files patcher), from the deployment's data/sound URLs (another
origin, CORS, under the same COEP rule), or from a tree served next to the page by a dev server. The fonts are copied
unless the deployment names fonts.url.

A kit carries no game code of its own. The client runs the shard's (data.gameCode server, or server-py2 for MOULa); the
client-side modes (client, client-python) are modes of the private test harness, outside a release, and refused, as are
harness and gameCodeCapture deployments. The high-resolution texture packs are not copied either (textures.hdPack).
"""

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

from plasma_web import CONFIG_MJS, INPUT_MODULES, PAGE_DIR, ROOT, TEST_HOOKS_DIR
from plasma_web import deployment as deployment_file
from plasma_web import manifest  # the .fetch-manifest writer
from plasma_web import provenance

PAGES_FILE_LIMIT = 25 * 1024 * 1024
# The same-origin relay of the ini's Server.Status URL (server.py /runtime/status, the Worker's statusEndpoint)
STATUS_RELAY = "/runtime/status"
HEADERS = """# The kit's response headers, for a static host that reads a Pages-style _headers file.
# crossOriginIsolated (SharedArrayBuffer for the client's pthreads) needs COOP same-origin + COEP require-corp on
# the page; every subresource is same-origin or CORS-approved (asset origins send Access-Control-Allow-Origin).
# The Content-Security-Policy lets the page and the client's workers connect only to this deployment's own gateway,
# join, status and asset origins (deployment.py content_security_policy), the same header server.py sends. The join
# endpoint and the status relay (/runtime/join, /runtime/status) are on the page's own origin: the gateway Worker
# answers them, as server.py does.
# The cache policy is server.py's (common_headers) for every kit file: the page, its modules, the client build and the
# provenance files revalidate by ETag (no-cache); /runtime/* and the fonts tree are no-store. The fonts tree adds
# no-transform, as server.py's trees do (FETCH_TREES): the client reads it only uncompressed, with strong ETags, and a
# CDN in front of the host (Cloudflare's compresses font/ttf) leaves a no-transform answer as it is.
/*
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp
  Cross-Origin-Resource-Policy: same-origin
  X-Content-Type-Options: nosniff
  Content-Security-Policy: {content_security_policy}
/runtime/*
  Cache-Control: no-store
/fonts/*
  Cache-Control: no-store, no-transform
/build/*
  Cache-Control: no-cache
/
  Cache-Control: no-cache
/index.html
  Cache-Control: no-cache
/boot.mjs
  Cache-Control: no-cache
/input/*
  Cache-Control: no-cache
/source.json
  Cache-Control: no-cache
/LICENSE.txt
  Cache-Control: no-cache
/COPYING.txt
  Cache-Control: no-cache
/THIRD_PARTY_NOTICES.md
  Cache-Control: no-cache
"""
PAGE_FILES = {
    "index.html": PAGE_DIR / "index.html",
    "boot.mjs": PAGE_DIR / "boot.mjs",
    "runtime/config.mjs": CONFIG_MJS,
    "input/capture.mjs": INPUT_MODULES / "capture.mjs",
    "input/transport.mjs": INPUT_MODULES / "transport.mjs",
    "input/fullscreen.mjs": INPUT_MODULES / "fullscreen.mjs",
}
TREES = ["dat", "sfx", "python", "SDL", "avi", "resource.dat"]
# The test-hook overlay (TEST_HOOKS_DIR, testhooks/: the private test harness, its page among it) never
# reaches a kit.


def refuse_test_hook_files(files):
    """Refuses a kit file taken from the test-hook overlay, or named as the test harness page."""
    for rel, src in files.items():
        if Path(src).resolve().is_relative_to(TEST_HOOKS_DIR.resolve()) or Path(rel).name.startswith("harness."):
            raise SystemExit(f"emit_static: {rel} is a test-hook file (the test harness page): never part of a kit")


# A client built with PLASMA_TEST_HOOKS=ON carries the test-hook instruments: a harness build, never a public page.
# Either sign refuses it: the mark the private test harness links into the client, or a true PLASMA_TEST_HOOKS
# entry in the CMakeCache.txt of the build tree the directory belongs to.
TEST_HOOKS_MARK = b"PLASMA_TEST_HOOKS harness build"
CMAKE_TRUE = re.compile(r"(ON|YES|TRUE|Y|[1-9][0-9]*)", re.I)


def refuse_harness_build(build_dir, build_files):
    for f in build_files:
        if f.suffix == ".wasm" and TEST_HOOKS_MARK in f.read_bytes():
            raise deployment_file.DeploymentError(f"{f} is a harness build (PLASMA_TEST_HOOKS=ON), never a public page")
    for d in (Path(build_dir).resolve(), *Path(build_dir).resolve().parents):
        cache = d / "CMakeCache.txt"
        if cache.is_file():
            m = re.search(r"^PLASMA_TEST_HOOKS:BOOL=(.*)$", cache.read_text(errors="replace"), re.M)
            if m and CMAKE_TRUE.fullmatch(m.group(1).strip()):
                raise deployment_file.DeploymentError(
                    f"{d} is configured with PLASMA_TEST_HOOKS={m.group(1).strip()}: a harness build, never a public page"
                )
            return


def emit_fonts(d, out):
    """The kit's fonts tree; returns the publish report's fonts entry. (The kit's THIRD_PARTY_NOTICES.md names them:
    provenance.kit_notices.)

    The page reads its fonts from roots.fonts: fonts.url (another origin, nothing to copy) or /fonts on its own origin,
    which a static host serves from the kit. The client mounts that tree from its .fetch-manifest (manifest.py, the
    origin server's writer: "F <mtime> <name>") and plTextFont opens each face as /fonts/<face>.ttf."""
    if d.fonts_url:
        return {"in_kit": False, "url": d.fonts_url, "source": d.fonts_source}
    (out / "fonts").mkdir(parents=True, exist_ok=True)
    files = {}
    for served, src in d.font_files.items():
        shutil.copyfile(src, out / "fonts" / served)
        files[served] = {
            "from": str(src),
            "bytes": Path(src).stat().st_size,
            "sha256": hashlib.sha256(Path(src).read_bytes()).hexdigest(),
        }
    # the source files' times, as the origin server lists the same tree
    (out / "fonts/.fetch-manifest").write_bytes(manifest.files_manifest(d.font_files))
    if d.fonts_source == "free":
        for n in deployment_file.FREE_FONT_NOTICES:
            shutil.copyfile(deployment_file.FREE_FONTS_DIR / n["licence_file"], out / "fonts" / n["licence_file"])
    return {"in_kit": True, "source": d.fonts_source, "files": files}


def emit_provenance(d, out, source):
    """The licence files, the kit's THIRD_PARTY_NOTICES.md and source.json (provenance.py)."""
    for name in provenance.LICENCE_FILES:
        shutil.copyfile(ROOT / name, out / name)
    (out / provenance.NOTICES).write_text(
        provenance.kit_notices(
            d.fonts_source, d.fonts_url, d.font_files, deployment_file.FREE_FONT_NOTICES, deployment_file.font_file_name
        )
    )
    (out / "source.json").write_text(json.dumps(source, indent=1) + "\n")


def size_classes(root):
    out = {}
    for top in TREES:
        p = Path(root) / top
        files = [p] if p.is_file() else [f for f in p.rglob("*") if f.is_file()] if p.is_dir() else []
        sizes = [(f.stat().st_size, str(f.relative_to(root))) for f in files]
        out[top] = {
            "files": len(sizes),
            "bytes": sum(s for s, _ in sizes),
            "under_1MiB": sum(1 for s, _ in sizes if s < 1 << 20),
            "1_to_25MiB": sum(1 for s, _ in sizes if 1 << 20 <= s <= PAGES_FILE_LIMIT),
            "over_25MiB": sorted(
                [{"path": n, "bytes": s} for s, n in sizes if s > PAGES_FILE_LIMIT], key=lambda x: -x["bytes"]
            ),
            "largest": max(sizes, default=(0, None))[::-1],
        }
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("deployment")
    ap.add_argument("out")
    ap.add_argument("--gateway-url", help="the public wss:// gateway, when the deployment file has none")
    ap.add_argument(
        "--build-dir",
        help="the client build's plClient directory (plClient.js, plClient.wasm; for a "
        "server-py2 deployment the py2 build's, plClient-py2.js, plClient-py2.wasm)",
    )
    a = ap.parse_args()
    try:
        d = deployment_file.load(a.deployment)
        # A registered extension's mode (deployment.py EXTENSIONS: the test harness's client game code) is refused by
        # deployment.load outside a harness deployment, and a harness deployment is refused next; this names the mode
        # for the one that remains.
        if d.game_code not in (None,) + deployment_file.GAME_CODES:
            raise deployment_file.DeploymentError(
                f"data.gameCode {d.game_code}: client-side game code is outside the v0.1 scope and a test harness "
                "mode; a static kit serves only the shard's own game code (server or server-py2)"
            )
        if d.harness:
            raise deployment_file.DeploymentError("a harness deployment is loopback-only, never a static public page")
        if d.game_code_capture:
            raise deployment_file.DeploymentError(
                "capabilities.gameCodeCapture is a loopback diagnostic, never a public page"
            )
        # The status relay: the login dialog reads the ini's Server.Status URL through /runtime/status on the page's
        # own origin (server.py relay_status; in a kit, the gateway Worker's), as server.py's page document names it.
        # A page cannot read that URL across origins, and the kit's Content-Security-Policy names no origin for it.
        doc = d.public_document(a.gateway_url, status_relay=STATUS_RELAY)
        # the deployment's build only, checked to be that Python ABI's build (deployment.ABI_MARKERS)
        build_files = deployment_file.client_build_files(a.build_dir, d.build) if a.build_dir else ()
        if a.build_dir:
            refuse_harness_build(a.build_dir, build_files)
    except (OSError, ValueError) as e:
        ap.error(f"invalid deployment: {e}")
    try:
        source = provenance.source_document(
            d.source_repository,
            d.source_tag,
            build_files,
            deployment_file.CLIENT_NAMES[d.build],
            "2" if d.build == "py2" else "3",
            d.third_party_sources,
        )
    except (OSError, ValueError) as e:
        ap.error(f"source.json: {e}")
    try:
        headers = HEADERS.format(content_security_policy=deployment_file.content_security_policy(doc))
    except ValueError as e:
        ap.error(f"invalid deployment: {e}")
    out = Path(a.out)
    if out.exists() and any(out.iterdir()):
        ap.error(f"{out} is not empty")
    files = dict(PAGE_FILES)
    files.update({f"build/{f.name}": f for f in build_files})
    refuse_test_hook_files(files)
    for rel, src in files.items():
        (out / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, out / rel)
    fonts = emit_fonts(d, out)
    emit_provenance(d, out, source)
    (out / "runtime/config.json").write_text(json.dumps(doc, indent=1) + "\n")
    (out / "runtime/server.ini").write_bytes(d.server_ini_bytes)
    (out / "_headers").write_text(headers)
    kit = {str(p.relative_to(out)): p.stat().st_size for p in sorted(out.rglob("*")) if p.is_file()}
    same_origin = {
        k: v for k, v in doc["roots"].items() if not v.startswith("https://") and not (k == "fonts" and fonts["in_kit"])
    }
    # The page document names /runtime/join (POST, single-use gateway join tokens). A static host cannot answer it: the
    # gateway Worker (web/deploy/worker) must route POST /runtime/join on the page's own hostname.
    report_join = (
        {
            "join_endpoint": doc.get("join"),
            "join_needs": "the gateway Worker on the page origin (Docs/Web/Deploying.md, The Worker)",
        }
        if doc.get("join")
        else {}
    )
    # Nor can it answer /runtime/status, the relay of the ini's Server.Status URL: the Worker does (emit_worker.py
    # passes it the URL).
    report_status = (
        {
            "status_endpoint": doc["shard"]["status"],
            "status_url": d.status_url,
            "status_needs": "the gateway Worker on the page origin (Docs/Web/Deploying.md, The Worker)",
        }
        if doc["shard"].get("status")
        else {}
    )
    # The high-res texture packs are not part of the kit either (CC BY-NC-SA 4.0 art, never from a commercial
    # deployment); a relative base must be served next to the page from textures.hdPack.root.
    report_hdpack = (
        {
            "hdpack": dict(
                doc["textures"]["hdPack"],
                root=str(d.hdpack_root) if d.hdpack_root else None,
                licence="CC BY-NC-SA 4.0, non-commercial deployments only (Docs/Web/Deploying.md, HD texture packs)",
            )
        }
        if doc.get("textures")
        else {}
    )
    report = {
        **report_join,
        **report_status,
        **report_hdpack,
        "deployment": d.name,
        "page_document": doc,
        "fonts": fonts,
        "source": source,
        "kit_files": kit,
        "pages_file_limit": PAGES_FILE_LIMIT,
        "kit_over_limit": sorted(k for k, v in kit.items() if v > PAGES_FILE_LIMIT),
        "roots_served_next_to_the_page": same_origin,
        "game_tree_size_classes": size_classes(d.data_root) if d.data_root else None,
        "note": "Game trees are not part of the kit; a shard's game files reach the client through the original "
        "patcher (data.files patcher), cached per shard in the browser.",
    }
    out.with_name(out.name + ".publish-report.json").write_text(json.dumps(report, indent=1) + "\n")
    print(
        json.dumps({k: report[k] for k in ("kit_files", "kit_over_limit", "roots_served_next_to_the_page")}, indent=1)
    )


if __name__ == "__main__":
    main()
