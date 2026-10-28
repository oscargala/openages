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
"""Disassemble the D3D9 vs_1_1/ps_1_1 token streams of plSurface/<name>.h.

The bytecode in `static const uint8_t <name>Codes[]` is what plShaderTable hands to
CreateVertexShader/CreatePixelShader (plDXVertexShader::ICreate, plDXPixelShader::ICreate).
Token layout per the D3D9 DDK "Shader Code Format" (version, instruction, parameter, comment and
end tokens). Usage: d3d9_disasm.py <plSurface dir> <name> [...]  (prints one listing per program).
"""

import re, struct, sys, os

OPS = {
    0: "nop",
    1: "mov",
    2: "add",
    3: "sub",
    4: "mad",
    5: "mul",
    6: "rcp",
    7: "rsq",
    8: "dp3",
    9: "dp4",
    10: "min",
    11: "max",
    12: "slt",
    13: "sge",
    14: "exp",
    15: "log",
    16: "lit",
    17: "dst",
    18: "lrp",
    19: "frc",
    20: "m4x4",
    21: "m4x3",
    22: "m3x4",
    23: "m3x3",
    24: "m3x2",
    31: "dcl",
    64: "texcoord",
    65: "texkill",
    66: "tex",
    67: "texbem",
    68: "texbeml",
    69: "texreg2ar",
    70: "texreg2gb",
    71: "texm3x2pad",
    72: "texm3x2tex",
    73: "texm3x3pad",
    74: "texm3x3tex",
    76: "texm3x3spec",
    77: "texm3x3vspec",
    78: "expp",
    79: "logp",
    80: "cnd",
    81: "def",
    88: "cmp",
    0xFFFD: "phase",
}
USAGE = {
    0: "position",
    1: "blendweight",
    2: "blendindices",
    3: "normal",
    4: "psize",
    5: "texcoord",
    6: "tangent",
    7: "binormal",
    8: "tessfactor",
    9: "positiont",
    10: "color",
    11: "fog",
    12: "depth",
    13: "sample",
}
SRCMOD = {
    0: "{}",
    1: "-{}",
    2: "{}_bias",
    3: "-{}_bias",
    4: "{}_bx2",
    5: "-{}_bx2",
    6: "1-{}",
    7: "{}_x2",
    8: "-{}_x2",
    9: "{}_dz",
    10: "{}_dw",
    11: "{}_abs",
    12: "-{}_abs",
    13: "!{}",
}
NARGS = {
    "nop": 0,
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
    "exp": 1,
    "log": 1,
    "lit": 1,
    "dst": 2,
    "lrp": 3,
    "frc": 1,
    "m4x4": 2,
    "m4x3": 2,
    "m3x4": 2,
    "m3x3": 2,
    "m3x2": 2,
    "texcoord": 0,
    "texkill": 0,
    "tex": 0,
    "texbem": 1,
    "texbeml": 1,
    "texreg2ar": 1,
    "texreg2gb": 1,
    "texm3x2pad": 1,
    "texm3x2tex": 1,
    "texm3x3pad": 1,
    "texm3x3tex": 1,
    "texm3x3spec": 2,
    "texm3x3vspec": 1,
    "expp": 1,
    "logp": 1,
    "cnd": 3,
    "cmp": 3,
}


def reg_type(t):
    return ((t >> 28) & 7) | ((t >> 8) & 0x18)


def reg_name(t, pixel):
    rt, n = reg_type(t), t & 0x7FF
    if rt == 0:
        return f"r{n}"
    if rt == 1:
        return f"v{n}"
    if rt == 2:
        return f"c{n}"
    if rt == 3:
        return f"t{n}" if pixel else f"a{n}"
    if rt == 4:
        return ["oPos", "oFog", "oPts"][n]
    if rt == 5:
        return f"oD{n}"
    if rt == 6:
        return f"oT{n}"
    return f"?{rt}:{n}"


def dst(t, pixel):
    mask = (t >> 16) & 0xF
    mods = (t >> 20) & 0xF
    shift = (t >> 24) & 0xF
    s = reg_name(t, pixel)
    if mask != 0xF:
        s += "." + "".join(c for i, c in enumerate("xyzw" if not pixel else "rgba") if mask & (1 << i))
    suffix = ""
    if shift:
        suffix += {1: "_x2", 2: "_x4", 3: "_x8", 15: "_d2", 14: "_d4", 13: "_d8"}.get(shift, f"_shift{shift}")
    if mods & 1:
        suffix += "_sat"
    return s, suffix


def src(t, pixel):
    sw = (t >> 16) & 0xFF
    mod = (t >> 24) & 0xF
    s = reg_name(t, pixel)
    if t & (1 << 13):
        s = s.replace("c", "c[a0.x+", 1) + "]"
    comps = [(sw >> (2 * i)) & 3 for i in range(4)]
    if comps != [0, 1, 2, 3]:
        names = "xyzw"
        st = "".join(names[c] for c in comps)
        if len(set(comps)) == 1:
            st = names[comps[0]]
        s += "." + st
    return SRCMOD.get(mod, "{}?" + str(mod)).format(s)


def load_codes(header):
    text = open(header).read()
    m = re.search(r"Codes\[\]\s*=\s*\{(.*?)\};", text, re.S)
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1)))
    return list(struct.unpack(f"<{len(data) // 4}I", data))


def disasm(tokens):
    out = []
    ver = tokens[0]
    pixel = (ver >> 16) == 0xFFFF
    out.append(("ps" if pixel else "vs") + f".{(ver >> 8) & 0xFF}.{ver & 0xFF}")
    i = 1
    while i < len(tokens):
        t = tokens[i]
        if t == 0x0000FFFF:
            out.append("end")
            break
        op = t & 0xFFFF
        if op == 0xFFFE:
            i += 1 + ((t >> 16) & 0x7FFF)
            continue
        name = OPS.get(op, f"op{op}")
        coissue = "+" if t & (1 << 30) else ""
        i += 1
        if name == "dcl":
            usage = tokens[i]
            d = tokens[i + 1]
            i += 2
            ds, _ = dst(d, pixel)
            idx = (usage >> 16) & 0xF
            out.append(f"dcl_{USAGE.get(usage & 0x1F, usage & 0x1F)}{idx if idx else ''} {ds}")
            continue
        if name == "def":
            ds, _ = dst(tokens[i], pixel)
            vals = struct.unpack("<4f", struct.pack("<4I", *tokens[i + 1 : i + 5]))
            i += 5
            out.append(f"def {ds}, " + ", ".join(f"{v:.9g}" for v in vals))
            continue
        ds, suffix = dst(tokens[i], pixel)
        i += 1
        args = []
        for _ in range(NARGS.get(name, 0)):
            args.append(src(tokens[i], pixel))
            i += 1
        out.append(f"{coissue}{name}{suffix} " + ", ".join([ds] + args))
    return out


if __name__ == "__main__":
    base = sys.argv[1]
    for name in sys.argv[2:]:
        print(f"// {name}.h")
        for n, line in enumerate(disasm(load_codes(os.path.join(base, name + ".h")))):
            print(f"{n:3d}  {line}")
        print()
