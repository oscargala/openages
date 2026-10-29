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
"""Cut the pieces of the specialised fixed-function fragment programs out of FixedPipelineShaders.wgsl.

A specialised program (plWGPUFixedFragment) is the fixed-function module with its fragment entry replaced by one that
runs only the branches a draw's state selects: stageOp's branch for each op, sampleStage's sample line for each stage
that reads its texture, fogFactor's branch for the table fog mode, alphaTest's comparison, and fixedFragment's own
statements. This script cuts every one of those expressions from the source text and writes them, with the extent of
the four functions a program replaces (fixedFragment, sampleStage, stageOp, alphaTest), as the table
FixedFragmentTable.inc. The client only concatenates the table's pieces in a fixed order: it parses nothing. The table
carries the source's FNV-1a 64 hash and size; the client keeps every draw on the uber shader when the embedded source
is not the one the table was cut from, and the native test fails.

A program reads the interstage variables the uber reads, through the uber's own statements: every argument is the
loop's stageArg call on the stage's uniforms (stageArg stays in the module), every stage below kStages samples its
texture (a stage whose arguments do not read it through the loop's sampleStage call, sampleStage added back verbatim:
the signature records no cube bit for it), and SPECULARENABLE is fixedFragment's runtime statement on u.lighting.x.
Measured in Chromium client frames: a program that leaves in.specular or a stage's in.t<s> unread draws differently
from the uber on lit scenes (GreatZero 84 px, Gira from Kemo 36 px, at most 2 levels), and the uber with only
in.specular left unread draws the same frame as such a program (legs product, xns); reading in.specular alone gave
GreatZero 0 px and Gira 58 px (xa, xc), reading in.specular and every stage's in.t<s> gave both 0 px (xsa). The pinned
benchmark, a float target without blending, showed none of it. fixedFragment names in.t0 .. in.t7 in its coordinate
array; whether the compiled uber reads those at kStages and above is not known (the vertex stage writes only the
stages below kStages), and a program reads in.t0 .. in.t<kStages - 1>.

A program with no alpha function (ALPHATESTENABLE off, or ALPHAFUNC ALWAYS) carries the uber's own alpha test:
alphaTest itself and fixedFragment's statement, verbatim. Its draws have u.fog.z 0 or ALWAYS, so the statement never
discards (alphaTest's default is true), but the program keeps the uber's discard: on Dawn/Metal (Apple GPU) a fragment
program's texture samples depend on whether it contains a discard at all, even one never taken, where the derivatives
vary over a quad. Measured in Chromium on a perspective scene: 175 of 537 traced programs without it differed
(chromium-20261028-205151-geo-scene-f32), the same never-taken branch without a discard changed nothing
(chromium-20261028-205449-x-branch), and a never-taken discard made every program equal
(chromium-20261028-205356-x-discard2, chromium-20261028-205653-scene-synth-xd, chromium-20261028-210054-scene-pairs-xd).

The script refuses a source whose shape changed, both ways: every set it cuts must be complete and exact (stageOp's
branches are the listed D3DTOP values and the default `return a1;`, stageArg's sources DIFFUSE, TEXTURE, TFACTOR and
SPECULAR with the CURRENT default, the seven alpha functions with the default `true` and no ALWAYS branch, the three fog
modes with the default 1.0, the eight sample lines, fixedFragment's one discard in its alpha test on u.fog.z, the last
statement before the return, its coordinate array of in.t0 .. in.t7 with the stage loop's one sampleStage call and four
stageArg calls, its SPECULARENABLE statement right after `var rgb`), and the constants the signature compares hold
their D3D9 values.

Usage: fixed_fragment_table.py [<FixedPipelineShaders.wgsl> [<FixedFragmentTable.inc>]] [--check]
  The paths default to the ones of this checkout (this file is in ShaderSrc/gen/).
  --check  regenerate in memory and compare with the table, writing nothing: exit 0 when it is equal byte for byte,
           1 when it differs or is missing.
"""

import argparse, difflib, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER_SRC = os.path.dirname(HERE)  # pfWGPUPipeline/ShaderSrc
SOURCE = os.path.join(SHADER_SRC, "FixedPipelineShaders.wgsl")
TABLE = os.path.join(SHADER_SRC, "FixedFragmentTable.inc")
GENERATOR = "pfWGPUPipeline/ShaderSrc/gen/fixed_fragment_table.py"

