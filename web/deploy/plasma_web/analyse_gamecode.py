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
"""Analyse a shard's game code (Python pak + SDL) as the client holds it.

Input: a folder exported from the browser's game-code capture (OPFS Diagnostics/gamecode/<deployment>/: index.json,
python/<name>.pak, SDL/<name>.sdl), the .zip the export page downloads, or any folder holding one *.pak and *.sdl
files (searched case-insensitively, e.g. a shard's auth data).
Read-only: nothing in the input is written, executed or imported.

It reports:
  1. the pak's module list and, per entry, the marshal format family and the Python version it was written by,
     decided from the bytes;
  2. per SDL descriptor: names, versions and variables against this repository's Scripts/SDL;
  3. the pak's module set against this repository's Scripts/Python (as plPythonPack names them).

Formats (the specification each reader follows):
  - pak: Sources/Tools/plPythonPack/main.cpp PackDirectory (LE32 count; count x (SafeString name, LE32 offset);
    at each offset LE32 size + the marshal bytes of one code object; size 0 = an entry the tool could not compile),
    read by plPythonPack::Open (the index) and plPythonPack::OpenPacked (an entry) in
    Sources/Plasma/FeatureLib/pfPython/plPythonPack.cpp; SafeString = hsStream::ReadSafeString (CoreLib/hsStream.cpp).
  - secure streams: plSecureStream (PubUtilLib/plFile/plSecureStream.cpp) files start with the 12-byte magic
    "whatdoyousee", "BriceIsSmart" or "notthedroids". The client decrypts them with the key the auth server gives
    (OnGameCodeDiscovered, plResPatcher.cpp); this tool never decrypts, it only reports such a file as encrypted.
  - marshal: CPython Python/marshal.c (3.12.9: the type codes TYPE_NULL to TYPE_SHORT_ASCII_INTERNED, marshal.c:45-77;
    FLAG_REF :71; r_object's case TYPE_CODE).
    A code object's field list tells the family:
      py2      (CPython 2.3-2.7 marshal.c w_object/r_object TYPE_CODE): argcount, nlocals, stacksize, flags,
               code, consts, names, varnames, freevars, cellvars, filename, name, firstlineno, lnotab;
               strings 's', 't' (interned, appended to a string table; marshal version >= 1, CPython >= 2.4),
               'R' (string table reference), 'u'; no FLAG_REF and no 'r'/'a'/'z'/')' (py3.4+ codes).
      py3.0-3.7  argcount, kwonlyargcount, nlocals, stacksize, flags, then the py2 object fields;
      py3.8-3.10 argcount, posonlyargcount, kwonlyargcount, nlocals, stacksize, flags, then as above;
      py3.11+    argcount, posonlyargcount, kwonlyargcount, stacksize, flags, code, consts, names,
               localsplusnames, localspluskinds, filename, name, qualname, firstlineno, linetable,
               exceptiontable (3.12.9 marshal.c, r_object's case TYPE_CODE).
    The layout that consumes exactly the entry's bytes wins. Within py2 the bytecode narrows the version
    (CPython 2.7 Lib/opcode.py: POP_JUMP_IF_FALSE 114 / POP_JUMP_IF_TRUE 115 are new in 2.7; 2.3-2.6 compile
    conditionals to JUMP_IF_FALSE 111 / JUMP_IF_TRUE 112 and leave 114/115 unassigned). Within py3.11+ the first
    instruction is RESUME (151 in 3.11 and 3.12: def_op('RESUME', 151), Lib/opcode.py:213 of 3.12.9; 149 in 3.13).
  - optional --cpython <exe>: a native CPython of the browser's version (the browser build links CPython 3.12.9's
    libpython3.12.a) unmarshals every entry (marshal.loads, marshal version 4) and checks every opcode of every
    nested code object against its opcode table: the
    "does this interpreter accept it" answer. The report records the interpreter's version; only 3.12.9 stands for
    the browser. (The wasm CPython itself, python.js, needs a browser-style worker and is not run here.)
  - SDL: Sources/Plasma/PubUtilLib/plSDL/plSDLParser.cpp ILoadSDLFile/IParseStateDesc/IParseVarDesc, tokens as
    hsStream::GetToken in CoreLib/hsStream.cpp (IsTokenSeparator: whitespace , =; '#' to end of line is a
    comment), atomic counts plSimpleVarDescriptor::SetType (plVarDescriptor.cpp).

  4. (--build py2|py3 or --deployment <file>) whether every entry runs on the client build it is paired
     with (BUILDS: plClient = CPython 3.12.9, plClient-py2 = CPython 2.3.3); a mismatch is refused (exit 3) with the
     families, leading bytes, marshal type codes and first opcodes found. A pak has no .pyc magic number to report.

Usage:
  analyse_gamecode.py <folder|zip> [--repo <plasma-web checkout>] [--json <out.json>] [--cpython <python3.12>]
                      [--stdlib <CPython lib/python3.12>] [--build py2|py3 | --deployment <deployment.json>]

Without --stdlib the pak-only modules are listed as one group; with it they are split into the stdlib's and the
others.
"""

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

SECURE_MAGICS = (b"whatdoyousee", b"BriceIsSmart", b"notthedroids")


