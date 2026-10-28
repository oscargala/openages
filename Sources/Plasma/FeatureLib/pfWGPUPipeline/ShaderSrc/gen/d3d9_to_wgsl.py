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
"""Translate the D3D9 vs_1_1/ps_1_1 programs of plSurface/<name>.h into WGSL.

The bytecode (`<name>Codes[]`, what plShaderTable hands to CreateVertexShader/CreatePixelShader in
plDXVertexShader::ICreate and plDXPixelShader::ICreate) defines what each program does; the module
structure follows pfMetalPipeline. Each instruction token becomes one WGSL statement, with the
instruction as a comment; the D3D9 semantics live in ShaderPrelude.wgsl (rcp, rsq, expp, frc, the
ps_1_1 range, the output clamps) and are cited there. Each program becomes one entry point
(vs_<name> or ps_<name>) that runs it once and hands its registers to the prelude's vsFinish or
psFinish.

Families are named as pfMetalPipeline/ShaderSrc/*.metal. The 14 programs translated are the ones
pfMetalPipeline ports (plMetalDynamicMaterialPipelineState::GetVertexFunction and
GetFragmentFunction); the pipeline reports any other program as not supported.

Inputs: the 14 upstream headers Sources/Plasma/PubUtilLib/plSurface/<name>.h named in FAMILIES
(upstream's Tools/plShaderAssembler assembled them from plSurface/ShaderSrc/<name>.inl). Outputs:
the 7 family modules <family>.wgsl and ShaderPrograms.inc in pfWGPUPipeline/ShaderSrc, each
starting with the upstream licence block.

Usage: d3d9_to_wgsl.py [<plSurface dir> [<ShaderSrc dir>]] [--check]
  The directories default to the ones of this checkout (this file is in ShaderSrc/gen/).
  --check  regenerate in memory and compare with the files in <ShaderSrc dir>, writing nothing:
           exit 0 when every file is equal byte for byte, 1 when any differs or is missing.
"""

import argparse, difflib, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d3d9_disasm import load_codes, disasm, reg_type, OPS, USAGE  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER_SRC = os.path.dirname(HERE)  # pfWGPUPipeline/ShaderSrc
PLSURFACE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(SHADER_SRC))), "PubUtilLib", "plSurface"
)  # Sources/Plasma/PubUtilLib/plSurface
GENERATOR = "pfWGPUPipeline/ShaderSrc/gen/d3d9_to_wgsl.py"

# Upstream's licence block in the C comment form, as every upstream source file carries it.
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

FAMILIES = [
    ("WaveSet7", ["vs_WaveFixedFin7", "ps_WaveFixed"]),
    ("CompCosines", ["vs_CompCosines", "ps_MoreCosines"]),
    ("BiasNormals", ["vs_BiasNormals", "ps_BiasNormals"]),
    ("Grass", ["vs_GrassShader", "ps_GrassShader"]),
    ("WaveDec1Lay_7", ["vs_WaveDec1Lay_7", "ps_CbaseAbase"]),
    ("WaveDecEnv", ["vs_WaveDecEnv_7", "ps_WaveDecEnv"]),
    ("WaveRip", ["vs_WaveRip7", "ps_WaveRip"]),
]

COMP = "xyzw"


class Program:
    def __init__(self, name, tokens):
        self.name = name
        self.tokens = tokens
        self.pixel = (tokens[0] >> 16) == 0xFFFF
        self.listing = disasm(tokens)


def swz(t):
    sw = (t >> 16) & 0xFF
    return "".join(COMP[(sw >> (2 * i)) & 3] for i in range(4))


def mask_of(t):
    return (t >> 16) & 0xF


