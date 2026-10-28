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
"""Run the web product's tests, each suite in its own interpreter, and summarise them.

    python3 web/tests/run.py [--build-dir DIR] [--out DIR] [suite ...]

With no suite named it runs every suite below. Each runs with the package (web/deploy) and this folder on PYTHONPATH,
as the suites import them, and with bytecode off (python -B), so a run leaves nothing in the tree. The exit status is
0 when every suite passes; a suite whose prerequisite is missing skips the checks that need it, and says which: the
unittest suites run verbose (-v), so each test and each skip's reason is in the output.

Prerequisites, from a clean clone:
  Python 3.13 with this folder's requirements:   python3 -m pip install -r web/tests/requirements.txt
  Playwright's Chromium, for test_config:        python3 -m playwright install chromium
  node on PATH (the page's loader and shared rules run under it), openssl (fixture certificates), and for the Worker's
  backend of test_contract its pinned wrangler (PLASMA_WRANGLER, or wrangler on PATH; without it the gateway backend
  runs alone).
  A built client, for the three test_config checks that read one: --build-dir, or PLASMA_WASM_BUILD, the build
  directory of web/build/build-client.sh. Without it those checks are skipped.
None needs a shard, MOULa data or a host path: every fixture is made in a temporary folder and removed.

  test_analyse_gamecode  analyse_gamecode on synthetic inputs
  test_rules             the rules kept in one module each, and the page's copies of two of them (under node)
  test_deployment        the operator deployment file and its consumers: the servers, the gateway, the static kit
  test_origin_server     the origin server's connection handling and response policy
  test_gateway           the gateway's HTTP/WS boundary, byte transport and admission limits
  test_admission         gateway admission and the page origin's /runtime/join, through the real processes
  test_tls               the gateway and the origin server with their own TLS (a fixture CA, nothing installed)
  test_contract          the gateway contract against gateway.py and the Worker (web/deploy/worker, wrangler dev)
  test_config            the deployment loader, the player page and the origin server's policy, in Chromium
  wgsl_regen             the WGSL generated from the upstream D3D9 shaders (d3d9_to_wgsl.py --check) is current

--out DIR keeps test_config's evidence (screenshots, JSON, summary.json) under DIR, and each suite's output in
DIR/<suite>.txt.
--require all (or a comma list of node, openssl, playwright, chromium, wrangler, build) turns a missing prerequisite
into an error (exit 2) before any suite runs, so a run that must cover everything (CI) cannot pass by skipping.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
WORKER = ROOT / "web/deploy/worker"
REGEN = ROOT / "Sources/Plasma/FeatureLib/pfWGPUPipeline/ShaderSrc/gen/d3d9_to_wgsl.py"
# suite -> (its script, its working directory)
SUITES = {
    name: (HERE / f"{name}.py", HERE)
    for name in (
        "test_analyse_gamecode",
        "test_rules",
        "test_deployment",
        "test_origin_server",
        "test_gateway",
        "test_admission",
        "test_tls",
    )
}
SUITES["test_contract"] = (WORKER / "test_contract.py", WORKER)
SUITES["test_config"] = (HERE / "test_config.py", HERE)
SUITES["wgsl_regen"] = (REGEN, ROOT)
PREREQUISITES = ("node", "openssl", "playwright", "chromium", "wrangler", "build")
RAN = re.compile(r"^Ran (\d+) tests? in ")
RESULT = re.compile(r"^(OK|FAILED)\b(?: \((.*)\))?$")


def prerequisites():
    """What this host lacks, as the suites would report it: {name: reason}."""
    missing = {}
    if not shutil.which("node"):
        missing["node"] = "node is not on PATH: the checks that run the page's modules under node are skipped"
    if not shutil.which("openssl"):
        missing["openssl"] = "openssl is not on PATH: the TLS checks are skipped"
    try:
        import playwright  # noqa: F401
    except ImportError:
        missing["playwright"] = (
            "Playwright is not installed (requirements.txt): test_config's Chromium checks are skipped"
        )
        missing["chromium"] = "no Chromium: Playwright, which starts it, is not installed"
    else:
        problem = chromium_problem()
        if problem:
            missing["chromium"] = f"{problem}: test_config's Chromium checks are skipped"
    if not (os.environ.get("PLASMA_WRANGLER") or shutil.which("wrangler")):
        missing["wrangler"] = "no wrangler (PLASMA_WRANGLER, or on PATH): test_contract runs the gateway backend alone"
    return missing


def chromium_problem():
    """Why Playwright's Chromium does not start here, as test_config would find it, or None when it starts."""
    from playwright.sync_api import sync_playwright

    try:
        with sync_playwright() as pw:
            pw.chromium.launch(headless=True).close()
    except Exception as e:  # Playwright raises its own Error for a missing browser or a missing system library
        first = (str(e).strip().splitlines() or [type(e).__name__])[0][:200]
        return f"Playwright's Chromium does not start (python -m playwright install chromium): {first}"
    return None


