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

#include <gtest/gtest.h>
#include <openssl/evp.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "pfWGPUPipeline/ShaderSrc/ShaderTypes.h"
#include "pfWGPUPipeline/plWGPUFixedFragment.h"

// The fixed-function fragment programs the client generates (plWGPUFixedFragment) against the ones the image check
// compared with the uber shader: the bench generates them from the same source, draws each with the uber and with the
// program and compares the images bit for bit, in Chromium and in Safari. The bench draws five sets of draw states: the
// traced ones (the stage state real draws carried), and four it generates: ops (one feature each), pairs (the pairwise
// domain: every op at the colour and the alpha site, at stage 0 and stage 1, with every pair of argument sources and
// modifiers it reads), reachable (the stage compositions plDXPipeline's stage grammar produces) and features (each
// table fog mode, vertex fog, SPECULARENABLE, the alpha functions, a cube stage, one to eight stages). This test
// generates the same four sets (the functions below follow the bench generator's, step for step) and reads the traced
// states from fixed_fragment_programs.inc, the bench generator's output for the shader the client embeds:
//   kFixtureHead: "source <FNV-1a 64> <bytes>", "chromium <runs>", "webkit <runs>": the source and the runs that
//     proved the programs;
//   kFixtureTraced: "<stages> <fog> <spec> <FOGTABLEMODE> <ALPHAFUNC> | <COLOROP> <COLORARG1> <COLORARG2> <ALPHAOP>
//     <ALPHAARG1> <ALPHAARG2> <cube> | ...": one traced draw state each;
//   kFixtureSets: "<set> <states> <programs> <states SHA-256> <programs SHA-256>", per set, in the order traced, ops,
//     pairs, reachable, features: the number of states and of programs it adds (a program is its signature's, counted
//     in the first set that has it), the SHA-256 of its states' lines (the signature and the domain verdict, below) and
//     the SHA-256 of its programs' digests;
//   kFixtureShort: the first four bytes of each program's digest as eight hex digits, sixteen programs to a string, in
//     order (the programs of the sets in turn), so that a program whose text differs is named.
// A state's line: "P <stages> <fog> <fog mode> <spec> <func> | <op> <c1> <c2> <alpha> <aop> <a1> <a2> <sampled> <cube> |
// ... | <in domain>" (plWGPUFixedFragmentSig, unread fields 255, the domain verdict 1 or 0); a program's digest: the
// SHA-256 of its line, a newline and its module text. The client's generator must give the same states, signatures,
// verdicts and texts, so the image check's result holds for what the client compiles. The bench's lab keeps the
// programs' full texts for diffing a failure. When FixedPipelineShaders.wgsl changes, regenerate
// ShaderSrc/FixedFragmentTable.inc (ShaderSrc/gen/fixed_fragment_table.py), run the image check again in both browsers
// and regenerate this fixture with those runs: a fixture regenerated without new runs proves nothing.

namespace
{
// FixedPipelineShaders.wgsl as the client embeds it (pfWGPUPipeline's EmbedWGSL.cmake makes it a raw string literal,
// whose line breaks are newlines whatever the checkout's): read from the source tree, since a compiler may refuse a
// string literal of its size.
const std::string& Source()
{
    static const std::string text = [] {
        std::ifstream in(PLASMA_FIXED_PIPELINE_WGSL, std::ios::binary);
        std::string   raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string   out;
        out.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); i++)
            if (!(raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n'))
                out += raw[i];
        return out;
    }();
    return text;
}

uint64_t Fnv1a64(const std::string& text)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char b : text) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

std::vector<std::string> Split(const std::string& line, const std::string& sep)
{
    std::vector<std::string> out;
    size_t                   pos = 0;
    for (;;) {
        const size_t at = line.find(sep, pos);
        out.push_back(line.substr(pos, at == std::string::npos ? std::string::npos : at - pos));
        if (at == std::string::npos)
            return out;
        pos = at + sep.size();
    }
}

#include "fixed_fragment_programs.inc"

template <size_t N>
std::vector<std::string> Lines(const char* const (&a)[N])
{
    return std::vector<std::string>(a, a + N);
}

const plWGPUFixedFragment& Generator()
{
    static plWGPUFixedFragment gen;
    static bool                bound = gen.Bind(Source().c_str());
    (void)bound;
    return gen;
}

using Digest = std::array<unsigned char, 32>;

Digest Sha256(const std::string& data)
{
    Digest       out{};
    unsigned int n = 0;
    EVP_Digest(data.data(), data.size(), out.data(), &n, EVP_sha256(), nullptr);
    return out;
}

std::string Hex(const unsigned char* p, size_t n)
{
    static const char* const digits = "0123456789abcdef";
    std::string              out;
    for (size_t i = 0; i < n; i++) {
        out += digits[p[i] >> 4];
        out += digits[p[i] & 15];
    }
    return out;
}

// A draw state as the bench draws it: the record's stage count and fog kind, SPECULARENABLE, the table fog mode and
// the alpha function, and per stage COLOROP, COLORARG1, COLORARG2, ALPHAOP, ALPHAARG1, ALPHAARG2 and the cube bit.
using RawStage = std::array<uint32_t, 6>;
struct RawState
{
    uint32_t                stages = 0, fog = 0, spec = 0, fogTable = 0, alphaFunc = 0;
    std::array<RawStage, 8> st{};
    std::array<uint32_t, 8> cube{};
};

