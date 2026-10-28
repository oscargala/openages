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
"""Offline tests of analyse_gamecode.py on synthetic inputs (no shard file, no network)."""

import json
import marshal
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

from plasma_web import analyse_gamecode as A

REPO = Path(__file__).resolve().parents[2]


def py2_code(code, consts=(None,)):
    """A module code object as CPython 2.4-2.7 marshal.c writes it (TYPE_CODE, four longs, then objects)."""
    out = b"c" + struct.pack("<iiii", 0, 0, 1, 64)
    out += b"s" + struct.pack("<i", len(code)) + bytes(code)
    out += b"(" + struct.pack("<i", len(consts)) + b"".join(b"N" for _ in consts)
    out += b"(" + struct.pack("<i", 0)  # names
    out += b"(" + struct.pack("<i", 0)  # varnames
    out += b"(" + struct.pack("<i", 0)  # freevars
    out += b"(" + struct.pack("<i", 0)  # cellvars
    out += b"t" + struct.pack("<i", 4) + b"x.py"  # filename (interned: string table entry 0)
    out += b"R" + struct.pack("<i", 0)  # name: a reference to entry 0
    out += struct.pack("<i", 1)  # firstlineno
    out += b"s" + struct.pack("<i", 0)  # lnotab
    return out


def py23_code(code, consts=(None,)):
    """A module code object as CPython 2.3 marshal.c writes it: marshal version 0, plain 's' strings only (2.3 has no
    't'/'R': TYPE_NULL to TYPE_UNKNOWN, Python/marshal.c:18-35)."""
    out = b"c" + struct.pack("<iiii", 0, 0, 1, 64)
    out += b"s" + struct.pack("<i", len(code)) + bytes(code)
    out += b"(" + struct.pack("<i", len(consts)) + b"".join(b"N" for _ in consts)
    for _ in range(4):  # names, varnames, freevars, cellvars
        out += b"(" + struct.pack("<i", 0)
    out += b"s" + struct.pack("<i", 4) + b"x.py"  # filename
    out += b"s" + struct.pack("<i", 1) + b"?"  # name
    out += struct.pack("<i", 1)  # firstlineno
    out += b"s" + struct.pack("<i", 0)  # lnotab
    return out


def py312_code(first_opcode=151):
    """A module code object in the 3.11+ layout (3.12.9 marshal.c, r_object's case TYPE_CODE) whose bytecode starts
    with RESUME 151 (3.11/3.12; 149 = 3.13), then RETURN_CONST 0."""
    out = b"c" + struct.pack("<iiiii", 0, 0, 0, 1, 0)
    code = bytes([first_opcode, 0, 121, 0])
    out += b"s" + struct.pack("<i", len(code)) + code
    out += b")" + bytes([1]) + b"N"  # consts
    out += b")" + bytes([0])  # names
    out += b")" + bytes([0])  # localsplusnames
    out += b"s" + struct.pack("<i", 0)  # localspluskinds
    out += b"z" + bytes([4]) + b"x.py"  # filename
    out += b"z" + bytes([8]) + b"<module>"  # name
    out += b"z" + bytes([8]) + b"<module>"  # qualname
    out += struct.pack("<i", 1)  # firstlineno
    out += b"s" + struct.pack("<i", 0)  # linetable
    out += b"s" + struct.pack("<i", 0)  # exceptiontable
    return out


def safe_string(name):
    raw = name.encode("latin-1")
    return struct.pack("<H", len(raw) | 0xF000) + raw


def make_pak(entries):
    """plPythonPack's layout (main.cpp PackDirectory)."""
    head = struct.pack("<I", len(entries))
    index_len = len(head) + sum(len(safe_string(n)) + 4 for n, _ in entries)
    body, offsets, pos = b"", [], index_len
    for _, data in entries:
        offsets.append(pos)
        chunk = struct.pack("<i", len(data)) + data
        body += chunk
        pos += len(chunk)
    index = b"".join(safe_string(n) + struct.pack("<I", o) for (n, _), o in zip(entries, offsets))
    return head + index + body