# ---------------------------------------------------------------------------------------------------------------
# Input


class GameCode:
    """The input's files: name (as the client names it: python/<f>.pak, SDL/<f>.sdl) -> bytes, plus index.json."""

    def __init__(self, source):
        self.source = str(source)
        self.files = {}
        self.index = None
        p = Path(source)
        if p.is_file() and zipfile.is_zipfile(p):
            with zipfile.ZipFile(p) as z:
                for name in z.namelist():
                    if not name.endswith("/"):
                        self._add(name, z.read(name))
        elif p.is_dir():
            for f in sorted(p.rglob("*")):
                if f.is_file():
                    self._add(f.relative_to(p).as_posix(), f.read_bytes())
        else:
            raise SystemExit(f"{source}: not a folder or a zip")

    @staticmethod
    def key(name):
        """python/<file>.pak or SDL/<file>.sdl, whatever directory and separators the shard used."""
        base = name.replace("\\", "/").split("/")[-1]
        low = base.lower()
        return ("python/" + base) if low.endswith(".pak") else ("SDL/" + base) if low.endswith(".sdl") else None

    def _add(self, rel, data):
        parts = rel.split("/")
        if parts[-1].lower() == "index.json" and len(parts) <= 2:
            try:
                self.index = json.loads(data)
            except ValueError:
                pass
        elif self.key(rel):
            self.files[self.key(rel)] = data

    def paks(self):
        return {k: v for k, v in self.files.items() if k.startswith("python/")}

    def sdls(self):
        return {k: v for k, v in self.files.items() if k.startswith("SDL/")}


def secure_magic(data):
    head = data[:12]
    return head.decode("ascii") if head in SECURE_MAGICS else None


# ---------------------------------------------------------------------------------------------------------------
# Pak


def read_safe_string(buf, pos):
    """hsStream::ReadSafeString (hsStream.cpp)."""
    (n,) = struct.unpack_from("<H", buf, pos)
    pos += 2
    if not n & 0xF000:
        raise ValueError("SafeString in the pre-2003 format")
    n &= ~0xF000 & 0xFFFF
    raw = bytearray(buf[pos : pos + n])
    if len(raw) != n:
        raise ValueError("SafeString runs past the end")
    if n and raw[0] & 0x80:
        raw = bytearray(~b & 0xFF for b in raw)
    return raw.decode("latin-1"), pos + n


def read_pak(data):
    """[(name, offset, size, marshal bytes)] per plPythonPack.cpp: plPythonPack::Open's index, OpenPacked's entries."""
    (count,) = struct.unpack_from("<I", data, 0)
    if count > 100000:
        raise ValueError(f"implausible entry count {count}")
    pos, index = 4, []
    for _ in range(count):
        name, pos = read_safe_string(data, pos)
        (offset,) = struct.unpack_from("<I", data, pos)
        pos += 4
        index.append((name, offset))
    entries = []
    for name, offset in index:
        (size,) = struct.unpack_from("<i", data, offset)
        if size < 0 or offset + 4 + size > len(data):
            raise ValueError(f"entry {name}: size {size} at {offset} runs past the end")
        entries.append((name, offset, size, data[offset + 4 : offset + 4 + size]))
    return entries


# ---------------------------------------------------------------------------------------------------------------
# Marshal (structure only; nothing is evaluated)


class MarshalError(Exception):
    pass


LAYOUTS = {
    # name: (number of leading 32-bit fields, object fields after them, int fields among the objects)
    "py2": (
        4,
        ["code", "consts", "names", "varnames", "freevars", "cellvars", "filename", "name", "#firstlineno", "lnotab"],
    ),
    "py3.0-3.7": (
        5,
        ["code", "consts", "names", "varnames", "freevars", "cellvars", "filename", "name", "#firstlineno", "lnotab"],
    ),
    "py3.8-3.10": (
        6,
        ["code", "consts", "names", "varnames", "freevars", "cellvars", "filename", "name", "#firstlineno", "lnotab"],
    ),
    "py3.11+": (
        5,
        [
            "code",
            "consts",
            "names",
            "localsplusnames",
            "localspluskinds",
            "filename",
            "name",
            "qualname",
            "#firstlineno",
            "linetable",
            "exceptiontable",
        ],
    ),
}