RawState State(const std::vector<RawStage>& stages, uint32_t fog, uint32_t fogTable = 0, uint32_t spec = 0,
               uint32_t alphaFunc = 0, std::vector<uint32_t> cubes = {})
{
    RawState r;
    r.stages = uint32_t(stages.size());
    r.fog = fog;
    r.fogTable = fogTable;
    r.spec = spec;
    r.alphaFunc = alphaFunc;
    for (size_t s = 0; s < stages.size(); s++) {
        r.st[s] = stages[s];
        r.cube[s] = s < cubes.size() ? cubes[s] : 0;
    }
    return r;
}

// The signature DrawFixedFunction's uniforms give (IFixedFragmentUniforms' fields).
plWGPUFixedFragmentSig SigOf(const RawState& r)
{
    plWGPUFFUniforms u{};
    for (uint32_t s = 0; s < r.stages; s++) {
        plWGPUFFStage& st = u.stages[s];
        for (int i = 0; i < 4; i++)
            st.ops[i] = r.st[s][i];
        st.ops2[0] = r.st[s][4];
        st.ops2[1] = r.st[s][5];
        st.flags[0] = r.cube[s];
    }
    u.lighting[0] = r.spec;
    u.fog[0] = r.fog == 1 ? r.fogTable : 0;
    u.fog[2] = r.alphaFunc;
    return plWGPUFixedFragment::Signature(u, r.stages, r.fog);
}

std::string SigLine(const plWGPUFixedFragmentSig& sig)
{
    std::string line = "P " + std::to_string(sig.stages) + ' ' + std::to_string(sig.fog) + ' ' +
                       std::to_string(sig.fogMode) + ' ' + std::to_string(sig.spec) + ' ' + std::to_string(sig.func);
    for (uint32_t s = 0; s < sig.stages && s < 8; s++) {
        const plWGPUFixedFragmentSig::Stage& st = sig.st[s];
        line += " |";
        for (unsigned f : {st.op, st.c1, st.c2, st.alpha, st.aop, st.a1, st.a2, st.sampled, st.cube})
            line += ' ' + std::to_string(f);
    }
    return line + " | " + (plWGPUFixedFragment::InDomain(sig) ? '1' : '0');
}

struct SigSet
{
    std::unordered_set<plWGPUFixedFragmentSig, plWGPUFixedFragmentSigHash> seen;
    bool                                                                   Add(const plWGPUFixedFragmentSig& sig) { return seen.insert(sig).second; }
};

// ops: one feature per state: each op as the colour op of stage 0 and of stage 1 (after a MODULATE), and as the alpha
// op of stage 0 and stage 1, three argument choices each; each fog mode, SPECULARENABLE, each alpha function, and a 2D
// and a cube stage three times (the bench's projected coordinates, which no program reads from its signature).
std::vector<RawState> OpsStates()
{
    const uint32_t        ops[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20, 21, 24, 17, 22, 23, 25, 26};
    const uint32_t        args[3][4] = {{0x02, 0x00, 0x02, 0x00}, {0x12, 0x21, 0x22, 0x11}, {0x03, 0x34, 0x24, 0x13}};
    const RawStage        mod = {4, 0x02, 0x00, 2, 0x02, 0x00};
    std::vector<RawState> out;
    for (uint32_t op : ops) {
        for (const auto& a : args) {
            out.push_back(State({{op, a[0], a[1], 1, 0, 0}}, 0));
            out.push_back(State({mod, {op, a[0], a[1] == 0 ? 0x01u : a[1], 1, 0, 0}}, 0));
            out.push_back(State({{2, 0x02, 0, op, a[2], a[3]}}, 0));
            out.push_back(State({mod, {2, 0x01, 0, op, a[2], a[3] == 0 ? 0x01u : a[3]}}, 0));
        }
    }
    const RawStage sel = {2, 0x02, 0, 2, 0x02, 0};
    for (uint32_t m : {1, 2, 3})
        out.push_back(State({sel}, 1, m));
    for (int m = 0; m < 3; m++)
        out.push_back(State({sel}, 2));
    out.push_back(State({sel}, 0, 0, 1));
    for (uint32_t f = 1; f <= 7; f++)
        out.push_back(State({sel}, 0, 0, 0, f));
    for (int c = 0; c < 3; c++)
        for (uint32_t cube : {0, 1})
            out.push_back(State({sel}, 0, 0, 0, 0, {cube}));
    return out;
}