def summary(text):
    """(tests run, the result line's details) of a unittest run's output, or (None, the last line)."""
    ran, result = None, None
    for line in text.splitlines():
        if m := RAN.match(line):
            ran = int(m.group(1))
        elif m := RESULT.match(line):
            result = line
    if ran is None:
        lines = [l for l in text.splitlines() if l.strip()]
        return None, lines[-1] if lines else ""
    return ran, result or ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("suites", nargs="*", help="the suites to run (default: all of them)")
    ap.add_argument(
        "--build-dir",
        type=Path,
        default=os.environ.get("PLASMA_WASM_BUILD") or None,
        help="a built client's build directory, for test_config (default: $PLASMA_WASM_BUILD)",
    )
    ap.add_argument("--out", type=Path, help="keep each suite's output and test_config's evidence under this folder")
    ap.add_argument(
        "--require",
        default="",
        help="fail (exit 2) when one of these prerequisites is missing instead of skipping its checks: "
        "all, or a comma list of " + ", ".join(PREREQUISITES),
    )
    a = ap.parse_args()
    required = set(PREREQUISITES) if a.require == "all" else {r for r in a.require.split(",") if r}
    if required - set(PREREQUISITES):
        ap.error(
            "unknown prerequisite: "
            + ", ".join(sorted(required - set(PREREQUISITES)))
            + " (the prerequisites: all, "
            + ", ".join(PREREQUISITES)
            + ")"
        )
    suites = a.suites or list(SUITES)
    unknown = [s for s in suites if s not in SUITES]
    if unknown:
        ap.error("unknown suite: " + ", ".join(unknown) + " (the suites: " + ", ".join(SUITES) + ")")
    if a.build_dir and not (a.build_dir / "Sources/Plasma/Apps/plClient/plClient.js").is_file():
        ap.error(f"{a.build_dir} holds no built client (Sources/Plasma/Apps/plClient/plClient.js)")
    if a.out:
        a.out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(
        [str(ROOT / "web/deploy"), str(HERE)] + ([env["PYTHONPATH"]] if env.get("PYTHONPATH") else [])
    )
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env.pop("PLASMA_WASM_BUILD", None)
    if a.build_dir:
        env["PLASMA_WASM_BUILD"] = str(a.build_dir.resolve())
    missing = prerequisites()
    if not a.build_dir:
        missing["build"] = (
            "no client build (--build-dir, PLASMA_WASM_BUILD): test_config's three built-client checks are skipped"
        )
    if "test_config" not in suites:
        missing.pop("build", None)
    for reason in missing.values():
        print(f"note: {reason}", flush=True)
    absent = sorted(required & set(missing))
    if absent:
        print(f"run.py: required but missing: {', '.join(absent)}", file=sys.stderr)
        return 2
    results = []
    for suite in suites:
        script, cwd = SUITES[suite]
        args = [sys.executable, "-B", str(script)]
        suite_env = dict(env)
        if suite == "test_config" and a.out:
            args += ["--out", str(a.out.resolve() / "test_config")]
        if suite == "wgsl_regen":
            args.append("--check")
        else:
            args.append("-v")  # each test's line, and each skip's reason
        if suite == "test_contract":
            if "node" in missing:
                results.append(
                    (suite, "SKIP", None, "node is not on PATH: the Worker's address key runs under node", 0)
                )
                print(f"== {suite}: skipped ({results[-1][3]})", flush=True)
                continue
            if "wrangler" in missing:
                suite_env["PLASMA_CONTRACT_BACKEND"] = "gateway"
        print(f"== {suite}", flush=True)
        t0 = time.monotonic()
        p = subprocess.run(args, cwd=cwd, env=suite_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        seconds = time.monotonic() - t0
        sys.stdout.write(p.stdout)
        sys.stdout.flush()
        if a.out:
            (a.out / f"{suite}.txt").write_text(p.stdout)
        ran, line = summary(p.stdout)
        results.append((suite, "PASS" if p.returncode == 0 else "FAIL", ran, line, seconds))
    print()
    for suite, verdict, ran, line, seconds in results:
        count = f"{ran} tests" if ran is not None else "-"
        print(f"{verdict:4}  {suite:22} {count:>10}  {seconds:4.0f} s  {line}")
    failed = [r for r in results if r[1] == "FAIL"]
    print(
        f"\n{len(results) - len(failed)} of {len(results)} suites passed or skipped"
        + (f"; FAILED: {', '.join(r[0] for r in failed)}" if failed else "")
    )
    return int(bool(failed))


if __name__ == "__main__":
    sys.exit(main())