class MarshalReader:
    def __init__(self, data, layout):
        self.d = data
        self.p = 0
        self.layout = layout
        self.py2 = layout == "py2"
        self.refs = []  # py3 FLAG_REF table
        self.strings = []  # py2 interned-string table ('t' appends, 'R' refers)
        self.types = set()  # type codes seen
        self.codes = []  # bytecode of every code object (outermost first)

    def byte(self):
        if self.p >= len(self.d):
            raise MarshalError("truncated")
        b = self.d[self.p]
        self.p += 1
        return b

    def take(self, n):
        if n < 0 or self.p + n > len(self.d):
            raise MarshalError("truncated")
        out = self.d[self.p : self.p + n]
        self.p += n
        return out

    def long(self):
        return struct.unpack("<i", self.take(4))[0]

    def obj(self):
        code = self.byte()
        flag = bool(code & 0x80)
        t = chr(code & 0x7F)
        if flag and self.py2:
            raise MarshalError("FLAG_REF in a py2 stream")
        self.types.add(t)
        slot = None
        if flag and t not in "r":
            slot = len(self.refs)
            self.refs.append(None)
        value = self._obj(t)
        if slot is not None:
            self.refs[slot] = value
        return value

    def _obj(self, t):
        if t in "0NFTS.":
            return t
        if t == "i":
            return self.long()
        if t == "I":
            return struct.unpack("<q", self.take(8))[0]
        if t in "fx":
            n = 2 if t == "x" else 1
            for _ in range(n):
                self.take(self.byte())
            return t
        if t == "g":
            self.take(8)
            return t
        if t == "y":
            self.take(16)
            return t
        if t == "l":
            n = self.long()
            self.take(2 * abs(n))
            return t
        if t in "stu":
            n = self.long()
            if n < 0:
                raise MarshalError("negative length")
            s = self.take(n)
            if self.py2 and t == "t":
                self.strings.append(s)  # py3 keeps 't' for interned non-ASCII str (marshal.c w_object)
            return s
        if t == "R":
            if not self.py2:
                raise MarshalError("'R' is a py2 string reference")
            i = self.long()
            if not 0 <= i < len(self.strings):
                raise MarshalError("bad string reference")
            return self.strings[i]
        if t in "aA":
            if self.py2:
                raise MarshalError("ascii types are py3.4+")
            return self.take(self.long())
        if t in "zZ":
            if self.py2:
                raise MarshalError("short ascii is py3.4+")
            return self.take(self.byte())
        if t == "r":
            if self.py2:
                raise MarshalError("'r' is py3.4+")
            i = self.long()
            if not 0 <= i < len(self.refs):
                raise MarshalError("bad reference")
            return self.refs[i]
        if t in "([<>":
            n = self.long()
            if n < 0 or n > len(self.d):
                raise MarshalError("bad length")
            return tuple(self.obj() for _ in range(n))
        if t == ")":
            if self.py2:
                raise MarshalError("small tuple is py3.4+")
            n = self.byte()
            return tuple(self.obj() for _ in range(n))
        if t == "{":
            while True:
                if self.p < len(self.d) and self.d[self.p] == ord("0"):
                    self.p += 1
                    return t
                self.obj()
                self.obj()
        if t == "c":
            return self.code()
        raise MarshalError(f"unknown type code {t!r}")

    def code(self):
        leading, fields = LAYOUTS[self.layout]
        for _ in range(leading):
            self.long()
        result = {}
        for f in fields:
            if f.startswith("#"):
                self.long()
            else:
                result[f] = self.obj()
        bytecode = result.get("code")
        if not isinstance(bytecode, (bytes, bytearray)):
            raise MarshalError("co_code is not a string")
        self.codes.append(bytes(bytecode))
        return "code"


def classify(raw):
    """(family, details) for one entry's marshal bytes."""
    if not raw:
        return "empty", {}
    first = raw[0]
    if first & 0x7F != ord("c"):
        return "not-code", {"first_byte": f"0x{first:02x}"}
    fits = []
    for layout in LAYOUTS:
        if layout == "py2" and first & 0x80:
            continue
        r = MarshalReader(raw, layout)
        try:
            r.obj()
        except (MarshalError, RecursionError, struct.error):
            continue
        if r.p == len(raw):
            fits.append((layout, r))
    if not fits:
        return "unparsed", {"first_byte": f"0x{first:02x}"}
    layout, r = fits[0]
    details = {
        "layout": layout,
        "first_byte": f"0x{first:02x}",
        "flag_ref": bool(first & 0x80),
        "type_codes": "".join(sorted(r.types)),
        "code_objects": len(r.codes),
    }
    if len(fits) > 1:
        details["also_fits"] = [f for f, _ in fits[1:]]
    if layout == "py2":
        ops = set()
        for co in r.codes:
            i = 0
            while i < len(co):
                op = co[i]
                ops.add(op)
                i += 3 if op >= 90 else 1  # HAVE_ARGUMENT = 90 in every CPython 2.x opcode.py
        details["py2_marshal_v1"] = "t" in r.types or "R" in r.types
        details["py2_pop_jump"] = bool(ops & {114, 115})
        details["py2_jump_if"] = bool(ops & {111, 112})
        details["py2_ops_not_in_2_3"] = sorted(ops - PY23_OPCODES)
    elif layout == "py3.11+":
        details["resume_opcode"] = (
            r.codes[-1][0] if r.codes and r.codes[-1] else None
        )  # the module (outermost) object completes last
    return layout, details