// pairs: every op (the listed ones and one unlisted, 17) at the colour site (ALPHAOP DISABLE) and at the alpha site
// (COLOROP SELECTARG1 TEXTURE), at stage 0 and at stage 1 (after a MODULATE stage), with every pair of the arguments
// the op reads: each a source (DIFFUSE, CURRENT, TEXTURE, TFACTOR, SPECULAR) with each modifier (none, COMPLEMENT,
// ALPHAREPLICATE, both); one state per signature.
std::vector<RawState> PairsStates()
{
    const uint32_t        ops[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20, 21, 24, 17};
    std::vector<uint32_t> arg;
    for (uint32_t m : {0x00, 0x10, 0x20, 0x30})
        for (uint32_t src = 0; src < 5; src++)
            arg.push_back(src | m);
    const RawStage        mod = {4, 0x02, 0x00, 2, 0x02, 0x00};
    std::vector<RawState> out;
    SigSet                seen;
    auto                  add = [&](uint32_t pos, const RawStage& st) {
        const RawState r = pos ? State({mod, st}, 0) : State({st}, 0);
        if (seen.Add(SigOf(r)))
            out.push_back(r);
    };
    for (uint32_t pos : {0, 1}) {
        for (uint32_t op : ops) {
            const bool            r1 = op != 3, r2 = op != 2 && op != 17;
            std::vector<uint32_t> a1s = r1 ? arg : std::vector<uint32_t>{0x02};
            std::vector<uint32_t> a2s = r2 ? arg : std::vector<uint32_t>{0x00};
            for (uint32_t a1 : a1s)
                for (uint32_t a2 : a2s)
                    add(pos, {op, a1, a2, 1, 0, 0});
            for (uint32_t a1 : a1s)
                for (uint32_t a2 : a2s)
                    add(pos, {2, 0x02, 0, op, a1, a2});
        }
    }
    return out;
}

// reachable: the stage compositions plDXPipeline's stage grammar produces, from the layers' blend flags (hsGMatState)
// as the pipeline turns them into texture stage state: stage 0 by IHandleTextureMode (plDXPipeline.cpp:6302-6399), the
// later stages by IHandleStageBlend (:5891-6124, whose two-stages-at-once Mult case rewrites stage 0 at :6070-6077), the
// tail stage by IStageStop (:6948-7011). One state per signature.
enum Blend
{
    kAlpha,
    kAdd,
    kMult,
    kDot3,
    kAddSigned,
    kAddSigned2X,
    kNone,
    kNumBlends
};
struct Layer
{
    bool     textured = false, invColor = false, invAlpha = false, noVtxAlpha = false, noTexAlpha = false;
    bool     invVtxAlpha = false, noTexColor = false, alphaAdd = false, alphaMult = false, mult2 = false;
    uint32_t stickyC = 0, stickyA = 0;
    int      blend = kAlpha;
};

RawStage FirstStage(const Layer& L0, size_t n)
{
    if (!L0.textured) {
        if (n > 1)
            return {3, 0x00, L0.stickyC, 3, 0x00, L0.stickyA};
        return {2, 0x00, 0x00, 2, L0.invVtxAlpha ? 0x10u : 0x00u, 0x00};
    }
    const uint32_t tex = 0x02 | (L0.invColor ? 0x10 : 0);
    const uint32_t aop = L0.noVtxAlpha ? 2 : L0.noTexAlpha ? 3
                                                           : 4;
    const uint32_t a1 = 0x02 | (L0.invAlpha ? 0x10 : 0), a2 = L0.invVtxAlpha ? 0x10 : 0;
    if (n <= 1)
        return {L0.noTexColor ? 3u : 4u, tex, 0x00, aop, a1, a2};
    if (L0.mult2)
        return {4, tex, 0x00, aop, a1, a2};
    return {2, tex, L0.stickyC, aop, a1, a2};
}

RawStage StageBlend(const Layer& L, const Layer& L0, size_t n)
{
    const uint32_t src = 0x02 | (L.invColor ? 0x10 : 0);
    const bool     noTexColor0 = L0.noTexColor || !L0.textured, noTexAlpha0 = L0.noTexAlpha || !L0.textured;
    const uint32_t ta = 0x02 | (L.invAlpha ? 0x10 : 0);
    RawStage       st = {2, src, 0x01, 3, 0x02, 0x01};
    switch (L.blend) {
        case kAlpha:
            if (n == 2 && (L.noTexColor || noTexColor0)) {
                if (L.noTexColor && noTexColor0) {
                    st[0] = 3;
                    st[2] = 0x00;
                } else if (noTexColor0) {
                    st[0] = 4;
                    st[1] = src;
                    st[2] = 0x00;
                } else {
                    st[0] = 4;
                    st[1] = 0x00;
                    st[2] = 0x01;
                }
            } else if (L.noTexColor) {
                st[0] = 3;
                st[2] = 0x01;
            } else {
                st[0] = L.invAlpha ? 20 : 13;
                st[1] = src;
                st[2] = 0x01;
            }
            if (L.alphaAdd && !L.alphaMult) {
                st[3] = 7;
                st[4] = ta;
                st[5] = 0x01;
            } else if (L.alphaMult && !L.alphaAdd) {
                st[3] = 4;
                st[4] = ta;
                st[5] = 0x01;
            } else if (n == 2 && (L.noTexAlpha || noTexAlpha0)) {
                if (L.noTexAlpha && noTexAlpha0) {
                    st[3] = 3;
                    st[5] = 0x00;
                } else if (noTexAlpha0) {
                    st[3] = 4;
                    st[4] = ta;
                    st[5] = 0x00;
                } else {
                    st[3] = 4;
                    st[4] = 0x00;
                    st[5] = 0x01;
                }
            } else {
                st[3] = 3;
                st[5] = 0x01;
            }
            return st;
        case kAdd:
            st[0] = 7;
            return st;
        case kMult:
            st[0] = 4;
            return st;
        case kDot3:
            st[0] = 24;
            return st;
        case kAddSigned:
            st[0] = 8;
            return st;
        case kAddSigned2X:
            st[0] = 9;
            return st;
        default:
            st[0] = 2;
            return st;
    }
}

