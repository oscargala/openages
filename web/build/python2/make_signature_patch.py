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
"""Apply the mechanical signature fixes the scan found (scan_signatures.py).

    make_signature_patch.py <scan.json> <source tree to edit in place>

The tree is a copy (patchwork/b); the patch is `diff -ru a b`. Each fix gives the callee the parameter list
CPython 2.3 calls it with, so a direct wasm call_indirect does not trap:
  - METH_NOARGS methods: CPython calls meth(self, NULL) (PyCFunction_Call, Objects/methodobject.c:82; call_function,
    Python/ceval.c:3425);
    a static one-parameter definition gets a second `PyObject *unused` parameter.
  - getters: called as get(obj, closure) (Objects/descrobject.c getset_get); setters as set(obj, value, closure)
    (getset_set); one gets `void *closure` appended.
  - a public API function used as a method (its prototype is in include/) or a void tp_clear (called as an
    inquiry, int result) is not changed; a static wrapper with the called signature is added after it and the
    table entry points at the wrapper.
Definitions are matched by the callee's name at column 0 followed by its parameter list, which is how every
CPython 2.3 definition is written; forward declarations in the same file are updated the same way.
"""

import json, re, sys
from pathlib import Path

rows = json.load(open(sys.argv[1]))
root = Path(sys.argv[2])
todo = {}
for r in rows:
    if r["verdict"] != "mismatch":
        continue
    if r["dst"] == "PyCFunction" and r["flags"] == ["METH_NOARGS"]:
        kind = "noargs"
    elif r["dst"] in ("getter", "setter"):
        kind = r["dst"]
    elif r["dst"] == "inquiry" and r["src_wasm"][0] == "":
        kind = "void_clear"
    else:
        continue  # by-construction dispatch casts, read by hand
    todo.setdefault(r["file"], {}).setdefault(r["callee"], set()).add(kind)

EXTRA = {"noargs": "PyObject *unused", "getter": "void *closure", "setter": "void *closure"}
report = []
for file, callees in sorted(todo.items()):
    p = root / file
    text = p.read_text()
    for name, kinds in sorted(callees.items()):
        (kind,) = kinds
        defn = re.compile(
            r"^(?P<pre>(?:static\s+[^\n;]*?\b)?)" + re.escape(name) + r"\((?P<params>[^)]*)\)(?P<post>\s*(?:;|\n\{|$))",
            re.M,
        )
        hits = list(defn.finditer(text))
        is_static = bool(re.search(r"^static[^\n;]*\n" + re.escape(name) + r"\(", text, re.M)) or bool(
            re.search(r"^static[^\n;]*\b" + re.escape(name) + r"\(", text, re.M)
        )
        if kind == "void_clear":
            # int wrapper after the definition's closing brace; the slot entry is repointed
            m = re.search(r"^" + re.escape(name) + r"\((?P<params>[^)]*)\)\s*\n\{", text, re.M)
            assert m, (file, name)
            end = text.index("\n}\n", m.end()) + 3
            ptype = m["params"].rsplit("*", 1)[0] + "*"
            wrap = (
                f"\n/* tp_clear is called as an inquiry (int result). */\nstatic int\n"
                f"{name}_inquiry({ptype}self)\n{{\n\t{name}(self);\n\treturn 0;\n}}\n"
            )
            text = text[:end] + wrap + text[end:]
            text = text.replace(f"(inquiry){name}", f"(inquiry){name}_inquiry")
            report.append(f"{file}: {name} -> wrapper {name}_inquiry")
            continue
        if not is_static:
            # a public API function (prototype in include/): wrapper before the method table that uses it
            assert kind == "noargs", (file, name, kind)
            wrap = (
                f"/* METH_NOARGS is called as meth(self, NULL). */\nstatic PyObject *\n"
                f"{name}_noargs(PyObject *self, PyObject *unused)\n{{\n\treturn {name}(self);\n}}\n\n"
            )
            tbl = text.index(f"(PyCFunction){name}")
            start = text.rfind("\nstatic PyMethodDef", 0, tbl) + 1
            text = text[:start] + wrap + text[start:]
            text = text.replace(f"(PyCFunction){name},", f"(PyCFunction){name}_noargs,")
            report.append(f"{file}: {name} (public API) -> wrapper {name}_noargs")
            continue

        def add(m):
            params = m["params"].strip()
            assert params and params != "void" and "," not in params or kind == "setter", (file, name, params)
            return f"{m['pre']}{name}({m['params']}, {EXTRA[kind]}){m['post']}"

        new, n = defn.subn(add, text)
        assert n >= 1, (file, name)
        text = new
        report.append(f"{file}: {name} +{EXTRA[kind]} ({n} site{'s' if n > 1 else ''})")
    p.write_text(text)
print("\n".join(report))
print(f"{len(report)} functions in {len(todo)} files")