# ---------------------------------------------------------------------------------------------------------------
# Which client build a pak pairs with. A pak entry is bare marshal data with no .pyc magic number
# (WritePythonFile in plPythonPack's main.cpp writes PythonInterface::DumpObject's bytes after a LE32 size;
# plPythonPack::OpenPacked reads them back with PyMarshal_ReadObjectFromString, plPythonPack.cpp:210), so the family
# is decided from the bytes, entry by entry.
#   py3 = plClient, the browser's CPython 3.12.9 (libpython3.12.a): a py3.11+ code object whose module code starts
#         with RESUME 151 (3.12.9 def_op('RESUME', 151), Lib/opcode.py:213; 3.11 also uses 151, 3.13 149;
#         --cpython 3.12.9 separates 3.11 from 3.12);
#   py2 = plClient-py2 (PLASMA_PYTHON_ABI=2), CPython 2.3.3:
#         a py2 code object using only the type codes 2.3.3's marshal.c reads (TYPE_NULL to TYPE_UNKNOWN,
#         Python/marshal.c:18-35; 't' and 'R', marshal version 1, are CPython 2.4+) and only opcodes 2.3.3 defines
#         (Include/opcode.h's #defines, STOP_CODE 0 to EXTENDED_ARG 143).
BUILDS = {
    "py3": {"client": "plClient", "python": "CPython 3.12.9", "family": "py3.11+"},
    "py2": {"client": "plClient-py2", "python": "CPython 2.3.3", "family": "py2"},
}
# CPython 2.3.3's type codes, TYPE_NULL to TYPE_UNKNOWN, Python/marshal.c:18-35 (TYPE_UNKNOWN '?' is never written)
PY23_TYPE_CODES = set("0NFTS.iIfxlsu([{c")
PY23_OPCODES = (
    {0, 1, 2, 3, 4, 5, 10, 11, 12, 13, 15}
    | set(range(19, 34))
    | set(range(40, 44))
    | set(range(50, 54))
    | set(range(55, 69))
    | set(range(70, 81))
    | set(range(82, 94))
    | set(range(95, 109))
    | {110, 111, 112, 113, 116, 119, 120, 121, 122, 124, 125, 126}
    | set(range(130, 138))
    | {140, 141, 142, 143}
)
# CPython 2.3.3 Include/opcode.h's #defines, STOP_CODE 0 to EXTENDED_ARG 143 (SLICE 30, STORE_SLICE 40 and
# DELETE_SLICE 50 also use +1..+3)


def build_refusal(build, family, details):
    """None when the build's interpreter reads this entry, else the reason it cannot."""
    want = BUILDS[build]["family"]
    if family != want:
        return f"marshal family {family}, not {want}"
    if build == "py3":
        if details.get("resume_opcode") != 151:
            return f"first opcode {details.get('resume_opcode')}, not RESUME 151 (3.11/3.12)"
        return None
    extra = sorted(set(details.get("type_codes", "")) - PY23_TYPE_CODES)
    if extra:
        return "marshal type codes " + "".join(extra) + " (not in CPython 2.3.3 marshal.c)"
    if details.get("py2_ops_not_in_2_3"):
        return "opcodes " + ",".join(map(str, details["py2_ops_not_in_2_3"])) + " (not in CPython 2.3.3 opcode.h)"
    return None


def family_check(rows, build):
    """Does every non-empty entry of one pak run on this client build? Returns the check record."""
    reasons, examples = {}, {}
    for r in rows:
        if r["family"] == "empty":
            continue
        why = build_refusal(build, r["family"], r["details"])
        if why:
            reasons[why] = reasons.get(why, 0) + 1
            examples.setdefault(why, r["name"])
    found = {"families": {}, "leading_bytes": {}, "type_codes": "", "first_opcodes": {}}
    types = set()
    for r in rows:
        if r["family"] == "empty":
            continue
        found["families"][r["family"]] = found["families"].get(r["family"], 0) + 1
        lead = r["details"].get("first_byte")
        if lead:
            found["leading_bytes"][lead] = found["leading_bytes"].get(lead, 0) + 1
        types |= set(r["details"].get("type_codes", ""))
        op = r["details"].get("resume_opcode")
        if op is not None:
            found["first_opcodes"][op] = found["first_opcodes"].get(op, 0) + 1
    found["type_codes"] = "".join(sorted(types))
    return {
        "build": build,
        "client": BUILDS[build]["client"],
        "python": BUILDS[build]["python"],
        "ok": not reasons,
        "refused_entries": sum(reasons.values()),
        "reasons": reasons,
        "examples": examples,
        "found": found,
        "pyc_magic": "none: a pak entry is bare marshal data (plPythonPack main.cpp:208-218)",
    }


def version_verdict(entries):
    """One verdict over all non-empty entries."""
    fams = {}
    for e in entries:
        fams[e["family"]] = fams.get(e["family"], 0) + 1
    out = {"families": fams}
    py2 = [e for e in entries if e["family"] == "py2"]
    if py2:
        v1 = sum(1 for e in py2 if e["details"].get("py2_marshal_v1"))
        pop = sum(1 for e in py2 if e["details"].get("py2_pop_jump"))
        jif = sum(1 for e in py2 if e["details"].get("py2_jump_if"))
        if pop:
            ver = "2.7"
        elif jif:
            ver = "2.4-2.6" if v1 else "2.3"
        else:
            ver = "2.x"
        out["py2"] = {
            "entries": len(py2),
            "marshal_v1_entries": v1,
            "pop_jump_entries": pop,
            "jump_if_entries": jif,
            "python": ver,
        }
    new = [e for e in entries if e["family"] == "py3.11+"]
    if new:
        resumes = {}
        for e in new:
            k = e["details"].get("resume_opcode")
            resumes[k] = resumes.get(k, 0) + 1
        ver = "3.11 or 3.12" if set(resumes) == {151} else ("3.13" if set(resumes) == {149} else "3.11+ (mixed)")
        out["py3.11+"] = {"entries": len(new), "first_opcodes": resumes, "python": ver}
    return out