// IStageStop's tail stage after n layers, or none.
std::vector<RawStage> StageStop(size_t n, const Layer& L0, const Layer& L1)
{
    if (n <= 1)
        return {};
    if (n == 2 && (L0.noTexColor || !L0.textured || L1.noTexColor || L0.mult2))
        return {};
    return {{4, 0x00, 0x01, 1, 0x02, 0x01}};
}

std::vector<Layer> BaseLayers()
{
    std::vector<Layer> out;
    for (int bits = 0; bits < 64; bits++) {
        Layer L;
        L.textured = true;
        L.invColor = bits & 32;
        L.invAlpha = bits & 16;
        L.noVtxAlpha = bits & 8;
        L.noTexAlpha = bits & 4;
        L.invVtxAlpha = bits & 2;
        L.noTexColor = bits & 1;
        if (!(L.noVtxAlpha && L.noTexAlpha))
            out.push_back(L);
    }
    for (uint32_t c : {0x00, 0x01, 0x02, 0x10}) {
        for (uint32_t a : {0x00, 0x10, 0x01, 0x02}) {
            Layer L;
            L.stickyC = c;
            L.stickyA = a;
            out.push_back(L);
        }
    }
    return out;
}

std::vector<Layer> UpperLayers()
{
    std::vector<Layer> out;
    for (int blend = 0; blend < kNumBlends; blend++) {
        for (int bits = 0; bits < 64; bits++) {
            Layer L;
            L.blend = blend;
            L.invColor = bits & 32;
            L.invAlpha = bits & 16;
            L.noTexColor = bits & 8;
            L.noTexAlpha = bits & 4;
            L.alphaAdd = bits & 2;
            L.alphaMult = bits & 1;
            out.push_back(L);
        }
    }
    return out;
}

bool NoFlags(const Layer& L)
{
    return !(L.invColor || L.invAlpha || L.noVtxAlpha || L.noTexAlpha || L.invVtxAlpha || L.noTexColor ||
             L.alphaAdd || L.alphaMult || L.mult2);
}

std::vector<RawState> ReachableStates()
{
    std::vector<RawState> out;
    SigSet                seen;
    auto                  add = [&](const std::vector<RawStage>& stages, uint32_t fog = 0, uint32_t fogTable = 0, uint32_t spec = 0,
                   uint32_t func = 0, std::vector<uint32_t> cubes = {}) {
        const RawState r = State(stages, fog, fogTable, spec, func, std::move(cubes));
        if (seen.Add(SigOf(r)))
            out.push_back(r);
    };
    auto join = [](std::vector<RawStage> a, const std::vector<RawStage>& b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    };
    const std::vector<Layer> base = BaseLayers(), upper = UpperLayers();
    // every stage-1 form after every stage-0 form, two layers (and the two-stages-at-once Mult rewrite)
    for (const Layer& L0 : base) {
        for (const Layer& L1 : upper) {
            add(join({FirstStage(L0, 2), StageBlend(L1, L0, 2)}, StageStop(2, L0, L1)));
            if (L1.blend == kMult && L0.textured) {
                Layer M = L0;
                M.mult2 = true;
                add(join({FirstStage(M, 2), StageBlend(L1, M, 2)}, StageStop(2, M, L1)));
            }
        }
    }
    // the admitted risky stages (BLENDTEXTUREALPHA and MODULATEINVALPHA_ADDCOLOR with each alpha case, DOTPRODUCT3) as
    // the last layer at stage p, after every reachable form of stage p - 1, the stages before fixed
    std::vector<Layer> risky;
    for (const Layer& L : upper)
        if (!L.invColor && ((L.blend == kAlpha && !L.noTexColor) || L.blend == kDot3))
            risky.push_back(L);
    Layer defaultBase;
    defaultBase.textured = true;
    const Layer defaultUpper;
    struct RiskyAt
    {
        size_t                p;
        std::vector<RawStage> stages;
    };
    std::vector<RiskyAt> riskyAt;
    for (size_t p = 1; p <= 3; p++) {
        const size_t              n = p + 1;
        const std::vector<Layer>& before = p == 1 ? base : upper;
        for (const Layer& B : before) {
            for (const Layer& R : risky) {
                const Layer&          L0 = p == 1 ? B : defaultBase;
                std::vector<RawStage> stages = {FirstStage(L0, n)};
                for (size_t s = 1; s < p; s++)
                    stages.push_back(StageBlend(s == p - 1 ? B : defaultUpper, L0, n));
                stages.push_back(StageBlend(R, L0, n));
                stages = join(stages, StageStop(n, L0, R));
                add(stages);
                if ((p == 1 && B.textured && NoFlags(B)) || (p > 1 && B.blend == kAlpha && NoFlags(B)))
                    riskyAt.push_back({p, stages});
            }
        }
    }
    // the endings (each fog mode, SPECULARENABLE, GREATER and GREATEREQUAL) at stage 1, and the cube maps
    for (const RiskyAt& r : riskyAt) {
        if (r.p == 1) {
            const uint32_t endings[5][2] = {{0, 0}, {1, 1}, {1, 2}, {1, 3}, {2, 0}};
            for (const auto& e : endings)
                for (uint32_t spec : {0, 1})
                    for (uint32_t func : {0, 5, 7})
                        add(r.stages, e[0], e[1], spec, func);
        }
        for (size_t c = 0; c < r.stages.size(); c++) {
            std::vector<uint32_t> cubes(r.stages.size(), 0);
            cubes[c] = 1;
            add(r.stages, 0, 0, 0, 0, cubes);
        }
        add(r.stages, 0, 0, 0, 0, std::vector<uint32_t>(r.stages.size(), 1));
    }
    return out;
}