SDL_A = """# comment
STATEDESC Foo
{
    VERSION 2
    VAR BOOL    fooVis[1]   DEFAULT=1 DEFAULTOPTION=VAULT
    VAR POINT3  fooPos[1]   DEFAULT=(0,0,0)
    VAR INT     fooList[]
}
STATEDESC Bar
{
    VERSION 1
    VAR BYTE    barState[1] DEFAULT=0 DISPLAYOPTION=red
}
"""

SDL_B = """STATEDESC Foo
{
    VERSION 2
    VAR BOOL    fooVis[1]   DEFAULT=0 DEFAULTOPTION=VAULT
    VAR POINT3  fooPos[1]   DEFAULT=(0,0,0)
}
STATEDESC Foo
{
    VERSION 3
    VAR BOOL    fooVis[1]   DEFAULT=0
}
"""


class Pak(unittest.TestCase):
    def test_py2_27(self):
        raw = py2_code([100, 0, 0, 114, 7, 0, 100, 0, 0, 83])
        fam, det = A.classify(raw)
        self.assertEqual(fam, "py2")
        self.assertTrue(det["py2_marshal_v1"])
        self.assertTrue(det["py2_pop_jump"])
        v = A.version_verdict([{"family": fam, "details": det}])
        self.assertEqual(v["py2"]["python"], "2.7")

    def test_py2_26(self):
        raw = py2_code([100, 0, 0, 111, 4, 0, 1, 100, 0, 0, 83])
        fam, det = A.classify(raw)
        self.assertEqual(fam, "py2")
        v = A.version_verdict([{"family": fam, "details": det}])
        self.assertEqual(v["py2"]["python"], "2.4-2.6")

    def test_py3_host(self):
        raw = marshal.dumps(compile("import os\nx = [i for i in range(3)]\n", "m.py", "exec"))
        fam, det = A.classify(raw)
        self.assertEqual(fam, "py3.11+" if sys.version_info >= (3, 11) else fam)
        self.assertNotIn("also_fits", det)

    def test_empty_and_garbage(self):
        self.assertEqual(A.classify(b"")[0], "empty")
        self.assertEqual(A.classify(b"N")[0], "not-code")
        self.assertEqual(A.classify(b"c" + b"\0" * 7)[0], "unparsed")

    def test_read_pak_and_inverted_names(self):
        data = make_pak([("xKI.py", py2_code([100, 0, 0, 83])), ("empty.py", b"")])
        names = [e[0] for e in A.read_pak(data)]
        self.assertEqual(names, ["xKI.py", "empty.py"])
        inv = bytes(~b & 0xFF for b in b"abc")
        self.assertEqual(A.read_safe_string(struct.pack("<H", 3 | 0xF000) + inv, 0)[0], "abc")


