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
"""Classify clang -Wcast-function-type-strict warnings by WebAssembly signature.

    scan_signatures.py <scan log> <patched source root> <out.json>

A wasm call_indirect traps when the caller's and the callee's signatures differ: parameter count, parameter value
types or result type. Pointers, int, long, char, short and enums are all i32 on wasm32; long long is i64; float f32;
double f64; a variadic function takes its fixed parameters plus one i32 (the argument buffer).

For a cast to PyCFunction the call signature is not the target type alone: CPython 2.3 calls the function by the
PyMethodDef flags (PyCFunction_Call in Objects/methodobject.c, call_function in Python/ceval.c, Objects/descrobject.c):
METH_KEYWORDS -> (self, args, kw); METH_NOARGS -> (self, NULL); METH_O, METH_VARARGS, METH_OLDARGS -> 2 args.
The flags are read from the method table entry in the source.
"""

import json, re, sys, collections
from pathlib import Path

WARN = re.compile(
    r"^(?P<file>[^:\s]+):(?P<line>\d+):(?P<col>\d+): warning: cast from (?P<src>'[^']*'(?: \(aka '[^']*'\))?) "
    r"to (?P<dst>'[^']*'(?: \(aka '[^']*'\))?) converts to incompatible function type"
)


# Casts read by hand: the call site dispatches on data that guarantees the callee's real signature.
REVIEWED = {
    (
        "Objects/cobject.c",
        "destr",
    ): "stored as destructor1, called as destructor2 only when desc is set (cobject.c:104-108)",
    (
        "Objects/cobject.c",
        "(destructor2)(self->destructor))(self->c",
    ): "the desc branch of PyCObject_dealloc (cobject.c:106)",
    (
        "Objects/descrobject.c",
        "wrapper",
    ): "cast to wrapperfunc_kwds only under PyWrapperFlag_KEYWORDS (descrobject.c:950-953)",
    ("Objects/typeobject.c", "wrap_call"): "slotdef with PyWrapperFlag_KEYWORDS, called through wrapperfunc_kwds",
    ("Objects/typeobject.c", "wrap_init"): "slotdef with PyWrapperFlag_KEYWORDS, called through wrapperfunc_kwds",
    (
        "Objects/typeobject.c",
        "tp_new_wrapper",
    ): "pointer comparison, not a call (typeobject.c:5162-5164); table entry is METH_KEYWORDS",
    ("Objects/methodobject.c", "meth"): "the METH_KEYWORDS branch of PyCFunction_Call (methodobject.c:75-77)",
    ("Modules/_sre.c", "scanner_match"): "flags 0 = METH_OLDARGS, called meth(self, arg): 2 parameters",
    ("Modules/_sre.c", "scanner_search"): "flags 0 = METH_OLDARGS, called meth(self, arg): 2 parameters",
}


def canonical(t):
    m = re.match(r"'([^']*)'(?: \(aka '([^']*)'\))?", t)
    return m.group(2) or m.group(1)