// features: each table fog mode (and two values that are none), vertex fog, SPECULARENABLE, each alpha function value
// 0 to 9, a cube stage, and one to eight stages.
std::vector<RawState> FeatureStates()
{
    const RawStage        sel = {4, 0x02, 0x00, 4, 0x02, 0x00};
    std::vector<RawState> out;
    for (uint32_t m = 0; m <= 4; m++)
        out.push_back(State({sel}, 1, m));
    out.push_back(State({sel}, 2));
    out.push_back(State({sel}, 0, 0, 1));
    for (uint32_t f = 0; f <= 9; f++)
        out.push_back(State({sel}, 0, 0, 0, f));
    out.push_back(State({sel}, 0, 0, 0, 0, {1}));
    for (uint32_t n = 1; n <= 8; n++) {
        std::vector<RawStage> stages;
        std::vector<uint32_t> cubes;
        for (uint32_t s = 0; s < n; s++) {
            stages.push_back({s ? 13u : 4u, 0x02, s ? 0x01u : 0x00u, s ? 15u : 4u, 0x02, 0x01});
            cubes.push_back(s % 3 == 2 ? 1 : 0);
        }
        out.push_back(State(stages, n % 3, 0, n % 2, 5, cubes));
    }
    return out;
}

// The traced states, from the fixture's T lines.
std::vector<RawState> TracedStates(const std::vector<std::string>& lines)
{
    std::vector<RawState> out;
    for (const std::string& line : lines) {
        const std::vector<std::string> parts = Split(line, " | ");
        RawState                       r;
        if (std::sscanf(parts[0].c_str(), "%u %u %u %u %u", &r.stages, &r.fog, &r.spec, &r.fogTable, &r.alphaFunc) != 5 ||
            r.stages > 8 || parts.size() != r.stages + 1u)
            return {};
        for (uint32_t s = 0; s < r.stages; s++) {
            RawStage& st = r.st[s];
            if (std::sscanf(parts[s + 1].c_str(), "%u %u %u %u %u %u %u", &st[0], &st[1], &st[2], &st[3], &st[4], &st[5],
                            &r.cube[s]) != 7)
                return {};
        }
        out.push_back(r);
    }
    return out;
}
} // namespace

// The table (ShaderSrc/FixedFragmentTable.inc) was cut from the source the client embeds.
TEST(plWGPUFixedFragment, BindsTheEmbeddedSource)
{
    plWGPUFixedFragment gen;
    ASSERT_FALSE(Source().empty()) << "no source at " << PLASMA_FIXED_PIPELINE_WGSL;
    EXPECT_TRUE(gen.Bind(Source().c_str())) << gen.Error();
    EXPECT_TRUE(gen.Bound());
    EXPECT_TRUE(gen.Error().empty());
}

// The fixture's programs were proven for the source the client embeds: its header names that source's hash and size.
TEST(plWGPUFixedFragment, FixtureIsTheEmbeddedSources)
{
    const std::vector<std::string> head = Lines(kFixtureHead);
    ASSERT_EQ(head.size(), 3u);
    const std::string& source = Source();
    char               want[64];
    std::snprintf(want, sizeof(want), "source %016llx %zu", static_cast<unsigned long long>(Fnv1a64(source)),
                  source.size());
    EXPECT_EQ(head[0], want);
    EXPECT_EQ(head[1].compare(0, 9, "chromium "), 0) << head[1];
    EXPECT_EQ(head[2].compare(0, 7, "webkit "), 0) << head[2];
}

// The four generated sets are the bench's, state for state: the counts and the SHA-256 of the states' lines (each
// state's signature and domain verdict) per set equal the bench generator's. Then every program's text is the one the
// image check drew: the SHA-256 of the programs' digests per set, and per program the first four bytes of its digest,
// so that a program whose text differs is named by its signature.
TEST(plWGPUFixedFragment, ProgramsMatchTheImageCheck)
{
    const plWGPUFixedFragment& gen = Generator();
    ASSERT_TRUE(gen.Bound()) << gen.Error();
    const std::vector<std::string> sets = Lines(kFixtureSets);
    ASSERT_EQ(sets.size(), 5u);
    std::string short4;
    for (const char* line : kFixtureShort)
        short4 += line;
    const std::vector<RawState> traced = TracedStates(Lines(kFixtureTraced));
    ASSERT_FALSE(traced.empty()) << "the fixture's traced states do not parse";

    const char* const           names[5] = {"traced", "ops", "pairs", "reachable", "features"};
    const std::vector<RawState> states[5] = {traced, OpsStates(), PairsStates(), ReachableStates(), FeatureStates()};
    SigSet                      programs;
    size_t                      index = 0, inDomain = 0, named = 0;
    for (int k = 0; k < 5; k++) {
        std::string lines, digests;
        size_t      added = 0;
        for (const RawState& r : states[k]) {
            const plWGPUFixedFragmentSig sig = SigOf(r);
            const std::string            line = SigLine(sig);
            lines += line + '\n';
            if (!programs.Add(sig))
                continue;
            const Digest d = Sha256(line + '\n' + gen.Module(sig));
            digests.append(reinterpret_cast<const char*>(d.data()), d.size());
            const std::string mine = Hex(d.data(), 4);
            if (short4.compare(8 * index, 8, mine) != 0 && named++ < 20)
                ADD_FAILURE() << names[k] << " program " << index << " differs from the image check's: " << line;
            index++;
            added++;
            inDomain += plWGPUFixedFragment::InDomain(sig);
        }
        const Digest      ls = Sha256(lines), ps = Sha256(digests);
        const std::string mine = std::string(names[k]) + ' ' + std::to_string(states[k].size()) + ' ' +
                                 std::to_string(added) + ' ' + Hex(ls.data(), ls.size()) + ' ' + Hex(ps.data(), ps.size());
        EXPECT_EQ(sets[k], mine) << "set " << names[k] << ": the fixture's line, then this generator's";
    }
    EXPECT_EQ(short4.size(), 8 * index);
    // the programs fall on both sides of the domain
    EXPECT_GT(inDomain, 0u);
    EXPECT_LT(inDomain, index);
}