CPYTHON_CHECK = r"""
import marshal, opcode, struct, sys, json
data = open(sys.argv[1], "rb").read()
valid = set(i for i, n in enumerate(opcode.opname) if not n.startswith("<"))
out = {"python": sys.version.split()[0]}
res = {}
n = struct.unpack_from("<I", data, 0)[0]
pos, index = 4, []
for _ in range(n):
    ln = struct.unpack_from("<H", data, pos)[0] & 0x0FFF; pos += 2
    raw = bytearray(data[pos:pos + ln]); pos += ln
    if ln and raw[0] & 0x80: raw = bytearray(~b & 0xFF for b in raw)
    index.append((raw.decode("latin-1"), struct.unpack_from("<I", data, pos)[0])); pos += 4
def walk(co, bad):
    code = co.co_code
    for i in range(0, len(code), 2):
        if code[i] not in valid: bad.add(code[i])
    for c in co.co_consts:
        if hasattr(c, "co_code"): walk(c, bad)
for name, off in index:
    size = struct.unpack_from("<i", data, off)[0]
    if size <= 0: res[name] = "empty"; continue
    try:
        co = marshal.loads(data[off + 4:off + 4 + size])
    except Exception as e:
        res[name] = "loads-failed: " + type(e).__name__ + ": " + str(e)[:80]; continue
    if not hasattr(co, "co_code"): res[name] = "not-code"; continue
    bad = set(); walk(co, bad)
    res[name] = "ok" if not bad else "bad-opcodes " + ",".join(map(str, sorted(bad)))
out["entries"] = res
print("GAMECODEJSON" + json.dumps(out))
"""


def cpython_check(pak_bytes, python):
    """Run CPYTHON_CHECK under a native CPython (isolated, no site), on a private temporary copy."""
    with tempfile.TemporaryDirectory() as tmp:
        pak = Path(tmp) / "input.pak"
        pak.write_bytes(pak_bytes)
        script = Path(tmp) / "check.py"
        script.write_text(CPYTHON_CHECK)
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        proc = subprocess.run(
            [str(python), "-I", "-S", "-B", str(script), str(pak)],
            capture_output=True,
            text=True,
            cwd=tmp,
            env=env,
            timeout=600,
        )
    for line in proc.stdout.splitlines():
        if line.startswith("GAMECODEJSON"):
            return json.loads(line[len("GAMECODEJSON") :])
    return {"error": f"rc {proc.returncode}: {proc.stderr.strip()[-400:]}"}


# ---------------------------------------------------------------------------------------------------------------
# Module sets


def repo_modules(scripts_python):
    """The names plPythonPack gives Scripts/Python (main.cpp FindFiles/FindPackages, the root plus its plasma/ and
    system/ sections; a package is a directory with __init__.py, its files named <package>.<file>)."""
    root = Path(scripts_python)
    names = set()

    def files(d):
        return [f.name for f in sorted(d.glob("*.py"))]

    def packages(d, parent=""):
        for sub in sorted(x for x in d.iterdir() if x.is_dir() and x.name not in ("plasma", "system", "__pycache__")):
            pkg = (parent + "." if parent else "") + sub.name
            if (sub / "__init__.py").is_file():
                names.update(f"{pkg}.{f}" for f in files(sub))
                packages(sub, pkg)

    for d in (root, root / "plasma", root / "system"):
        if d.is_dir():
            names.update(files(d))
            packages(d)
    return {n[:-3] for n in names}


def stdlib_modules(lib):
    """Module names of a CPython Lib directory, named as plPythonPack names the system/ section."""
    lib = Path(lib)
    if not lib.is_dir():
        return set()
    names = {f.stem for f in lib.glob("*.py")}

    def packages(d, parent=""):
        for sub in sorted(x for x in d.iterdir() if x.is_dir()):
            if (sub / "__init__.py").is_file():
                pkg = (parent + "." if parent else "") + sub.name
                names.update(f"{pkg}.{f.stem}" for f in sub.glob("*.py"))
                packages(sub, pkg)

    packages(lib)
    return names


# ---------------------------------------------------------------------------------------------------------------
# SDL

ATOMIC = {"vector3": 3, "point3": 3, "rgb": 3, "rgba": 4, "rgb8": 3, "rgba8": 4}


def tokens(text):
    """hsStream::GetToken over the whole file (hsStream.cpp, with IsTokenSeparator)."""
    out, i, n = [], 0, len(text)
    sep = lambda c: c.isspace() or c in ",="
    while i < n:
        while i < n and sep(text[i]):
            i += 1
        if i >= n:
            break
        if text[i] == "#":
            while i < n and text[i] != "\n":
                i += 1
            continue
        j = i
        while j < n and not sep(text[j]):
            j += 1
        out.append(text[i:j])
        i = j
    return out


def atomic_count(vtype):
    t = vtype.lower()
    if t.startswith("quat"):
        return 4
    return ATOMIC.get(t, 1)


