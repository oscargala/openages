/*==LICENSE==*

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

#include "plWGPUFixedFragment.h"

#include <cstdio>
#include <string_view>

#include "ShaderSrc/ShaderTypes.h"

namespace
{
// D3D9 values the canonical form names (d3d9types.h D3DTEXTUREOP, D3DTA_*). The listed ops are those the uber's
// stageOp has a branch for; any other op returns Arg1 there (op 0 here).
enum : uint32_t
{
    kTopDisable = 1,
    kTopSelectArg1 = 2,
    kTopSelectArg2 = 3,
    kTopBlendTextureAlpha = 13,
    kTopBlendTextureAlphaPM = 15,
    kTopDotProduct3 = 24,
    kTaDiffuse = 0,
    kTaCurrent = 1,
    kTaTexture = 2,
    kTaTFactor = 3,
    kTaSpecular = 4,
    kTaComplement = 0x10,
    kTaAlphaReplicate = 0x20,
};
constexpr uint32_t kListedOps[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20, 21, 24};

// The table of the pieces: ShaderSrc/FixedFragmentTable.inc, cut from FixedPipelineShaders.wgsl by
// ShaderSrc/gen/fixed_fragment_table.py.
#include "ShaderSrc/FixedFragmentTable.inc"

bool IListed(uint32_t op)
{
    for (uint32_t l : kListedOps)
        if (l == op)
            return true;
    return false;
}

// The arguments an op reads: SELECTARG1 (and an unlisted op) Arg1, SELECTARG2 Arg2, every other op both.
void IArgsRead(uint32_t op, bool& r1, bool& r2)
{
    r1 = op != kTopSelectArg2;
    r2 = !(op == kTopSelectArg1 || !IListed(op));
}

// An argument as the uber's stageArg reads it: a source it does not name reads CURRENT; COMPLEMENT always matters,
// ALPHAREPLICATE only where the channels mix (the colour, and DOTPRODUCT3's alpha).
uint8_t ICanonArg(uint32_t sel, bool keepReplicate)
{
    uint32_t which = sel & 0x0f;
    if (which != kTaDiffuse && which != kTaTexture && which != kTaTFactor && which != kTaSpecular)
        which = kTaCurrent;
    return uint8_t(which | (sel & (kTaComplement | (keepReplicate ? kTaAlphaReplicate : 0))));
}

bool IIsTexture(uint8_t arg)
{
    return arg != plWGPUFixedFragmentSig::kUnread && (arg & 0x0f) == kTaTexture;
}

// The fallback domain's terms (InDomain). The six ops that round differently in a specialised program in some
// argument forms: ADDSMOOTH, BLENDTEXTUREALPHA, BLENDFACTORALPHA, MODULATEINVALPHA_ADDCOLOR, MODULATEINVCOLOR_ADDALPHA,
// DOTPRODUCT3.
bool IRisky(uint8_t op)
{
    return op == 11 || op == kTopBlendTextureAlpha || op == 14 || op == 20 || op == 21 || op == kTopDotProduct3;
}

// An argument's dependence set, one bit each: its source; CURRENT's is DIFFUSE at stage 0, and DIFFUSE, SPECULAR,
// TFACTOR and the earlier stages' textures (bit 8) at a later stage.
uint32_t IDepends(uint8_t a, uint32_t stage)
{
    const uint8_t src = a & 0x0f;
    if (src == kTaCurrent)
        return stage == 0 ? 1u << kTaDiffuse : (1u << kTaDiffuse) | (1u << kTaSpecular) | (1u << kTaTFactor) | (1u << 8);
    return 1u << src;
}

bool IReadsCurrent(uint8_t a)
{
    return a != plWGPUFixedFragmentSig::kUnread && (a & 0x0f) == kTaCurrent;
}

// One op's arguments in the admitted forms (InDomain).
bool IOpAdmitted(uint8_t op, uint8_t a1, uint8_t a2, uint32_t stage, bool colour)
{
    constexpr uint8_t kUnread = plWGPUFixedFragmentSig::kUnread;
    if (!IRisky(op))
        return true;
    if (((a1 == kUnread ? 0 : a1) | (a2 == kUnread ? 0 : a2)) & (kTaComplement | kTaAlphaReplicate))
        return false;
    if (a1 != kUnread && a2 != kUnread && (IDepends(a1, stage) & IDepends(a2, stage)))
        return false;
    if (op == kTopBlendTextureAlpha && !colour && a2 != kUnread && (IDepends(a2, stage) & (1u << kTaTexture)))
        return false;
    if (op == 14 && a2 != kUnread && (IDepends(a2, stage) & (1u << kTaTFactor)))
        return false;
    return true;
}

uint64_t IFnv1a64(const char* text, size_t size)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < size; i++)
        h = (h ^ uint8_t(text[i])) * 0x100000001b3ULL;
    return h;
}
} // namespace

size_t plWGPUFixedFragmentSigHash::operator()(const plWGPUFixedFragmentSig& s) const noexcept
{
    return std::hash<std::string_view>()(std::string_view(reinterpret_cast<const char*>(&s), sizeof(s)));
}

plWGPUFixedFragmentSig::Stage plWGPUFixedFragment::CanonicalStage(uint32_t op, uint32_t a1, uint32_t a2, uint32_t aop,
                                                                  uint32_t aa1, uint32_t aa2, bool cube)
{
    using Sig = plWGPUFixedFragmentSig;
    Sig::Stage st{};
    bool       r1, r2;
    IArgsRead(op, r1, r2);
    st.op = IListed(op) ? uint8_t(op) : 0;
    st.c1 = r1 ? ICanonArg(a1, true) : Sig::kUnread;
    st.c2 = r2 ? ICanonArg(a2, true) : Sig::kUnread;
    bool tex = IIsTexture(st.c1) || IIsTexture(st.c2) || op == kTopBlendTextureAlpha || op == kTopBlendTextureAlphaPM;
    st.aop = st.a1 = st.a2 = Sig::kUnread;
    if (op == kTopDotProduct3) {
        st.alpha = Sig::kAlphaDot3;
    } else if (aop == kTopDisable) {
        st.alpha = Sig::kAlphaCurrent;
    } else {
        bool q1, q2;
        IArgsRead(aop, q1, q2);
        const bool rep = aop == kTopDotProduct3;
        st.alpha = Sig::kAlphaOp;
        st.aop = IListed(aop) ? uint8_t(aop) : 0;
        st.a1 = q1 ? ICanonArg(aa1, rep) : Sig::kUnread;
        st.a2 = q2 ? ICanonArg(aa2, rep) : Sig::kUnread;
        tex = tex || IIsTexture(st.a1) || IIsTexture(st.a2) || aop == kTopBlendTextureAlpha ||
              aop == kTopBlendTextureAlphaPM;
    }
    st.sampled = tex ? 1 : 0;
    st.cube = tex && cube ? 1 : 0;
    return st;
}

plWGPUFixedFragmentSig plWGPUFixedFragment::Signature(const plWGPUFFUniforms& u, uint32_t stages, uint32_t fog)
{
    plWGPUFixedFragmentSig sig{};
    sig.stages = uint8_t(stages);
    sig.fog = uint8_t(fog);
    sig.fogMode = fog == 1 && u.fog[0] >= 1 && u.fog[0] <= 3 ? uint8_t(u.fog[0]) : 0;
    sig.spec = u.lighting[0] ? 1 : 0;
    sig.func = u.fog[2] >= 1 && u.fog[2] <= 7 ? uint8_t(u.fog[2]) : 0;
    for (uint32_t s = 0; s < stages && s < 8; s++) {
        const plWGPUFFStage& st = u.stages[s];
        sig.st[s] = CanonicalStage(st.ops[0], st.ops[1], st.ops[2], st.ops[3], st.ops2[0], st.ops2[1], st.flags[0] != 0);
    }
    return sig;
}

bool plWGPUFixedFragment::InDomain(const plWGPUFixedFragmentSig& sig)
{
    bool dot3Before = false;
    for (uint32_t s = 0; s < sig.stages && s < 8; s++) {
        const plWGPUFixedFragmentSig::Stage& st = sig.st[s];
        const bool                           alphaOp = st.alpha == plWGPUFixedFragmentSig::kAlphaOp;
        if (!IOpAdmitted(st.op, st.c1, st.c2, s, true))
            return false;
        if (alphaOp && (!IOpAdmitted(st.aop, st.a1, st.a2, s, false) || (IRisky(st.op) && IRisky(st.aop))))
            return false;
        if (dot3Before && ((IRisky(st.op) && (IReadsCurrent(st.c1) || IReadsCurrent(st.c2))) ||
                           (alphaOp && IRisky(st.aop) && (IReadsCurrent(st.a1) || IReadsCurrent(st.a2)))))
            return false;
        dot3Before = dot3Before || st.op == kTopDotProduct3 || (alphaOp && st.aop == kTopDotProduct3);
    }
    return true;
}

bool plWGPUFixedFragment::Bind(const char* source)
{
    fBound = false;
    fBase.clear();
    const size_t   size = std::char_traits<char>::length(source);
    const uint64_t hash = IFnv1a64(source, size);
    if (size != kSourceSize || hash != kSourceFnv) {
        char text[192];
        std::snprintf(text, sizeof(text),
                      "source changed: FixedPipelineShaders.wgsl is %zu bytes, FNV-1a 64 %016llx; the table's is %zu "
                      "bytes, %016llx (regenerate ShaderSrc/FixedFragmentTable.inc)",
                      size, (unsigned long long)hash, kSourceSize, (unsigned long long)kSourceFnv);
        fError = text;
        return false;
    }
    fBase.assign(source, size);
    for (const auto& c : kCut)
        fBase.erase(c[0], c[1] - c[0]);
    fError.clear();
    fBound = true;
    return true;
}

std::string plWGPUFixedFragment::Module(const plWGPUFixedFragmentSig& sig) const
{
    // The helpers (sampleStage, each op's function, fog_factor, then alphaTest) in first-use order, then the entry: the
    // uber's fixedFragment with its stage loop unrolled and each choice made. The entry reads the interstage variables
    // the uber reads, through the uber's own statements: each argument is the loop's stageArg call on the stage's
    // uniforms, every stage samples its texture (one whose arguments do not read it through the loop's sampleStage
    // call, the signature having no cube bit for it), and SPECULARENABLE is fixedFragment's runtime statement. A
    // program that leaves the uber's in.specular or a stage's in.t<s> unread draws differently on Dawn/Metal. With no
    // alpha function the entry keeps the uber's alpha test (alphaTest and its statement, which those draws never
    // take): a program's texture samples depend on whether it contains a discard at all. fixed_fragment_table.py says
    // where each was measured.
    const char* fns[2 * 8 + 3];
    size_t      nfns = 0;
    auto        helper = [&fns, &nfns](const char* text) {
        for (size_t i = 0; i < nfns; i++)
            if (fns[i] == text)
                return;
        fns[nfns++] = text;
    };
    std::string L;
    auto        line = [&L](std::initializer_list<std::string_view> parts) {
        if (!L.empty())
            L += '\n';
        for (std::string_view p : parts)
            L += p;
    };
    line({"@fragment"});
    line({"fn fixedFragment(in: VertexOut) -> @location(0) vec4f {"});
    line({"    let diffuse = in.diffuse;"});
    line({"    let specular = in.specular;"});
    line({"    var current = diffuse;"});
    for (uint32_t s = 0; s < sig.stages && s < 8; s++) {
        const plWGPUFixedFragmentSig::Stage& st = sig.st[s];
        line({"    {"});
        if (st.sampled) {
            line({"        let c = ", kCoord[s], ";"});
            line({"        let bias = ", kBias[s], ";"});
            line({"        let tex = ", st.cube ? kCube[s] : kFlat[s], ";"});
        } else {
            helper(kSampleStageFn);
            line({"        let tex = ", kSampleStage[s], ";"});
        }
        line({"        let c1 = ", kArgC1[s], ";"});
        line({"        let c2 = ", kArgC2[s], ";"});
        helper(kOpFn[st.op]);
        line({"        let color = ", kOpName[st.op], "(c1, c2, diffuse, current, tex);"});
        line({"        var alpha = current.a;"});
        if (st.alpha == plWGPUFixedFragmentSig::kAlphaDot3) {
            line({"        alpha = color.a;"});
        } else if (st.alpha == plWGPUFixedFragmentSig::kAlphaOp) {
            line({"        let a1 = ", kArgA1[s], ";"});
            line({"        let a2 = ", kArgA2[s], ";"});
            helper(kOpFn[st.aop]);
            line({"        alpha = ", kOpName[st.aop], "(a1, a2, diffuse, current, tex).a;"});
        }
        line({"        current = ", kClampCurrent, ";"});
        line({"    }"});
    }
    line({"    var rgb = current.rgb;"});
    line({kSpecStmt});
    if (sig.fog == 1) {
        helper(kFogFn[sig.fogMode]);
        line({"    {"});
        line({"        let f = fog_factor(in.fog.x);"});
        line({"        rgb = ", kFogMix, ";"});
        line({"    }"});
    } else if (sig.fog == 2) {
        line({"    {"});
        line({"        let f = ", kVertexFog, ";"});
        line({"        rgb = ", kVertexFogMix, ";"});
        line({"    }"});
    }
    if (sig.func) {
        line({"    {"});
        line({"        let a8 = ", kAlpha8, ";"});
        line({"        if (!(", kTest[sig.func], ")) {"});
        line({"            discard;"});
        line({"        }"});
        line({"    }"});
    } else {
        helper(kAlphaTestFn);
        line({kAlphaTestStmt});
    }
    line({"    return ", kReturn, ";"});
    line({"}"});

    std::string out = fBase;
    out += '\n';
    for (size_t i = 0; i < nfns; i++) {
        if (i)
            out += '\n';
        out += fns[i];
    }
    out += '\n';
    out += L;
    out += '\n';
    return out;
}