// Signature reads the uniforms DrawFixedFunction fills: the stage ops, the cube bit, SPECULARENABLE, the table fog mode
// (with table fog only) and the alpha function (NEVER .. GREATEREQUAL only); stages past kStages stay zero.
TEST(plWGPUFixedFragment, SignatureReadsTheUniforms)
{
    plWGPUFFUniforms u{};
    for (plWGPUFFStage& st : u.stages) {
        st.ops[0] = 4;      // MODULATE
        st.ops[1] = 2;      // TEXTURE
        st.ops[2] = 1;      // CURRENT
        st.ops[3] = 1;      // ALPHAOP DISABLE
        st.ops2[3] = 0x103; // COUNT3 | PROJECTED: per draw, not in the signature
        st.flags[0] = 1;    // a cube map
    }
    u.lighting[0] = 1;
    u.fog[0] = 3; // LINEAR
    u.fog[2] = 8; // ALWAYS: no test

    plWGPUFixedFragmentSig sig = plWGPUFixedFragment::Signature(u, 2, 1);
    EXPECT_EQ(sig.stages, 2);
    EXPECT_EQ(sig.fog, 1);
    EXPECT_EQ(sig.fogMode, 3);
    EXPECT_EQ(sig.spec, 1);
    EXPECT_EQ(sig.func, 0);
    EXPECT_EQ(sig.st[0].op, 4);
    EXPECT_EQ(sig.st[0].sampled, 1);
    EXPECT_EQ(sig.st[0].cube, 1);
    EXPECT_EQ(sig.st[1].alpha, plWGPUFixedFragmentSig::kAlphaCurrent);
    for (int s = 2; s < 8; s++)
        EXPECT_EQ(sig.st[s].op, 0) << s;

    // vertex fog: no table fog mode
    EXPECT_EQ(plWGPUFixedFragment::Signature(u, 2, 2).fogMode, 0);
    // NEVER is a test; a stage that does not read its texture has no cube bit
    u.fog[2] = 1;
    u.stages[0].ops[0] = 2; // SELECTARG1
    u.stages[0].ops[1] = 0; // DIFFUSE
    sig = plWGPUFixedFragment::Signature(u, 2, 0);
    EXPECT_EQ(sig.func, 1);
    EXPECT_EQ(sig.fogMode, 0);
    EXPECT_EQ(sig.st[0].sampled, 0);
    EXPECT_EQ(sig.st[0].cube, 0);
    EXPECT_EQ(sig.st[0].c2, plWGPUFixedFragmentSig::kUnread);
}

// A module is the source without the four functions a program replaces, followed by the program's fixedFragment: the
// vertex stage and the helpers it keeps (sampleCoord, fogFactor, stageArg) are the source's. (alphaTest returns for a
// program with no alpha function, sampleStage for one with a stage that does not read its texture:
// ProgramsWithoutAnAlphaFunctionKeepTheUbersAlphaTest, ProgramsReadTheUbersInterstageVariables.)
TEST(plWGPUFixedFragment, ModuleKeepsTheSourcesVertexStage)
{
    const plWGPUFixedFragment& gen = Generator();
    ASSERT_TRUE(gen.Bound()) << gen.Error();
    plWGPUFixedFragmentSig sig{};
    sig.stages = 1;
    sig.st[0] = plWGPUFixedFragment::CanonicalStage(4, 2, 0, 2, 2, 0, false);
    const std::string text = gen.Module(sig);
    for (const char* kept : {"fn fixedVertex(", "fn sampleCoord(", "fn fogFactor(", "fn stageCoord(", "fn stageArg("})
        EXPECT_NE(text.find(kept), std::string::npos) << kept;
    for (const char* cut : {"fn sampleStage(", "fn stageOp("})
        EXPECT_EQ(text.find(cut), std::string::npos) << cut;
    EXPECT_EQ(text.find("fn fixedFragment("), text.rfind("fn fixedFragment("));
    EXPECT_NE(text.find("textureSampleBias(tex0, samp0, c.xy, bias)"), std::string::npos);
}