def parse_sdl(text, filename):
    """[{name, version, file, vars: [{name, type, count, default, display, defaultoption}]}] as ILoadSDLFile."""
    toks = tokens(text)
    descs, cur, i = [], None, 0
    parsing_desc = False
    skip = False
    tok = None
    while True:
        if not skip:
            if i >= len(toks):
                break
            tok = toks[i]
            i += 1
        skip = False
        if tok == "VAR":
            parsing_desc = False
            continue
        if tok == "STATEDESC":
            parsing_desc = True
            cur = None
            continue
        if tok == "}":
            parsing_desc = False
            continue
        if parsing_desc:
            cur = {"name": tok, "version": None, "file": filename, "vars": []}
            i += 1  # '{'
            if i < len(toks) and toks[i] == "VERSION":
                cur["version"] = int(re.match(r"-?\d*", toks[i + 1]).group() or 0) if i + 1 < len(toks) else None
                i += 2
            descs.append(cur)
            continue
        if cur is None:
            continue
        var = {"type": tok, "name": None, "count": None, "default": None, "display": [], "defaultoption": None}
        cur["vars"].append(var)
        if i < len(toks):
            m = re.match(r"([^\[]*)\[([^\]]*)\]?", toks[i])
            var["name"] = m.group(1) if m else toks[i]
            var["count"] = int(re.match(r"\d*", m.group(2)).group() or 0) if m else None
            i += 1
        while i < len(toks):
            t = toks[i]
            i += 1
            if t == "DEFAULT":
                vals = toks[i : i + atomic_count(var["type"]) if not var["type"].startswith("$") else i]
                i += len(vals)
                var["default"] = ",".join(vals) if vals else None
            elif t == "DISPLAYOPTION":
                if i < len(toks):
                    var["display"].append(toks[i])
                    i += 1
            elif t == "DEFAULTOPTION":
                if i < len(toks):
                    var["defaultoption"] = toks[i]
                    i += 1
            else:
                tok = t
                skip = True
                break
    return descs


def sdl_set(files):
    """{(name, version): desc} over a {filename: text} map; duplicates recorded."""
    out, dups = {}, []
    for fname, text in sorted(files.items()):
        for d in parse_sdl(text, fname):
            key = (d["name"], d["version"])
            if key in out:
                dups.append({"name": d["name"], "version": d["version"], "files": [out[key]["file"], fname]})
            out[key] = d
    return out, dups


def var_sig(v):
    return {
        "type": v["type"],
        "count": v["count"],
        "default": v["default"],
        "display": ",".join(v["display"]),
        "defaultoption": v["defaultoption"],
    }


def sdl_diff(captured, repo):
    cap, cap_dups = sdl_set(captured)
    rep, rep_dups = sdl_set(repo)
    names = sorted({k[0] for k in cap} | {k[0] for k in rep})
    per = []
    for name in names:
        cv = sorted(v for (n, v) in cap if n == name)
        rv = sorted(v for (n, v) in rep if n == name)
        entry = {
            "name": name,
            "captured_versions": cv,
            "repo_versions": rv,
            "captured_file": cap[(name, cv[-1])]["file"] if cv else None,
            "repo_file": rep[(name, rv[-1])]["file"] if rv else None,
        }
        if not cv:
            entry["status"] = "repo-only"
        elif not rv:
            entry["status"] = "captured-only"
        else:
            changes = []
            for ver in sorted(set(cv) & set(rv)):
                a = {v["name"]: var_sig(v) for v in cap[(name, ver)]["vars"]}
                b = {v["name"]: var_sig(v) for v in rep[(name, ver)]["vars"]}
                order_a = [v["name"] for v in cap[(name, ver)]["vars"]]
                order_b = [v["name"] for v in rep[(name, ver)]["vars"]]
                ch = {"version": ver}
                if set(a) - set(b):
                    ch["captured_only_vars"] = sorted(set(a) - set(b))
                if set(b) - set(a):
                    ch["repo_only_vars"] = sorted(set(b) - set(a))
                changed = {k: {"captured": a[k], "repo": b[k]} for k in sorted(set(a) & set(b)) if a[k] != b[k]}
                if changed:
                    ch["changed_vars"] = changed
                if order_a != order_b and set(a) == set(b):
                    ch["var_order_differs"] = True
                if len(ch) > 1:
                    changes.append(ch)
            entry["status"] = "same" if cv == rv and not changes else "differs" if changes else "versions-differ"
            if changes:
                entry["changes"] = changes
            entry["latest_equal"] = cv[-1] == rv[-1] and not any(c["version"] == cv[-1] for c in changes)
        per.append(entry)
    counts = {}
    for e in per:
        counts[e["status"]] = counts.get(e["status"], 0) + 1
    return {
        "descriptors": per,
        "status_counts": counts,
        "captured_descriptor_versions": len(cap),
        "repo_descriptor_versions": len(rep),
        "captured_duplicates": cap_dups,
        "repo_duplicates": rep_dups,
    }


# ---------------------------------------------------------------------------------------------------------------