def split_top(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def value_type(t):
    t = t.strip()
    if "*" in t or "(" in t or "[" in t:
        return "i32"
    t = re.sub(r"\b(const|volatile|signed|unsigned|struct|enum|register)\b", "", t).strip()
    if t == "void":
        return ""
    if re.search(r"\blong\s+long\b", t) or t in ("PY_LONG_LONG", "int64_t", "uint64_t", "Py_off_t", "off_t", "time_t"):
        return "i64"
    if t == "double":
        return "f64"
    if t == "float":
        return "f32"
    return "i32"


def wasm_sig(fn):
    """'R (*)(A, B)' -> ('R', ('A', 'B'), variadic) as wasm value types."""
    fn = fn.strip()
    i = fn.find("(*)")
    if i < 0:
        raise ValueError(fn)
    ret = fn[:i]
    params = fn[i + 3 :].strip()
    assert params.startswith("(") and params.endswith(")"), fn
    plist = split_top(params[1:-1])
    variadic = bool(plist) and plist[-1] == "..."
    if variadic:
        plist = plist[:-1]
    if plist == ["void"]:
        plist = []
    p = tuple(value_type(x) for x in plist)
    if variadic:
        p = p + ("i32",)
    return value_type(ret), p


def meth_flags(src_root, file, line):
    lines = Path(src_root, file).read_text(errors="replace").splitlines()
    text = " ".join(lines[line - 1 : line + 3])
    text = text.split("}")[0]
    return set(re.findall(r"METH_[A-Z]+", text))


def callee_name(src_root, file, line, col):
    s = Path(src_root, file).read_text(errors="replace").splitlines()[line - 1][col - 1 :]
    m = re.match(r"\(\s*[\w\s\*]+\)\s*&?\s*(\w+)", s)
    return m.group(1) if m else s[:40].strip()


def main():
    log, src_root, out = sys.argv[1:4]
    rows = []
    seen = set()
    for raw in Path(log).read_text(errors="replace").splitlines():
        m = WARN.match(raw)
        if not m:
            continue
        key = (m["file"], m["line"], m["col"])
        if key in seen:  # a header warning repeats once per unit
            continue
        seen.add(key)
        src, dst = canonical(m["src"]), canonical(m["dst"])
        s_sig, d_sig = wasm_sig(src), wasm_sig(dst)
        dst_name = re.match(r"'([^']*)'", m["dst"]).group(1)
        call = d_sig
        flags = None
        if dst_name == "PyCFunction":
            flags = sorted(meth_flags(src_root, m["file"], int(m["line"])))
            if "METH_KEYWORDS" in flags:
                call = ("i32", ("i32", "i32", "i32"))
            elif not flags:
                call = None  # flags not found next to the cast: read by hand
        verdict = "unresolved" if call is None else ("match" if s_sig == call else "mismatch")
        callee = callee_name(src_root, m["file"], int(m["line"]), int(m["col"]))
        reviewed = REVIEWED.get((m["file"], callee))
        if verdict != "match" and reviewed:
            verdict = "reviewed"
        rows.append(
            dict(
                file=m["file"],
                line=int(m["line"]),
                col=int(m["col"]),
                callee=callee,
                reviewed=reviewed,
                src=src,
                dst=dst_name,
                src_wasm=list(s_sig),
                call_wasm=list(call) if call else None,
                flags=flags,
                verdict=verdict,
            )
        )
    by = collections.Counter((r["dst"], r["verdict"]) for r in rows)
    print(
        f"casts: {len(rows)}  match: {sum(r['verdict'] == 'match' for r in rows)}  "
        f"reviewed: {sum(r['verdict'] == 'reviewed' for r in rows)}  mismatch: {sum(r['verdict'] == 'mismatch' for r in rows)}  unresolved: {sum(r['verdict'] == 'unresolved' for r in rows)}"
    )
    print("by target type and verdict:")
    for (dst, v), n in sorted(by.items()):
        print(f"  {dst:28} {v:10} {n}")
    cls = collections.Counter(
        (r["dst"], tuple(r["flags"] or ()), str(r["src_wasm"]), str(r["call_wasm"]))
        for r in rows
        if r["verdict"] in ("mismatch", "unresolved")
    )
    print("non-matching classes (target, flags, callee wasm, call wasm):")
    for k, n in cls.most_common():
        print(f"  {n:4}  {k}")
    files = collections.Counter(r["file"] for r in rows if r["verdict"] in ("mismatch", "unresolved"))
    print("non-matching by file:")
    for f, n in sorted(files.items()):
        print(f"  {f:36} {n}")
    for r in rows:
        if r["verdict"] != "match":
            print(
                f"  {r['verdict']:10} {r['file']}:{r['line']} {r['callee']} {r['src']} -> {r['dst']} {r['flags'] or ''}"
                + (f"  [{r['reviewed']}]" if r["reviewed"] else "")
            )
    Path(out).write_text(json.dumps(rows, indent=1))
    return 1 if any(r["verdict"] in ("mismatch", "unresolved") for r in rows) else 0


if __name__ == "__main__":
    sys.exit(main())