LICENSE = """/*==LICENSE==*

CyanWorlds.com Engine - MMOG client, server and tools
Copyright (C) 2011  Cyan Worlds, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

Additional permissions under GNU GPL version 3 section 7

If you modify this Program, or any covered work, by linking or
combining it with any of RAD Game Tools Bink SDK, Autodesk 3ds Max SDK,
NVIDIA PhysX SDK, Microsoft DirectX SDK, OpenSSL library, Independent
JPEG Group JPEG library, Microsoft Windows Media SDK, or Apple QuickTime SDK
(or a modified version of those libraries),
containing parts covered by the terms of the Bink SDK EULA, 3ds Max EULA,
PhysX SDK EULA, DirectX SDK EULA, OpenSSL and SSLeay licenses, IJG
JPEG Library README, Windows Media SDK EULA, or QuickTime SDK EULA, the
licensors of this Program grant you additional
permission to convey the resulting work. Corresponding Source for a
non-source form of such a combination shall include the source code for
the parts of OpenSSL and IJG JPEG Library used as well as that of the covered
work.

You can contact Cyan Worlds, Inc. by email legal@cyan.com
 or by snail mail at:
      Cyan Worlds, Inc.
      14617 N Newport Hwy
      Mead, WA   99021

*==LICENSE==*/
"""

# The D3DTEXTUREOP values with a stageOp branch (d3d9types.h), and the D3D9 values of the constants the canonical
# signature (plWGPUFixedFragment::CanonicalStage) compares.
LISTED = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20, 21, 24}
DOT3 = 24
D3D_VALUES = {
    "TOP_DISABLE": 1,
    "TOP_SELECTARG1": 2,
    "TOP_SELECTARG2": 3,
    "TOP_BLENDTEXTUREALPHA": 13,
    "TOP_BLENDTEXTUREALPHAPM": 15,
    "TOP_DOTPRODUCT3": 24,
    "TA_DIFFUSE": 0,
    "TA_CURRENT": 1,
    "TA_TEXTURE": 2,
    "TA_TFACTOR": 3,
    "TA_SPECULAR": 4,
    "TA_COMPLEMENT": 0x10,
    "TA_ALPHAREPLICATE": 0x20,
    "CMP_NEVER": 1,
    "CMP_LESS": 2,
    "CMP_EQUAL": 3,
    "CMP_LESSEQUAL": 4,
    "CMP_GREATER": 5,
    "CMP_NOTEQUAL": 6,
    "CMP_GREATEREQUAL": 7,
    "CMP_ALWAYS": 8,
    "FOG_EXP": 1,
    "FOG_EXP2": 2,
    "FOG_LINEAR": 3,
}
TA_CURRENT = 1
FUNCTIONS = ["fixedFragment", "sampleStage", "stageOp", "alphaTest"]  # the functions a program replaces


class SourceChanged(Exception):
    pass