def analyse(source, repo, cpython=None, stdlib=None, build=None):
    """build: "py2" or "py3", the client build the pak must pair with (BUILDS); each pak then gets a
    build_check record and report["build_check"] the overall verdict. stdlib: a CPython Lib directory whose modules
    label the pak-only ones (None: no labelling, every pak-only module is "other")."""
    gc = GameCode(source)
    report = {"input": gc.source, "index": gc.index, "files": []}
    for name, data in sorted(gc.files.items()):
        report["files"].append(
            {
                "name": name,
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "secure_magic": secure_magic(data),
            }
        )
    if gc.index and isinstance(gc.index.get("files"), list):
        listed = {GameCode.key(str(f.get("name", ""))): f for f in gc.index["files"]}
        mism = [
            f["name"]
            for f in report["files"]
            if f["name"] in listed
            and (listed[f["name"]].get("sha256") != f["sha256"] or listed[f["name"]].get("bytes") != f["bytes"])
        ]
        report["index_check"] = {
            "listed": len(listed),
            "present": len(report["files"]),
            "missing": sorted(set(listed) - {f["name"] for f in report["files"]}),
            "mismatched": mism,
        }

    paks = {}
    for name, data in sorted(gc.paks().items()):
        if secure_magic(data):
            paks[name] = {
                "encrypted": secure_magic(data),
                "note": "plSecureStream file: the client decrypts it with the shard's key; capture it "
                "with the browser's game-code capture to analyse the plaintext",
            }
            continue
        try:
            entries = read_pak(data)
        except (ValueError, struct.error) as e:
            paks[name] = {"error": f"not a plPythonPack file: {e}"}
            continue
        rows = []
        for ename, offset, size, raw in entries:
            fam, det = classify(raw)
            rows.append({"name": ename, "offset": offset, "size": size, "family": fam, "details": det})
        info = {
            "entries": len(rows),
            "empty_entries": sum(1 for r in rows if r["family"] == "empty"),
            "verdict": version_verdict([r for r in rows if r["family"] != "empty"]),
            "modules": rows,
        }
        if build:
            info["build_check"] = family_check(rows, build)
        if cpython:
            chk = cpython_check(data, cpython)
            if "entries" in chk:
                states = {}
                for r in rows:
                    s = chk["entries"].get(r["name"], "missing")
                    r["cpython_check"] = s
                    k = s.split(":")[0].split(" ")[0]
                    states[k] = states.get(k, 0) + 1
                info["cpython_check"] = {"python": chk.get("python"), "states": states}
            else:
                info["cpython_check"] = chk
        paks[name] = info
    report["paks"] = paks
    if build:
        # Every pak must be readable and pair with the build; an encrypted or unreadable pak, or none at
        # all, is not a pass.
        undecided = sorted(n for n, i in paks.items() if "build_check" not in i)
        refused = sorted(n for n, i in paks.items() if "build_check" in i and not i["build_check"]["ok"])
        report["build_check"] = {
            "build": build,
            "client": BUILDS[build]["client"],
            "python": BUILDS[build]["python"],
            "ok": bool(paks) and not undecided and not refused,
            "refused_paks": refused,
            "undecided_paks": undecided,
            "paks": len(paks),
        }

    repo = Path(repo)
    if any("modules" in info for info in paks.values()):
        mods = set()
        for info in paks.values():
            mods.update(r["name"][:-3] if r["name"].endswith(".py") else r["name"] for r in info.get("modules", []))
        rset = repo_modules(repo / "Scripts/Python")
        std = stdlib_modules(stdlib) if stdlib else set()
        only_pak = sorted(mods - rset)
        report["modules_vs_repo"] = {
            "repo_scripts": str(repo / "Scripts/Python"),
            "pak_modules": len(mods),
            "repo_modules": len(rset),
            "common": len(mods & rset),
            "pak_only_stdlib": [m for m in only_pak if m in std],
            "pak_only_other": [m for m in only_pak if m not in std],
            "repo_only": sorted(rset - mods),
            "stdlib_reference": str(stdlib) if std else None,
        }

    texts, enc = {}, []
    for name, data in gc.sdls().items():
        if secure_magic(data):
            enc.append(name)
            continue
        texts[name.split("/", 1)[1]] = data.decode("latin-1")
    repo_sdl = {f.name: f.read_text(encoding="latin-1") for f in sorted((repo / "Scripts/SDL").glob("*.sdl"))}
    report["sdl"] = {
        "files": len(texts),
        "encrypted_files": sorted(enc),
        "repo_files": len(repo_sdl),
        "captured_only_files": sorted(set(texts) - set(repo_sdl)),
        "repo_only_files": sorted(set(repo_sdl) - set(texts)) if texts else [],
        "byte_identical_files": sorted(n for n in set(texts) & set(repo_sdl) if texts[n] == repo_sdl[n]),
    }
    if texts:
        report["sdl"]["diff"] = sdl_diff(texts, repo_sdl)
    return report


