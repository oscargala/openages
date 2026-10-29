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

#ifndef plWGPUFixedFragment_h
#define plWGPUFixedFragment_h

#include <cstdint>
#include <cstring>
#include <string>

struct plWGPUFFUniforms;

/**
 * The state a fixed-function draw's fragment stage depends on (FixedPipelineShaders.wgsl fixedFragment), in
 * canonical form: two draws with the same signature run the same fragment code. Per enabled stage the COLOROP, the
 * colour arguments it reads, the alpha (DOTPRODUCT3's colour, CURRENT under ALPHAOP DISABLE, or ALPHAOP with the
 * arguments it reads), whether the stage reads its texture and whether that texture is a cube map; then
 * SPECULARENABLE, kFog with the table fog mode, and the alpha function. D3DTSS_TEXTURETRANSFORMFLAGS is not in it:
 * every program reads it per draw, as the uber does (sampleCoord). Only uint8_t members, compared and hashed by bytes.
 */
struct plWGPUFixedFragmentSig
{
    static constexpr uint8_t kUnread = 0xff;
    enum : uint8_t
    {
        kAlphaCurrent = 0, // ALPHAOP DISABLE: the alpha passes
        kAlphaDot3 = 1,    // COLOROP DOTPRODUCT3: the alpha is the colour's
        kAlphaOp = 2,      // ALPHAOP with its arguments
    };
    struct Stage
    {
        uint8_t op;      // COLOROP; 0 for an op the uber's stageOp has no branch for (it returns Arg1)
        uint8_t c1, c2;  // COLORARG1/2 as the op reads them, kUnread when it does not
        uint8_t alpha;   // kAlphaCurrent, kAlphaDot3, kAlphaOp
        uint8_t aop;     // with kAlphaOp: ALPHAOP (0 unlisted), else kUnread
        uint8_t a1, a2;  // with kAlphaOp: ALPHAARG1/2 as ALPHAOP reads them, else kUnread
        uint8_t sampled; // the stage reads its texture
        uint8_t cube;    // ... a cube map
    };
    uint8_t stages;  // kStages
    uint8_t fog;     // kFog: 0 none, 1 table, 2 vertex
    uint8_t fogMode; // with table fog, D3DRS_FOGTABLEMODE EXP, EXP2 or LINEAR; else 0
    uint8_t spec;    // D3DRS_SPECULARENABLE
    uint8_t func;    // with ALPHATESTENABLE, D3DRS_ALPHAFUNC NEVER .. GREATEREQUAL; else 0 (no test, or ALWAYS: the
                     // program carries the uber's runtime alpha test)
    uint8_t pad[3];
    Stage   st[8];

    bool operator==(const plWGPUFixedFragmentSig& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};
static_assert(sizeof(plWGPUFixedFragmentSig) == 80, "plWGPUFixedFragmentSig has no padding");

struct plWGPUFixedFragmentSigHash
{
    size_t operator()(const plWGPUFixedFragmentSig& s) const noexcept;
};

/**
 * The specialised fragment programs of fixed-function draws. A program's module is FixedPipelineShaders.wgsl without
 * fixedFragment, sampleStage, stageOp and alphaTest, followed by a generated fixedFragment that runs only the branches
 * its signature selects: every expression in it is the source's own (stageOp's branch for each op, sampleStage's
 * sample line for each stage that reads its texture, fogFactor's branch for the table fog mode, alphaTest's
 * comparison, fixedFragment's statements), cut from the source at build time by ShaderSrc/gen/fixed_fragment_table.py
 * into ShaderSrc/FixedFragmentTable.inc. Here the pieces are only concatenated, in a fixed order.
 *
 * A program reads the interstage variables the uber reads, through the uber's statements: every argument is the
 * stage loop's stageArg call on the stage's uniforms, every stage below kStages samples its texture (one whose
 * arguments do not read it through the loop's sampleStage call, added back verbatim), and SPECULARENABLE is
 * fixedFragment's runtime statement. A program that leaves in.specular or a stage's in.t<s> unread draws differently
 * from the uber on Dawn/Metal (measured in client frames; the generator's notes name the legs). So the signature's
 * arguments, its sampled bits and SPECULARENABLE choose the fallback domain (InDomain), not the text. A program with
 * no alpha function keeps the uber's alpha test, alphaTest and its statement verbatim: never taken by its draws, but a
 * program without a discard samples textures differently on Dawn/Metal (the generator's notes cite the runs). The
 * vertex stage is the source's.
 */
class plWGPUFixedFragment
{
public:
    /** The signature of a draw from its filled uniforms (plWGPUDevice::IFixedFragmentUniforms) and key. */
    static plWGPUFixedFragmentSig        Signature(const plWGPUFFUniforms& u, uint32_t stages, uint32_t fog);
    /**
     * Whether the signature's program is in the fallback domain: the programs the client runs specialised (the others
     * keep the uber). A measured fit: over the bench's pairwise programs (Chromium, Dawn/Metal) six ops differ from
     * the uber in some argument forms, ADDSMOOTH, BLENDTEXTUREALPHA, BLENDFACTORALPHA, MODULATEINVALPHA_ADDCOLOR,
     * MODULATEINVCOLOR_ADDALPHA and DOTPRODUCT3. Those are admitted only with no argument modifier, with two arguments
     * whose dependence sets share nothing (an argument's source; CURRENT's is DIFFUSE at stage 0, and DIFFUSE,
     * SPECULAR, TFACTOR and the earlier stages' textures later), BLENDTEXTUREALPHA's alpha with Arg2's set not holding
     * TEXTURE, BLENDFACTORALPHA with Arg2's not holding TFACTOR, never as both ops of one stage, and never reading
     * CURRENT after an earlier DOTPRODUCT3 stage. Every other op is admitted in every form. Measured over every
     * pairwise program against the uber, over the stage compositions plDXPipeline's grammar reaches (which gave the
     * last term), and over the programs this rule admits, on the pinned source: none of those differs.
     */
    static bool                          InDomain(const plWGPUFixedFragmentSig& sig);
    /** One stage's canonical form from its D3D9 stage state. */
    static plWGPUFixedFragmentSig::Stage CanonicalStage(uint32_t colorOp, uint32_t colorArg1, uint32_t colorArg2,
                                                        uint32_t alphaOp, uint32_t alphaArg1, uint32_t alphaArg2,
                                                        bool cube);

    /**
     * Bind the table to the embedded source: false when the source is not the one the table was cut from (its size
     * and FNV-1a 64 hash), and Error() says so; then no program can be made and every draw keeps the uber.
     */
    bool               Bind(const char* source);
    bool               Bound() const { return fBound; }
    const std::string& Error() const { return fError; }

    /** The whole WGSL module of a signature's program (Bind first). */
    std::string Module(const plWGPUFixedFragmentSig& sig) const;

private:
    bool        fBound = false;
    std::string fError;
    std::string fBase; // the source without the five functions
};

#endif /* plWGPUFixedFragment_h */