def fail(what):
    raise SourceChanged("source changed: " + what)


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def close_brace(text, open_at):
    depth = 0
    for j in range(open_at, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return j
    fail("unbalanced braces")


def fn_range(text, name):
    """[begin, end) of `fn name(` with its entry attribute and body."""
    m = re.search(r"(@(vertex|fragment)\s+)?\bfn\s+" + name + r"\s*\(", text)
    if not m:
        fail("no fn " + name)
    return m.start(), close_brace(text, text.index("{", m.start())) + 1


def fn_text(text, name):
    b, e = fn_range(text, name)
    return text[b:e]


def block(text, head):
    """The trimmed body of the block that opens with `head` (whose last character is the brace)."""
    at = text.find(head)
    if at < 0:
        return None
    open_at = at + len(head) - 1
    return text[open_at + 1 : close_brace(text, open_at)].strip()


def group(text, pattern, what):
    m = re.search(pattern, text)
    if not m:
        fail(what)
    return m.group(1)


def indent_body(body):
    return "\n    ".join(line.strip() for line in body.split("\n"))


def word(text, w, by, count=0):
    return re.sub(r"\b" + re.escape(w) + r"\b", lambda _m: by, text, count=count)


def cut(src):
    """Every piece of the programs, from the source text."""
    consts = {}
    for m in re.finditer(r"const (\w+): u32 = (0x[0-9a-f]+|\d+)u;", src):
        consts[m.group(1)] = int(m.group(2), 0)
    for k, v in D3D_VALUES.items():
        if consts.get(k) != v:
            fail(f"{k} is not {v}")

    def name(prefix, v):
        for k, x in consts.items():
            if k.startswith(prefix) and x == v:
                return k
        fail(f"no {prefix} constant for {v}")

    # stageOp: `if (op == TOP_X) { return EXPR; }`, DOTPRODUCT3's a block, the default `return a1;`
    op = fn_text(src, "stageOp")
    op_body = {}
    for m in re.finditer(r"if \(op == (TOP_\w+)\) \{ return (.*); \}\n", op):
        op_body[consts[m.group(1)]] = "return " + m.group(2) + ";"
    dot3 = block(op, "if (op == TOP_DOTPRODUCT3) {")
    if dot3 is None:
        fail("stageOp has no DOTPRODUCT3 block")
    op_body[DOT3] = dot3
    if op.count("if (op == ") != len(op_body):
        fail("stageOp has a branch of another shape")
    if set(op_body) != LISTED:
        fail("stageOp's ops are not the listed ones")
    if not re.search(r"\n    return a1;\n\}$", op):
        fail("stageOp's default is not `return a1;`")

    # stageArg (kept in the programs' modules, which call it as the loop does): the sources, the CURRENT default, the
    # two modifiers, which the canonical signature's arguments (plWGPUFixedFragment::CanonicalStage) assume
    arg = fn_text(src, "stageArg")
    arg_src = {TA_CURRENT: group(arg, r"var a = (\w+);", "stageArg has no `var a`")}
    for m in re.finditer(r"which == (TA_\w+)\) \{\s*a = ([^;]+);", arg):
        arg_src[consts[m.group(1)]] = m.group(2)
    if arg.count("which == ") != 4 or set(arg_src) != {0, 1, 2, 3, 4}:
        fail("stageArg's sources are not DIFFUSE, TEXTURE, TFACTOR, SPECULAR and the CURRENT default")
    group(arg, r"TA_COMPLEMENT\) != 0u\) \{\s*a = ([^;]+);", "stageArg has no COMPLEMENT")
    group(arg, r"TA_ALPHAREPLICATE\) != 0u\) \{\s*a = ([^;]+);", "stageArg has no ALPHAREPLICATE")
    if arg.count("(sel & ") != 2:
        fail("stageArg's modifiers")

    # sampleStage: the eight sample lines, the coordinate and the bias (s and t its parameters)
    samp = fn_text(src, "sampleStage")
    flat, cube = {}, {}
    for m in re.finditer(r"if \(s == (\d)u\) \{ if \(cube\) \{ return (.*?); \} return (.*?); \}", samp):
        cube[int(m.group(1))], flat[int(m.group(1))] = m.group(2), m.group(3)
    m = re.search(
        r"if \(cube\) \{ return (textureSampleBias\(cube7.*?); \}\s*return (textureSampleBias\(tex7.*?);", samp
    )
    if not m:
        fail("sampleStage has no stage-7 tail")
    cube[7], flat[7] = m.group(1), m.group(2)
    if set(flat) != set(range(8)) or samp.count("if (s == ") != 7 or samp.count("textureSampleBias(") != 16:
        fail("sampleStage's sample lines are not the eight stages'")
    coord = group(samp, r"let c = (sampleCoord\(s, t\));", "sampleStage's coordinate")
    group(samp, r"let cube = (u\.stages\[s\]\.flags\.x != 0u);", "sampleStage's cube test")
    bias = group(samp, r"let bias = (u\.stages\[s\]\.bias\.x);", "sampleStage's bias")
    sc = fn_text(src, "sampleCoord")  # stays in the programs' modules: the projection is per draw
    group(sc, r"let q = (.*);", "sampleCoord")
    group(sc, r"return (vec4f\(t\.xyz / q, 1\.0\));", "sampleCoord")

    # fogFactor: the three table modes (their values the source's constants) and the default 1.0
    fogf = fn_text(src, "fogFactor")
    fog_body = {}
    for mode_name in ("FOG_LINEAR", "FOG_EXP", "FOG_EXP2"):
        body = block(fogf, f"if (mode == {mode_name}) {{")
        if body is None:
            fail("fogFactor has no " + mode_name)
        fog_body[consts[mode_name]] = body
    if fogf.count("if (mode == ") != 3 or not re.search(r"\n    return 1\.0;\n\}$", fogf):
        fail("fogFactor's modes are not EXP, EXP2, LINEAR and the default 1.0")

    # alphaTest: the quantised alpha, the seven functions and the default true
    at = fn_text(src, "alphaTest")
    a8 = group(at, r"let a8 = (.*);", "alphaTest has no a8")
    test = {}
    for m in re.finditer(r"if \(func == (CMP_\w+)\) \{ return (.*); \}", at):
        test[consts[m.group(1)]] = m.group(2)
    if at.count("if (func == ") != 7 or set(test) != set(range(1, 8)) or not re.search(r"\n    return true;\n\}$", at):
        fail("alphaTest's functions are not NEVER .. GREATEREQUAL and the default true")
    if "CMP_ALWAYS" in at:
        fail("alphaTest has an ALWAYS branch (a program without an alpha function relies on its default true)")

    # fixedFragment's own statements, which every program's entry repeats
    frag = fn_text(src, "fixedFragment")
    stmt = {
        "ClampCurrent": group(
            frag,
            r"current = (clamp\(vec4f\(color\.rgb, alpha\), vec4f\(0\.0\), vec4f\(1\.0\)\));",
            "fixedFragment's clamp",
        ),
        "FogMix": group(
            frag, r"let f = fogFactor\(u\.fog\.x, in\.fog\.x\);\s*rgb = (.*);", "fixedFragment's table fog"
        ),
        "VertexFog": group(frag, r"let f = (clamp\(in\.fog\.y, 0\.0, 1\.0\));", "fixedFragment's vertex fog"),
        "VertexFogMix": group(
            frag, r"let f = clamp\(in\.fog\.y, 0\.0, 1\.0\);\s*rgb = (.*);", "fixedFragment's vertex fog"
        ),
        "Return": group(frag, r"return (vec4f\(rgb, current\.a\));", "fixedFragment's return"),
    }

    # fixedFragment's stage loop: its coordinate array names in.t<s> at index s, `stage` is the stage's uniforms, and each
    # stage samples its texture and takes its arguments with these calls, which every program repeats per stage
    if "var coords = array<vec4f, 8>(" + ", ".join(f"in.t{s}" for s in range(8)) + ");" not in frag:
        fail("fixedFragment's coordinate array is not in.t0 .. in.t7")
    group(frag, r"let stage = (u\.stages\[s\]);", "fixedFragment's stage uniforms")
    loop_sample = group(frag, r"let tex = (sampleStage\(s, coords\[s\]\));", "fixedFragment's sample")
    loop_arg = {
        k: group(
            frag,
            r"let " + k + r" = (stageArg\(stage\." + f + r", diffuse, current, tex, specular\));",
            "fixedFragment's " + k,
        )
        for k, f in (("c1", r"ops\.y"), ("c2", r"ops\.z"), ("a1", r"ops2\.x"), ("a2", r"ops2\.y"))
    }
    if frag.count("stageArg(") != 4 or frag.count("sampleStage(") != 1:
        fail("fixedFragment's stage loop has other stageArg or sampleStage calls")

    # fixedFragment's SPECULARENABLE statement, which every program repeats verbatim right after `var rgb`
    spec_at = re.findall(
        r"\n    var rgb = current\.rgb;\n(    if \(u\.lighting\.x != 0u\) \{\n"
        r"        rgb = clamp\(rgb \+ specular\.rgb, vec3f\(0\.0\), vec3f\(1\.0\)\);\n    \})\n",
        frag,
    )
    if len(spec_at) != 1 or frag.count("u.lighting.x") != 1:
        fail("fixedFragment's specular statement is not the one after `var rgb`")
    stmt["SpecStmt"] = spec_at[0]

    # fixedFragment's alpha test, which a program without an alpha function repeats verbatim: the entry's one discard,
    # in its last statement before the return, the runtime test on u.fog.z (0, or ALWAYS, for such a program's draws:
    # it never discards, alphaTest's default being true)
    if frag.count("discard;") != 1:
        fail("fixedFragment has not exactly one discard")
    test_at = re.search(
        r"\n(    if \((.*)\) \{\n        discard;\n    \})\n    return vec4f\(rgb, current\.a\);\n\}$", frag
    )
    if not test_at:
        fail("fixedFragment's alpha test is not its last statement before the return")
    guard, _, call = test_at.group(2).partition(" && !")
    if guard != "u.fog.z != 0u":
        fail("fixedFragment's alpha test is not guarded by `u.fog.z != 0u`")
    if call != "alphaTest(u.fog.z, current.a, u.fog.w)":
        fail("fixedFragment's alpha test does not call `alphaTest(u.fog.z, current.a, u.fog.w)`")

    # the four functions a program replaces: their extents, which must not overlap
    cuts = sorted((fn_range(src, n) for n in FUNCTIONS), reverse=True)
    for (b1, _), (_, e2) in zip(cuts, cuts[1:]):
        if e2 > b1:
            fail("the replaced functions overlap")

    # the pieces as the programs use them
    def in_stage(e, s):
        return e.replace("coords[s]", f"in.t{s}").replace("stage.", f"u.stages[{s}u].").replace("(s, ", f"({s}u, ")

    def at_stage(e, s):
        return word(word(e, "s", f"{s}u"), "t", f"in.t{s}")

    def helper(fn, body):
        return (
            f"fn {fn}(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {{\n    "
            + indent_body(body)
            + "\n}\n"
        )

    op_names, op_fns = {}, {}
    for o in [0] + sorted(LISTED):
        n = "op_" + (name("TOP_", o)[4:].lower() if o else "default")
        op_names[o], op_fns[o] = n, helper(n, op_body[o] if o else "return a1;")
    fog_fns = {
        m: "fn fog_factor(d: f32) -> f32 {\n    " + indent_body(fog_body[m] if m else "return 1.0;") + "\n}\n"
        for m in range(4)
    }
    return {
        "cuts": cuts,
        "op_names": op_names,
        "op_fns": op_fns,
        "args": {k: [in_stage(v, s) for s in range(8)] for k, v in loop_arg.items()},
        "sample_call": [in_stage(loop_sample, s) for s in range(8)],
        "sample_fn": samp + "\n",
        "coord": [at_stage(coord, s) for s in range(8)],
        "bias": [at_stage(bias, s) for s in range(8)],
        "flat": [flat[s] for s in range(8)],
        "cube": [cube[s] for s in range(8)],
        "fog_fns": fog_fns,
        "alpha8": word(a8, "a", "current.a", count=1),
        "test": {f: test[f].replace("ref_", "u.fog.w") for f in range(1, 8)},
        "alpha_fn": at + "\n",
        "alpha_stmt": test_at.group(1),
        "stmt": stmt,
    }


def c_string(text):
    out = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return '"' + out + '"'


def table(src_bytes):
    src = src_bytes.decode("utf-8")
    P = cut(src)
    L = LICENSE.split("\n") + [
        "// The pieces of the specialised fixed-function fragment programs (plWGPUFixedFragment), cut from",
        f"// FixedPipelineShaders.wgsl by {GENERATOR}. Do not edit: regenerate.",
        "// The client uses the table only with the source it was cut from (kSourceFnv, kSourceSize); otherwise every",
        "// draw keeps the uber shader.",
        "",
        f"constexpr uint64_t kSourceFnv = 0x{fnv1a64(src_bytes):016x}ULL;",
        f"constexpr size_t   kSourceSize = {len(src_bytes)};",
        "",
        "// The functions a program replaces (" + ", ".join(FUNCTIONS) + "):",
        "// [begin, end) in the source, last first.",
        "constexpr size_t kCut[" + str(len(P["cuts"])) + "][2] = {",
    ]
    L += [f"    {{ {b}, {e} }}," for b, e in P["cuts"]]
    L += [
        "};",
        "",
        "// stageOp's branch per D3DTOP value as a helper function, and its name (0: an unlisted op, Arg1).",
    ]
    L += ["const char* const kOpName[25] = {"]
    L += [f"    {c_string(P['op_names'][o]) if o in P['op_names'] else 'nullptr'}," for o in range(25)]
    L += ["};", "const char* const kOpFn[25] = {"]
    L += [f"    {c_string(P['op_fns'][o]) if o in P['op_fns'] else 'nullptr'}," for o in range(25)]
    L += [
        "};",
        "",
        "// fixedFragment's stageArg calls per stage: COLORARG1, COLORARG2, ALPHAARG1, ALPHAARG2.",
    ]
    for k in ("c1", "c2", "a1", "a2"):
        L += [f"const char* const kArg{k.upper()}[8] = {{"] + [f"    {c_string(x)}," for x in P["args"][k]] + ["};"]
    L += ["", "// sampleStage per stage: the coordinate, the bias, the 2D sample and the cube sample."]
    for key, cname in (("coord", "kCoord"), ("bias", "kBias"), ("flat", "kFlat"), ("cube", "kCube")):
        L += [f"const char* const {cname}[8] = {{"] + [f"    {c_string(x)}," for x in P[key]] + ["};"]
    L += [
        "// A stage that does not read its texture: fixedFragment's sampleStage call per stage, and sampleStage itself."
    ]
    L += ["const char* const kSampleStage[8] = {"] + [f"    {c_string(x)}," for x in P["sample_call"]] + ["};"]
    L += [f"const char* const kSampleStageFn = {c_string(P['sample_fn'])};"]
    L += ["", "// fogFactor's branch per table fog mode as fog_factor (0: no mode, 1.0)."]
    L += ["const char* const kFogFn[4] = {"] + [f"    {c_string(P['fog_fns'][m])}," for m in range(4)] + ["};"]
    L += ["", "// alphaTest: the quantised alpha of current.a, and the comparison per D3DCMP function (1 .. 7)."]
    L += [f"const char* const kAlpha8 = {c_string(P['alpha8'])};", "const char* const kTest[8] = {", "    nullptr,"]
    L += [f"    {c_string(P['test'][f])}," for f in range(1, 8)] + ["};"]
    L += [
        "// With no alpha function (0): alphaTest itself, and fixedFragment's alpha-test statement.",
        f"const char* const kAlphaTestFn = {c_string(P['alpha_fn'])};",
        f"const char* const kAlphaTestStmt = {c_string(P['alpha_stmt'])};",
    ]
    L += ["", "// fixedFragment's statements, which every program's entry repeats."]
    L += [f"const char* const k{k} = {c_string(v)};" for k, v in P["stmt"].items()]
    return "\n".join(L) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description="Cut the specialised fixed-function fragment programs' pieces.")
    ap.add_argument("src", nargs="?", default=SOURCE, help="FixedPipelineShaders.wgsl (default: this checkout's)")
    ap.add_argument("dst", nargs="?", default=TABLE, help="FixedFragmentTable.inc (default: this checkout's)")
    ap.add_argument("--check", action="store_true", help="compare with dst instead of writing it")
    args = ap.parse_args(argv)
    with open(args.src, "rb") as f:
        src = f.read()
    try:
        text = table(src)
    except SourceChanged as e:
        print(f"{os.path.basename(args.src)}: {e}")
        return 1
    name = os.path.basename(args.dst)
    if args.check:
        have = open(args.dst, "rb").read() if os.path.exists(args.dst) else None
        if have == text.encode("utf-8"):
            print(f"EQUAL    {name}\nregeneration check: PASS")
            return 0
        print(("MISSING  " if have is None else "DIFFERS  ") + name)
        if have is not None:
            for k, line in enumerate(
                difflib.unified_diff(
                    have.decode("utf-8", "replace").split("\n"),
                    text.split("\n"),
                    f"{name} (committed)",
                    f"{name} (regenerated)",
                    lineterm="",
                    n=1,
                )
            ):
                if k == 40:
                    print("    ...")
                    break
                print("    " + line)
        print("regeneration check: FAIL")
        return 1
    with open(args.dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"{name}: {len(src)} bytes of source, FNV-1a 64 {fnv1a64(src):016x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