def summary(r):
    out = [f"input {r['input']}: {len(r['files'])} files"]
    if r.get("index_check"):
        ic = r["index_check"]
        out.append(
            f"index.json: {ic['listed']} listed, {len(ic['missing'])} missing, {len(ic['mismatched'])} "
            f"size/sha256 mismatches"
        )
    for name, info in r["paks"].items():
        if "encrypted" in info:
            out.append(f"{name}: ENCRYPTED ({info['encrypted']}); not analysed")
            continue
        if "error" in info:
            out.append(f"{name}: {info['error']}")
            continue
        out.append(f"{name}: {info['entries']} entries, {info['empty_entries']} empty")
        v = info["verdict"]
        out.append(f"  marshal families: {v['families']}")
        for fam in ("py2", "py3.11+"):
            if fam in v:
                out.append(f"  {fam}: {v[fam]}")
        if "cpython_check" in info:
            out.append(f"  CPython check: {info['cpython_check']}")
        if "build_check" in info:
            c = info["build_check"]
            f = c["found"]
            out.append(
                f"  build check ({c['build']}: {c['client']}, {c['python']}): "
                + (
                    "PASS"
                    if c["ok"]
                    else f"REFUSED, {c['refused_entries']} entries do not run on it: "
                    + "; ".join(f"{k} x{v} (e.g. {c['examples'][k]})" for k, v in sorted(c["reasons"].items()))
                )
            )
            out.append(
                f"    found: families {f['families']}, leading bytes {f['leading_bytes']}, type codes "
                f"{f['type_codes']!r}, first opcodes {f['first_opcodes']}; .pyc magic {c['pyc_magic']}"
            )
    m = r.get("modules_vs_repo")
    if m:
        out.append(
            f"modules vs Scripts/Python: pak {m['pak_modules']}, repo {m['repo_modules']}, common "
            f"{m['common']}, pak-only stdlib {len(m['pak_only_stdlib'])}, pak-only other "
            f"{len(m['pak_only_other'])}, repo-only {len(m['repo_only'])}"
        )
        if m["pak_only_other"]:
            out.append(
                "  pak-only (not stdlib): "
                + ", ".join(m["pak_only_other"][:40])
                + (" ..." if len(m["pak_only_other"]) > 40 else "")
            )
        if m["repo_only"]:
            out.append("  repo-only: " + ", ".join(m["repo_only"][:40]) + (" ..." if len(m["repo_only"]) > 40 else ""))
    s = r["sdl"]
    out.append(
        f"SDL: {s['files']} plaintext files ({len(s['encrypted_files'])} encrypted), repo {s['repo_files']}; "
        f"byte-identical {len(s['byte_identical_files'])}, captured-only files {len(s['captured_only_files'])}, "
        f"repo-only files {len(s['repo_only_files'])}"
    )
    b = r.get("build_check")
    if "diff" in s:
        d = s["diff"]
        out.append(
            f"  descriptors: {d['status_counts']} (captured {d['captured_descriptor_versions']} name/version "
            f"pairs, repo {d['repo_descriptor_versions']})"
        )
        for e in d["descriptors"]:
            if e["status"] != "same":
                out.append(
                    f"  {e['status']:15s} {e['name']}: captured {e['captured_versions']} repo "
                    f"{e['repo_versions']}" + ("" if e.get("latest_equal", True) else " (latest differs)")
                )
    if b:
        out.append(
            f"BUILD CHECK {'PASS' if b['ok'] else 'REFUSED'}: {b['paks']} pak(s) against the {b['build']} build "
            f"({b['client']}, {b['python']})"
            + (f"; refused {b['refused_paks']}" if b["refused_paks"] else "")
            + (f"; undecided (encrypted, unreadable) {b['undecided_paks']}" if b["undecided_paks"] else "")
            + ("; no pak in the input" if not b["paks"] else "")
        )
    return "\n".join(out)


def main(argv=None):
    # The checkout's root, imported here: the module itself needs only the standard library, so a tool can also
    # load this file by its path.
    from plasma_web import ROOT

    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("input", help="exported capture folder, its .zip, or a folder with one .pak and .sdl files")
    ap.add_argument(
        "--repo",
        default=str(ROOT),
        help="plasma-web checkout whose Scripts/Python and Scripts/SDL are the reference (default: this one)",
    )
    ap.add_argument("--json", help="write the whole report here")
    ap.add_argument("--cpython", help="a native CPython 3.12.9 executable: unmarshal and opcode-check every entry")
    ap.add_argument(
        "--stdlib",
        help="CPython Lib (lib/python3.12) used to label pak-only modules as stdlib; without it they are not labelled",
    )
    ap.add_argument(
        "--build",
        choices=sorted(BUILDS),
        help="refuse (exit 3) a pak whose marshal family does not run on this client build: py3 = "
        "plClient (CPython 3.12.9), py2 = plClient-py2 (CPython 2.3.3)",
    )
    ap.add_argument(
        "--deployment",
        help="the operator deployment file whose build the pak must pair with "
        "(data.gameCode server-py2 = py2, otherwise py3)",
    )
    a = ap.parse_args(argv)
    build = a.build
    if a.deployment:
        from plasma_web import deployment as deployment_file

        try:
            dep_build = deployment_file.load(a.deployment).build
        except (OSError, ValueError) as e:
            ap.error(f"invalid deployment: {e}")
        if build and build != dep_build:
            ap.error(f"--build {build} conflicts with the deployment's {dep_build} build")
        build = dep_build
    r = analyse(a.input, a.repo, a.cpython, a.stdlib, build)
    if a.json:
        Path(a.json).write_text(json.dumps(r, indent=1, sort_keys=True))
    print(summary(r))
    return 3 if build and not r["build_check"]["ok"] else 0


if __name__ == "__main__":
    sys.exit(main())