// A program with no alpha function keeps the uber's alpha test, the source's alphaTest and fixedFragment's statement
// verbatim, as its one discard and the last statement before the return (its draws never take it, but on Dawn/Metal a
// program without a discard samples textures differently); a program with one has its comparison and no alphaTest.
TEST(plWGPUFixedFragment, ProgramsWithoutAnAlphaFunctionKeepTheUbersAlphaTest)
{
    const plWGPUFixedFragment& gen = Generator();
    ASSERT_TRUE(gen.Bound()) << gen.Error();
    const std::string& src = Source();
    const auto         fnOf = [&src](const char* head) {
        const size_t b = src.find(head);
        return b == std::string::npos ? std::string() : src.substr(b, src.find("\n}\n", b) + 3 - b);
    };
    const std::string alphaTest = fnOf("fn alphaTest(");
    ASSERT_FALSE(alphaTest.empty());
    const std::string stmt =
        "    if (u.fog.z != 0u && !alphaTest(u.fog.z, current.a, u.fog.w)) {\n        discard;\n    }\n";
    ASSERT_NE(fnOf("@fragment\nfn fixedFragment(").find(stmt + "    return vec4f(rgb, current.a);\n}\n"),
              std::string::npos);
    const auto count = [](const std::string& text, const std::string& what) {
        size_t n = 0;
        for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
            n++;
        return n;
    };
    for (int fog = 0; fog <= 2; fog++) {
        for (int func = 0; func <= 7; func++) {
            plWGPUFixedFragmentSig sig{};
            sig.stages = 1;
            sig.fog = uint8_t(fog);
            sig.fogMode = uint8_t(fog == 1 ? 3 : 0);
            sig.func = uint8_t(func);
            sig.st[0] = plWGPUFixedFragment::CanonicalStage(4, 2, 0, 2, 2, 0, false);
            const std::string text = gen.Module(sig);
            const std::string what = "fog " + std::to_string(fog) + " func " + std::to_string(func);
            EXPECT_EQ(count(text, "discard;"), 1u) << what;
            if (func) {
                EXPECT_EQ(count(text, "fn alphaTest("), 0u) << what;
                EXPECT_EQ(count(text, stmt), 0u) << what;
                continue;
            }
            EXPECT_EQ(count(text, alphaTest), 1u) << what;
            EXPECT_EQ(count(text, stmt + "    return vec4f(rgb, current.a);\n}\n"), 1u) << what;
            // the helpers' order: the ops, fog_factor, alphaTest, then the entry
            const size_t at = text.find(alphaTest);
            EXPECT_LT(text.rfind("fn op_", at), at) << what;
            EXPECT_GT(text.find("@fragment\nfn fixedFragment("), at) << what;
            if (fog == 1)
                EXPECT_LT(text.find("fn fog_factor("), at) << what;
        }
    }
}

// Every program reads the interstage variables the uber reads, through the uber's statements (a program that leaves
// in.specular or a stage's in.t<s> unread draws differently from the uber on Dawn/Metal, measured in client frames):
// over every program of the five sets, the entry's in. references are exactly diffuse, specular, t<s> for each stage
// below kStages, and fog.x with table fog or fog.y with vertex fog; each stage takes its two colour arguments, and its
// two alpha arguments with an alpha op, as the loop's stageArg calls on its uniforms, and samples its texture once
// (through the loop's sampleStage call when its arguments do not read it, sampleStage then the source's verbatim);
// fixedFragment's SPECULARENABLE statement appears once, right after `var rgb`.
TEST(plWGPUFixedFragment, ProgramsReadTheUbersInterstageVariables)
{
    const plWGPUFixedFragment& gen = Generator();
    ASSERT_TRUE(gen.Bound()) << gen.Error();
    const std::string& src = Source();
    const auto         fnOf = [&src](const char* head) {
        const size_t b = src.find(head);
        return b == std::string::npos ? std::string() : src.substr(b, src.find("\n}\n", b) + 3 - b);
    };
    const std::string sampleStage = fnOf("fn sampleStage(");
    ASSERT_FALSE(sampleStage.empty());
    const std::string spec =
        "    var rgb = current.rgb;\n    if (u.lighting.x != 0u) {\n        rgb = clamp(rgb + specular.rgb, vec3f(0.0), "
        "vec3f(1.0));\n    }\n";
    ASSERT_NE(fnOf("@fragment\nfn fixedFragment(").find(spec), std::string::npos);
    const auto count = [](const std::string& text, const std::string& what) {
        size_t n = 0;
        for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
            n++;
        return n;
    };
    const std::vector<RawState> traced = TracedStates(Lines(kFixtureTraced));
    ASSERT_FALSE(traced.empty());
    const std::vector<RawState> states[5] = {traced, OpsStates(), PairsStates(), ReachableStates(), FeatureStates()};
    SigSet                      programs;
    size_t                      checked = 0, unsampled = 0, failures = 0;
    for (const std::vector<RawState>& set : states) {
        for (const RawState& r : set) {
            const plWGPUFixedFragmentSig sig = SigOf(r);
            if (!programs.Add(sig))
                continue;
            const std::string     text = gen.Module(sig);
            const std::string     entry = text.substr(text.find("@fragment\nfn fixedFragment("));
            const std::string     what = SigLine(sig);
            // the in. references, each with its swizzle for fog
            std::set<std::string> reads, want = {"diffuse", "specular"};
            for (size_t at = entry.find("in."); at != std::string::npos; at = entry.find("in.", at + 1)) {
                if (at && (std::isalnum(uint8_t(entry[at - 1])) || entry[at - 1] == '_'))
                    continue;
                size_t e = at + 3;
                while (e < entry.size() && (std::isalnum(uint8_t(entry[e])) || entry[e] == '_'))
                    e++;
                std::string name = entry.substr(at + 3, e - at - 3);
                if (name == "fog" && e + 1 < entry.size() && entry[e] == '.')
                    name += entry.substr(e, 2);
                reads.insert(name);
            }
            for (uint32_t s = 0; s < sig.stages; s++)
                want.insert("t" + std::to_string(s));
            if (sig.fog == 1)
                want.insert("fog.x");
            if (sig.fog == 2)
                want.insert("fog.y");
            bool ok = reads == want;
            bool anyUnsampled = false;
            for (uint32_t s = 0; s < sig.stages; s++) {
                const std::string n = std::to_string(s), us = "u.stages[" + n + "u].";
                const size_t      args = sig.st[s].alpha == plWGPUFixedFragmentSig::kAlphaOp ? 4 : 2;
                ok = ok && count(entry, "stageArg(" + us) == args &&
                     count(entry, "stageArg(" + us + "ops.y, diffuse, current, tex, specular)") == 1 &&
                     count(entry, "stageArg(" + us + "ops.z, diffuse, current, tex, specular)") == 1;
                const size_t samples = count(entry, "textureSampleBias(tex" + n + ", ") +
                                       count(entry, "textureSampleBias(cube" + n + ", ") +
                                       count(entry, "sampleStage(" + n + "u, in.t" + n + ")");
                ok = ok && samples == 1;
                if (!sig.st[s].sampled) {
                    anyUnsampled = true;
                    ok = ok && count(entry, "let tex = sampleStage(" + n + "u, in.t" + n + ");") == 1;
                }
            }
            ok = ok && count(entry, "stageArg(") == count(entry, "stageArg(u.stages[") && count(entry, spec) == 1 &&
                 count(entry, "tex = vec4f(0.0)") == 0 && count(text, sampleStage) == (anyUnsampled ? 1u : 0u);
            unsampled += anyUnsampled;
            checked++;
            if (!ok && failures++ < 10)
                ADD_FAILURE() << "program reads other interstage variables than the uber: " << what;
        }
    }
    EXPECT_EQ(failures, 0u);
    EXPECT_GT(checked, 1000u);
    EXPECT_GT(unsampled, 0u);
}

