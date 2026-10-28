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
"""Check that every object in the given static archives was built with the browser client's
global flags (-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm).

The client links with shared memory and native wasm exceptions; one object built without them
fails the link, or, worse, links and then traps at run time. build-deps.sh's check step runs
this over the whole deps prefix. It needs EMSDK, the emsdk checkout whose LLVM tools read the archives.

Checks per object (wasm 'target_features' section and undefined symbols):
  - +atomics and +bulk-memory   (-pthread; otherwise --shared-memory link fails)
  - +exception-handling         (-fwasm-exceptions / -sSUPPORT_LONGJMP=wasm)
  - no JS-based EH / SjLj imports (invoke_*, emscripten_longjmp, saveSetjmp,
    __cxa_find_matching_catch_*, __resumeException), which mark objects built with
    -fexceptions or -sSUPPORT_LONGJMP=emscripten.
Usage: check-deps.py <lib.a or dir> ...
"""

import os
import re
import subprocess
import sys

EMSDK = os.environ.get("EMSDK", "")
BIN = os.path.join(EMSDK, "upstream", "bin")
JS_EH = re.compile(
    r"^(invoke_\w+|emscripten_longjmp|saveSetjmp|testSetjmp|"
    r"__cxa_find_matching_catch_\w*|__resumeException|_emscripten_throw_longjmp)$"
)


def features(archive):
    out = subprocess.run(
        [os.path.join(BIN, "llvm-objdump"), "-s", "-j", "target_features", archive], capture_output=True, text=True
    ).stdout
    result, cur, buf = {}, None, b""
    for line in out.splitlines():
        m = re.match(r"^(.*\.o(?:bj)?):\s+file format", line) or re.match(r"^(\S.*?):\s+file format", line)
        if m:
            if cur is not None:
                result[cur] = buf
            cur, buf = m.group(1), b""
            continue
        m = re.match(r"^ [0-9a-f]{4} ((?:[0-9a-f]{2,8} ?){1,4})", line)
        if m and cur is not None:
            buf += bytes.fromhex(m.group(1).replace(" ", ""))
    if cur is not None:
        result[cur] = buf
    parsed = {}
    for obj, raw in result.items():
        feats, i = set(), 0
        if raw:
            n = raw[0]
            i = 1
            for _ in range(n):
                prefix = chr(raw[i])
                ln = raw[i + 1]
                i += 2
                feats.add(prefix + raw[i : i + ln].decode())
                i += ln
        parsed[obj.split("(")[-1].rstrip(")") if "(" in obj else os.path.basename(obj)] = feats
    return parsed


def undefined(archive):
    out = subprocess.run([os.path.join(BIN, "llvm-nm"), "-u", archive], capture_output=True, text=True).stdout
    bad, cur = {}, None
    for line in out.splitlines():
        if line.endswith(":") and not line.startswith(" "):
            cur = line[:-1]
            continue
        sym = line.strip().split()[-1] if line.strip() else ""
        if sym and JS_EH.match(sym):
            bad.setdefault(cur, set()).add(sym)
    return bad


def main(paths):
    if not EMSDK or not os.path.isfile(os.path.join(BIN, "llvm-objdump")):
        print("check-deps.py: set EMSDK to the emsdk checkout (its upstream/bin has llvm-objdump)", file=sys.stderr)
        return 2
    if not paths:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    archives = []
    for p in paths:
        if os.path.isdir(p):
            archives += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith(".a"))
        else:
            archives.append(p)
    total_bad = 0
    for a in archives:
        feats = features(a)
        problems = []
        for obj, f in feats.items():
            if not f:
                continue  # objects with no code (e.g. empty TUs) carry no features
            miss = [x for x in ("+atomics", "+bulk-memory", "+exception-handling") if x not in f]
            if miss:
                problems.append(f"{obj}: missing {','.join(miss)}")
        for obj, syms in undefined(a).items():
            problems.append(f"{obj}: JS EH/SjLj imports {','.join(sorted(syms))}")
        status = "OK " if not problems else "BAD"
        print(f"{status} {os.path.basename(a)}: {len(feats)} objects")
        for p in problems[:10]:
            print("     ", p)
        total_bad += len(problems)
    print("RESULT:", "all objects carry the client flags" if total_bad == 0 else f"{total_bad} problems")
    return 1 if total_bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
