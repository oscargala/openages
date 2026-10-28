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
"""python2/build.sh fetch leaves no .cypython-fetch.* folder behind when it fails or is stopped.

fetch rebuilds the pinned Cypython-2.3.3 tree in a temporary folder beside its destination. Here the git download is
replaced by a stand-in (a git wrapper first on PATH that passes every other command to git): one that fails, as a
network error does, and one that waits until the test stops the script's process group with SIGINT (Ctrl-C) or
SIGTERM. Each time the script must end with an error status (130 and 143 for the signals) and the folder
must be gone. Nothing is downloaded.

    python3 web/build/python2/test_fetch_cleanup.py
"""

import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE / "build.sh"


class FetchCleanupTests(unittest.TestCase):
    def run_fetch(self, mode, stop=None):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp, True)
        deps, shim = tmp / "deps", tmp / "bin"
        deps.mkdir()
        shim.mkdir()
        real = shutil.which("git")
        # "fail": the download fails at once; "wait": it hangs, as a stalled download does, until stopped
        action = "exit 1" if mode == "fail" else f'touch "{tmp}/waiting"; exec sleep 60'
        (shim / "git").write_text(
            '#!/bin/sh\nfor a in "$@"; do [ "$a" = fetch ] && { ' + action + f'; }}; done\nexec "{real}" "$@"\n'
        )
        (shim / "git").chmod(0o755)
        env = dict(
            os.environ,
            PLASMA_WASM_DEPS=str(deps),
            HOST_PYTHON=sys.executable,
            PATH=f"{shim}{os.pathsep}{os.environ['PATH']}",
        )
        env.pop("PLASMA_WASM_PYTHON2_SRC", None)
        p = subprocess.Popen(
            ["bash", str(BUILD), "fetch"],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
        self.addCleanup(lambda: p.poll() is None and os.killpg(p.pid, signal.SIGKILL))
        if stop is not None:
            deadline = time.time() + 30
            while not (tmp / "waiting").exists() and time.time() < deadline and p.poll() is None:
                time.sleep(0.05)
            self.assertTrue((tmp / "waiting").exists(), "the stand-in download never started")
            self.assertEqual(len(list(deps.glob(".cypython-fetch.*"))), 1)  # the folder exists while it runs
            os.killpg(p.pid, stop)  # as Ctrl-C or kill reaches the group
        out, _ = p.communicate(timeout=60)
        return p.returncode, out, sorted(x.name for x in deps.iterdir())

    def test_failed_download(self):
        rc, out, left = self.run_fetch("fail")
        self.assertNotEqual(rc, 0, out)
        self.assertIn("python2: fetch:", out)
        self.assertEqual(left, [], out)

    def test_interrupted(self):
        for sig, status in ((signal.SIGINT, 130), (signal.SIGTERM, 143)):
            with self.subTest(signal=sig.name):
                rc, out, left = self.run_fetch("wait", sig)
                self.assertEqual(rc, status, out)
                self.assertEqual(left, [], out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