class Translator:
    def __init__(self, prog):
        self.p = prog
        self.lines = []
        self.vs_inputs = {}  # register number -> WGSL input expression (from dcl)
        self.defs = {}  # ps def constants
        self.texcoord_read = set()
        self.pad_srcs = []
        self.written = set()  # vs output registers written
        self.read = set()  # ps inputs read (d0, d1, t0..t3)
        self.samples = 0  # ps: the stages a tex/texm3x3vspec instruction samples
        self.prev_dest = None

    # Register and operand expressions -------------------------------------------------
    def reg(self, t, as_dest=False):
        rt, n = reg_type(t), t & 0x7FF
        pixel = self.p.pixel
        if rt == 0:
            return f"r{n}"
        if rt == 1:
            if pixel:
                self.read.add(f"oD{n}")
                return f"v{n}"
            if n not in self.vs_inputs:
                raise ValueError(f"{self.p.name}: v{n} read without dcl")
            return f"v{n}"
        if rt == 2:
            if pixel:
                return f"pconst({n}u)" if n not in self.defs else f"def{n}"
            return f"c({n}u)"
        if rt == 3:
            if pixel:
                return f"t{n}"
            raise ValueError("address register not supported (no translated program uses a0)")
        if rt == 4:
            return ["o.oPos", "o.oFog", "o.oPts"][n]
        if rt == 5:
            return f"o.oD{n}"
        if rt == 6:
            return f"o.oT{n}"
        raise ValueError(f"register type {rt}")

    def src(self, t):
        base = self.reg(t)
        if t & (1 << 13):
            raise ValueError("relative addressing not supported (no translated program uses it)")
        s = swz(t)
        e = base if s == "xyzw" else f"{base}.{s}"
        mod = (t >> 24) & 0xF
        if mod == 0:
            return e
        if mod == 1:
            return f"(-{e})"
        if mod == 2:
            return f"bias({e})"
        if mod == 3:
            return f"(-bias({e}))"
        if mod == 4:
            return f"bx2({e})"
        if mod == 5:
            return f"(-bx2({e}))"
        if mod == 6:
            return f"(vec4f(1.0) - {e})"
        if mod == 7:
            return f"(2.0 * {e})"
        if mod == 8:
            return f"(-2.0 * {e})"
        raise ValueError(f"source modifier {mod}")

    def write(self, dt, expr, comment):
        """Assign a 4-vector expression to the destination under its write mask."""
        d = self.reg(dt, True)
        mask = mask_of(dt)
        shift = (dt >> 24) & 0xF
        mods = (dt >> 20) & 0xF
        if shift:
            raise ValueError("instruction shift modifiers not supported (none in the translated programs)")
        if self.p.pixel:
            # ps_1_1: every arithmetic result in [-1, 1] (MaxPixelShaderValue).
            expr = f"pc({expr})"
        if mods & 1:
            expr = f"clamp({expr}, vec4f(0.0), vec4f(1.0))"
        if d.startswith("o."):
            self.written.add(d[2:])
        if d == "o.oFog":
            # oFog is a scalar register: the instruction's x result.
            self.lines.append(f"    {d} = ({expr}).x; // {comment}")
            return
        if mask == 0xF:
            self.lines.append(f"    {d} = {expr}; // {comment}")
        else:
            bools = ", ".join("true" if mask & (1 << i) else "false" for i in range(4))
            self.lines.append(f"    {d} = wmask({d}, {expr}, vec4<bool>({bools})); // {comment}")

    # Instructions ---------------------------------------------------------------------
    def translate(self):
        tokens = self.p.tokens
        i = 1
        listing = iter(self.p.listing[1:])
        while i < len(tokens):
            t = tokens[i]
            if t == 0x0000FFFF:
                break
            op = t & 0xFFFF
            if op == 0xFFFE:
                i += 1 + ((t >> 16) & 0x7FFF)
                continue
            name = OPS[op]
            text = next(listing)
            i += 1
            if name == "dcl":
                usage, dt = tokens[i], tokens[i + 1]
                i += 2
                u, idx = usage & 0x1F, (usage >> 16) & 0xF
                n = dt & 0x7FF
                if USAGE[u] == "position" and idx == 0:
                    e = "in.position"
                elif USAGE[u] == "color" and idx == 0:
                    e = "d3dColor(in.diffuse)"
                elif USAGE[u] == "color" and idx == 1:
                    e = "d3dColor(in.specular)"
                elif USAGE[u] == "texcoord" and idx < 4:
                    e = f"in.uvw{idx}"
                else:
                    raise ValueError(f"{self.p.name}: dcl usage {USAGE[u]}{idx}")
                self.vs_inputs[n] = e
                self.lines.append(f"    let v{n} = {e}; // {text}")
                continue
            if name == "def":
                dt = tokens[i]
                vals = struct.unpack("<4f", struct.pack("<4I", *tokens[i + 1 : i + 5]))
                i += 5
                n = dt & 0x7FF
                self.defs[n] = vals
                self.lines.append(f"    let def{n} = vec4f({', '.join(repr(float(v)) for v in vals)}); // {text}")
                continue
            dt = tokens[i]
            i += 1
            if t & (1 << 30) and self.prev_dest is not None:
                pass  # checked below once the sources are known
            nargs = {
                "mov": 1,
                "add": 2,
                "sub": 2,
                "mad": 3,
                "mul": 2,
                "rcp": 1,
                "rsq": 1,
                "dp3": 2,
                "dp4": 2,
                "min": 2,
                "max": 2,
                "slt": 2,
                "sge": 2,
                "expp": 1,
                "frc": 1,
                "m4x4": 2,
                "m4x3": 2,
                "lrp": 3,
                "tex": 0,
                "texm3x3pad": 1,
                "texm3x3vspec": 1,
            }[name]
            raw = tokens[i : i + nargs]
            i += nargs
            if t & (1 << 30):
                # ps_1_1 co-issue: both halves read their sources before either writes; the pairs in
                # the translated programs write disjoint channels and never read the pair's destination.
                for s_ in raw:
                    if (s_ & 0x780007FF) == (self.prev_dest & 0x780007FF) and ((s_ >> 8) & 0x18) == (
                        (self.prev_dest >> 8) & 0x18
                    ):
                        raise ValueError(f"{self.p.name}: co-issued instruction reads its pair's destination")
            self.prev_dest = dt
            a = [self.src(s) for s in raw]
            if name == "mov":
                self.write(dt, a[0], text)
            elif name == "add":
                self.write(dt, f"{a[0]} + {a[1]}", text)
            elif name == "sub":
                self.write(dt, f"{a[0]} - {a[1]}", text)
            elif name == "mul":
                self.write(dt, f"{a[0]} * {a[1]}", text)
            elif name == "mad":
                self.write(dt, f"{a[0]} * {a[1]} + {a[2]}", text)
            elif name == "lrp":
                self.write(dt, f"{a[0]} * ({a[1]} - {a[2]}) + {a[2]}", text)
            elif name == "min":
                self.write(dt, f"min({a[0]}, {a[1]})", text)
            elif name == "max":
                self.write(dt, f"max({a[0]}, {a[1]})", text)
            elif name == "dp3":
                self.write(dt, f"vec4f(dot(({a[0]}).xyz, ({a[1]}).xyz))", text)
            elif name == "dp4":
                self.write(dt, f"vec4f(dot({a[0]}, {a[1]}))", text)
            elif name == "rcp":
                self.write(dt, f"vec4f(rcp(({a[0]}).w))", text)
            elif name == "rsq":
                self.write(dt, f"vec4f(rsq(({a[0]}).w))", text)
            elif name == "expp":
                self.write(dt, f"expp(({a[0]}).w)", text)
            elif name == "frc":
                self.write(dt, f"frc({a[0]})", text)
            elif name in ("m4x4", "m4x3"):
                if reg_type(raw[1]) != 2 or ((raw[1] >> 16) & 0xFF) != 0xE4 or (raw[1] >> 24) & 0xF:
                    raise ValueError("matrix operand must be a plain constant")
                r = raw[1] & 0x7FF
                if name == "m4x4":
                    self.write(dt, f"m4x4({a[0]}, {r}u)", text)
                else:
                    self.write(dt, f"vec4f(m4x3({a[0]}, {r}u), 0.0)", text)
            elif name == "tex":
                n = dt & 0x7FF
                self.texcoord_read.add(n)
                self.read.add(f"oT{n}")
                self.samples |= 1 << n
                self.lines.append(f"    t{n} = texStage({n}u, in.t{n}); // {text}")
            elif name == "texm3x3pad":
                n = dt & 0x7FF
                self.texcoord_read.add(n)
                self.read.add(f"oT{n}")
                self.lines.append(f"    // {text}: row {n} of the texm3x3 product (folded into texm3x3vspec)")
                self.pad_srcs.append(a[0])
            elif name == "texm3x3vspec":
                n = dt & 0x7FF
                self.texcoord_read.add(n)
                self.read.add(f"oT{n}")
                self.samples |= 1 << n
                if self.pad_srcs != [a[0], a[0]]:
                    raise ValueError("texm3x3pad/vspec rows must read one source register")
                self.lines.append(
                    f"    t{n} = texm3x3vspec({n}u, in.t{n - 2}, in.t{n - 1}, in.t{n}, {a[0]}); // {text}"
                )
            else:
                raise ValueError(f"{self.p.name}: {name} not supported")
        return self.lines