class BuildCheck(unittest.TestCase):
    """A pak is refused for a client build whose interpreter cannot read it (exit 3)."""

    def run_cli(self, folder, *args):
        import contextlib
        import io

        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = A.main([str(folder), "--stdlib", str(Path(folder) / "none"), *args])
        return rc, buf.getvalue()

    def folder(self, tmp, entries):
        (Path(tmp) / "python").mkdir()
        (Path(tmp) / "python/python.pak").write_bytes(make_pak(entries))
        return tmp

    def test_classify_23_and_312(self):
        fam, det = A.classify(py23_code([100, 0, 0, 111, 4, 0, 1, 100, 0, 0, 83]))
        self.assertEqual(
            (fam, det["type_codes"], det["py2_ops_not_in_2_3"], det["first_byte"]), ("py2", "(Ncs", [], "0x63")
        )
        self.assertIsNone(A.build_refusal("py2", fam, det))
        self.assertEqual(A.build_refusal("py3", fam, det), "marshal family py2, not py3.11+")
        fam, det = A.classify(py312_code())
        self.assertEqual((fam, det["resume_opcode"]), ("py3.11+", 151))
        self.assertIsNone(A.build_refusal("py3", fam, det))
        self.assertEqual(A.build_refusal("py2", fam, det), "marshal family py3.11+, not py2")
        fam, det = A.classify(py312_code(149))  # 3.13
        self.assertIn("not RESUME 151", A.build_refusal("py3", fam, det))
        fam, det = A.classify(py2_code([100, 0, 0, 114, 7, 0, 100, 0, 0, 83]))  # 2.7: 't'/'R', POP_JUMP_IF_FALSE
        self.assertEqual(A.build_refusal("py2", fam, det), "marshal type codes Rt (not in CPython 2.3.3 marshal.c)")
        fam, det = A.classify(py23_code([100, 0, 0, 114, 7, 0, 100, 0, 0, 83]))  # 2.3 strings, 2.7 opcode
        self.assertEqual(A.build_refusal("py2", fam, det), "opcodes 114 (not in CPython 2.3.3 opcode.h)")

    def test_py2_pak_against_both_builds(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.folder(
                tmp,
                [
                    ("xKI.py", py23_code([100, 0, 0, 111, 4, 0, 1, 100, 0, 0, 83])),
                    ("Cleft.py", py23_code([100, 0, 0, 83])),
                    ("empty.py", b""),
                ],
            )
            rc, out = self.run_cli(tmp, "--build", "py2")
            self.assertEqual(rc, 0, out)
            self.assertIn("build check (py2: plClient-py2, CPython 2.3.3): PASS", out)
            self.assertIn("BUILD CHECK PASS", out)
            rc, out = self.run_cli(tmp, "--build", "py3")
            self.assertEqual(rc, 3, out)
            self.assertIn("REFUSED, 2 entries do not run on it: marshal family py2, not py3.11+ x2", out)
            self.assertIn(
                "found: families {'py2': 2}, leading bytes {'0x63': 2}, type codes '(Ncs', first opcodes {}", out
            )
            self.assertIn(".pyc magic none", out)
            self.assertIn("BUILD CHECK REFUSED", out)
            rc, out = self.run_cli(tmp)  # no build: report only, exit 0
            self.assertEqual(rc, 0)
            self.assertNotIn("BUILD CHECK", out)

    def test_py3_pak_against_both_builds(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.folder(tmp, [("xKI.py", py312_code()), ("Cleft.py", py312_code())])
            rc, out = self.run_cli(tmp, "--build", "py3")
            self.assertEqual(rc, 0, out)
            rc, out = self.run_cli(tmp, "--build", "py2")
            self.assertEqual(rc, 3, out)
            self.assertIn("marshal family py3.11+, not py2 x2", out)
            self.assertIn("first opcodes {151: 2}", out)

    def test_mixed_pak_refused_by_both(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.folder(tmp, [("a.py", py312_code()), ("b.py", py23_code([100, 0, 0, 83]))])
            self.assertEqual(self.run_cli(tmp, "--build", "py3")[0], 3)
            self.assertEqual(self.run_cli(tmp, "--build", "py2")[0], 3)

    def test_undecided_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as tmp:  # encrypted: cannot be checked
            (Path(tmp) / "Python").mkdir()
            (Path(tmp) / "Python/python.pak").write_bytes(b"notthedroids" + b"\0" * 20)
            rc, out = self.run_cli(tmp, "--build", "py2")
            self.assertEqual(rc, 3)
            self.assertIn("undecided (encrypted, unreadable) ['python/python.pak']", out)
        with tempfile.TemporaryDirectory() as tmp:  # no pak at all
            rc, out = self.run_cli(tmp, "--build", "py3")
            self.assertEqual(rc, 3)
            self.assertIn("no pak in the input", out)

    def test_build_from_the_deployment(self):
        # --deployment: data.gameCode server-py2 = the py2 build; another mode = py3; a conflicting --build fails
        from test_deployment import Fixture, ini_text

        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, ini_text(), data={"files": "patcher", "gameCode": "server-py2"})
            g = Path(tmp) / "capture"
            g.mkdir()
            self.folder(g, [("xKI.py", py23_code([100, 0, 0, 83]))])
            rc, out = self.run_cli(g, "--deployment", str(f.path))
            self.assertEqual(rc, 0, out)
            self.assertIn("against the py2 build (plClient-py2, CPython 2.3.3)", out)
            f2 = Fixture(Path(tmp) / "py3", ini_text(), data={"files": "patcher", "gameCode": "server"})
            self.assertEqual(self.run_cli(g, "--deployment", str(f2.path))[0], 3)
            import contextlib
            import io

            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as e:
                self.run_cli(g, "--deployment", str(f.path), "--build", "py3")
            self.assertEqual(e.exception.code, 2)


class Sdl(unittest.TestCase):
    def test_parse(self):
        d = A.parse_sdl(SDL_A, "a.sdl")
        self.assertEqual([(x["name"], x["version"]) for x in d], [("Foo", 2), ("Bar", 1)])
        foo = {v["name"]: v for v in d[0]["vars"]}
        self.assertEqual(foo["fooPos"]["default"], "(0,0,0)")
        self.assertEqual(foo["fooList"]["count"], 0)
        self.assertEqual(foo["fooVis"]["defaultoption"], "VAULT")
        self.assertEqual(d[1]["vars"][0]["display"], ["red"])

    def test_diff(self):
        r = A.sdl_diff({"a.sdl": SDL_A}, {"b.sdl": SDL_B})
        by = {e["name"]: e for e in r["descriptors"]}
        self.assertEqual(by["Bar"]["status"], "captured-only")
        self.assertEqual(by["Foo"]["captured_versions"], [2])
        self.assertEqual(by["Foo"]["repo_versions"], [2, 3])
        ch = by["Foo"]["changes"][0]
        self.assertEqual(ch["captured_only_vars"], ["fooList"])
        self.assertIn("fooVis", ch["changed_vars"])

    def test_repo_sdl_parses(self):
        files = {f.name: f.read_text(encoding="latin-1") for f in (REPO / "Scripts/SDL").glob("*.sdl")}
        descs, dups = A.sdl_set(files)
        self.assertGreater(len(descs), 100)
        self.assertEqual(dups, [])
        self.assertTrue(all(v is not None for (_, v) in descs))


class EndToEnd(unittest.TestCase):
    def test_capture_zip(self):
        pak = make_pak(
            [
                ("xKI.py", py2_code([100, 0, 0, 114, 7, 0, 100, 0, 0, 83])),
                ("Cleft.py", py2_code([100, 0, 0, 83])),
                ("notInRepo.py", b""),
            ]
        )
        sdl = SDL_A.encode()
        import hashlib

        index = {
            "files": [
                {"name": "python/python.pak", "bytes": len(pak), "sha256": hashlib.sha256(pak).hexdigest()},
                {"name": "SDL/a.sdl", "bytes": len(sdl), "sha256": hashlib.sha256(sdl).hexdigest()},
            ]
        }
        with tempfile.TemporaryDirectory() as tmp:
            z = Path(tmp) / "capture.zip"
            with zipfile.ZipFile(z, "w") as zf:
                zf.writestr("moula/index.json", json.dumps(index))
                zf.writestr("moula/python/python.pak", pak)
                zf.writestr("moula/SDL/a.sdl", sdl)
            r = A.analyse(z, REPO, stdlib=Path(tmp) / "none")
        self.assertEqual(r["index_check"]["mismatched"], [])
        self.assertEqual(r["index_check"]["missing"], [])
        info = r["paks"]["python/python.pak"]
        self.assertEqual(info["verdict"]["py2"]["python"], "2.7")
        self.assertEqual(info["empty_entries"], 1)
        m = r["modules_vs_repo"]
        self.assertIn("notInRepo", m["pak_only_other"])
        self.assertIn("xKI", [n for n in ["xKI"] if n not in m["repo_only"]])
        self.assertIn("Foo", {e["name"] for e in r["sdl"]["diff"]["descriptors"]})
        text = A.summary(r)
        self.assertIn("py2", text)

    def test_encrypted(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "Python").mkdir()
            (Path(tmp) / "Python/python.pak").write_bytes(b"notthedroids" + b"\0" * 20)
            (Path(tmp) / "SDL").mkdir()
            (Path(tmp) / "SDL/a.sdl").write_bytes(b"whatdoyousee" + b"\0" * 20)
            r = A.analyse(tmp, REPO)
        self.assertEqual(r["paks"]["python/python.pak"]["encrypted"], "notthedroids")
        self.assertEqual(r["sdl"]["encrypted_files"], ["SDL/a.sdl"])
        self.assertNotIn("modules_vs_repo", r)


if __name__ == "__main__":
    unittest.main()