// A source other than the one the table was cut from is refused (the device then keeps every draw on the uber).
TEST(plWGPUFixedFragment, RefusesAnotherSource)
{
    std::string       src(Source());
    const std::string head = "if (op == TOP_DOTPRODUCT3) {";
    const size_t      at = src.find(head);
    ASSERT_NE(at, std::string::npos);
    src.replace(at, head.size(), "if (op == TOP_DOT3) {");
    plWGPUFixedFragment gen;
    EXPECT_FALSE(gen.Bind(src.c_str()));
    EXPECT_FALSE(gen.Bound());
    EXPECT_NE(gen.Error().find("source changed"), std::string::npos) << gen.Error();
    // the same size, another byte
    src.assign(Source());
    src[src.size() / 2] ^= 1;
    EXPECT_FALSE(gen.Bind(src.c_str()));
}

// A stage whose colour op and alpha op are both of the six risky ops stays on the uber, whatever its arguments.
TEST(plWGPUFixedFragment, BothOpsRiskyIsOutsideTheDomain)
{
    plWGPUFixedFragmentSig sig{};
    sig.stages = 2;
    sig.st[0] = plWGPUFixedFragment::CanonicalStage(2, 2, 0, 2, 2, 0, false);  // SELECTARG1 TEXTURE
    sig.st[1] = plWGPUFixedFragment::CanonicalStage(13, 2, 1, 4, 2, 1, false); // BLENDTEXTUREALPHA; MODULATE
    EXPECT_TRUE(plWGPUFixedFragment::InDomain(sig));
    sig.st[1] = plWGPUFixedFragment::CanonicalStage(13, 2, 1, 14, 0, 4, false); // ...; BLENDFACTORALPHA
    EXPECT_FALSE(plWGPUFixedFragment::InDomain(sig));
}

TEST(plWGPUFixedFragment, RiskyOpReadingCurrentAfterDot3IsOutsideTheDomain)
{
    plWGPUFixedFragmentSig sig{};
    sig.stages = 3;
    sig.st[0] = plWGPUFixedFragment::CanonicalStage(2, 2, 0, 2, 2, 0, false);  // SELECTARG1 TEXTURE
    sig.st[1] = plWGPUFixedFragment::CanonicalStage(13, 2, 1, 4, 2, 1, false); // BLENDTEXTUREALPHA TEXTURE CURRENT
    sig.st[2] = plWGPUFixedFragment::CanonicalStage(13, 2, 1, 4, 2, 1, false); // the same after it
    EXPECT_TRUE(plWGPUFixedFragment::InDomain(sig));
    sig.st[1] = plWGPUFixedFragment::CanonicalStage(24, 2, 1, 4, 2, 1, false); // DOTPRODUCT3 TEXTURE CURRENT
    EXPECT_FALSE(plWGPUFixedFragment::InDomain(sig));
    sig.st[2] = plWGPUFixedFragment::CanonicalStage(4, 2, 1, 4, 2, 1, false); // MODULATE after it: not a risky op
    EXPECT_TRUE(plWGPUFixedFragment::InDomain(sig));
}