def vs_function(prog):
    tr = Translator(prog)
    body = tr.translate()
    prog.tr = tr
    out = [
        f"// {prog.name}: plSurface/{prog.name}.h, {len(prog.tokens)} tokens",
        f"fn {prog.name}_run(in: VertexIn) -> VSRegs {{",
        "    var o: VSRegs;",
        "    o.oPos = vec4f(0.0, 0.0, 0.0, 1.0);",
        "    o.oFog = 1.0;   // unwritten output registers: the family's pixel program never reads them",
    ]
    for r in range(12):
        out.append(f"    var r{r} = vec4f(0.0);")
    out += body
    out.append("    return o;")
    out.append("}")
    out.append("")
    out.append("@vertex")
    out.append(f"fn {prog.name}(in: VertexIn) -> VSOut {{")
    out.append(f"    return vsFinish({prog.name}_run(in));")
    out.append("}")
    return out


def ps_function(prog):
    tr = Translator(prog)
    body = tr.translate()
    prog.tr = tr
    out = [
        f"// {prog.name}: plSurface/{prog.name}.h, {len(prog.tokens)} tokens",
        f"fn {prog.name}_run(in: VSOut) -> vec4f {{",
        "    let v0 = in.d0;",
        "    let v1 = in.d1;",
        "    var r0 = vec4f(0.0);",
        "    var r1 = vec4f(0.0);",
        "    var t0 = vec4f(0.0);",
        "    var t1 = vec4f(0.0);",
        "    var t2 = vec4f(0.0);",
        "    var t3 = vec4f(0.0);",
    ]
    out += body
    out.append("    return r0;")
    out.append("}")
    out.append("")
    out.append("@fragment")
    out.append(f"fn {prog.name}(in: VSOut) -> @location(0) vec4f {{")
    out.append(f"    return psFinish({prog.name}_run(in), in.fog);")
    out.append("}")
    return out


def generate(src):
    """{file name: text} of every generated file, from the plSurface headers in src."""
    files = {}
    table = []
    for family, names in FAMILIES:
        lines = LICENSE.split("\n") + [
            f"// The {family} program family (pfMetalPipeline/ShaderSrc/{family}.metal names it),",
            f"// translated from the D3D9 bytecode by {GENERATOR}. Do not edit:",
            "// regenerate. Each statement is one instruction token, the instruction in its comment.",
            "// ShaderPrelude.wgsl precedes this text in the module (plWGPUShader.cpp).",
            "",
        ]
        progs = []
        for name in names:
            prog = Program(name, load_codes(os.path.join(src, name + ".h")))
            lines += vs_function(prog) if not prog.pixel else ps_function(prog)
            lines.append("")
            progs.append(prog)
        vs, ps = progs
        # The family's vertex and pixel programs are drawn as a pair: every input the pixel program
        # reads is an output its vertex program writes, so no unwritten output register is ever read.
        missing = ps.tr.read - vs.tr.written
        if missing:
            raise ValueError(f"{family}: {ps.name} reads {missing} that {vs.name} never writes")
        for prog in progs:
            table.append((prog.name, family, prog.pixel, "oFog" in prog.tr.written, prog.tr.samples, len(prog.tokens)))
        files[family + ".wgsl"] = "\n".join(lines)
    inc = [
        LICENSE + "\n",
        "// The 14 translated shader programs, generated by\n"
        f"// {GENERATOR} with their WGSL. Do not edit: regenerate.\n"
        "// { plShaderID, module family, WGSL entry point, pixel program, writes oFog, stages sampled, tokens }\n",
    ]
    for name, family, pixel, fog, samples, ntok in table:
        inc.append(
            f'{{ plShaderID::{name}, "{family}", "{name}", {"true" if pixel else "false"}, '
            f"{'true' if fog else 'false'}, 0x{samples:x}, {ntok} }},\n"
        )
    files["ShaderPrograms.inc"] = "".join(inc)
    return files


def check(files, dst):
    """Compare the generated text with the files in dst; the number of files that differ."""
    bad = 0
    for name, text in files.items():
        path = os.path.join(dst, name)
        if not os.path.exists(path):
            print(f"MISSING  {name}")
            bad += 1
            continue
        with open(path, "rb") as f:
            have = f.read()
        if have == text.encode("utf-8"):
            print(f"EQUAL    {name}")
            continue
        bad += 1
        print(f"DIFFERS  {name}")
        diff = difflib.unified_diff(
            have.decode("utf-8", "replace").split("\n"),
            text.split("\n"),
            f"{name} (committed)",
            f"{name} (regenerated)",
            lineterm="",
            n=1,
        )
        for k, line in enumerate(diff):
            if k == 40:
                print("    ...")
                break
            print("    " + line)
    print(f"regeneration check: {len(files) - bad} of {len(files)} files equal: {'PASS' if not bad else 'FAIL'}")
    return bad


def main(argv=None):
    ap = argparse.ArgumentParser(description="Translate the D3D9 shader programs of the seven families into WGSL.")
    ap.add_argument("src", nargs="?", default=PLSURFACE, help="the plSurface folder (default: this checkout's)")
    ap.add_argument("dst", nargs="?", default=SHADER_SRC, help="the ShaderSrc folder (default: this checkout's)")
    ap.add_argument("--check", action="store_true", help="compare with the files in dst instead of writing them")
    args = ap.parse_args(argv)
    files = generate(args.src)
    if args.check:
        return 1 if check(files, args.dst) else 0
    for name, text in files.items():
        with open(os.path.join(args.dst, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        if name.endswith(".wgsl"):
            print(f"{name}: {', '.join(dict(FAMILIES)[name[:-5]])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
