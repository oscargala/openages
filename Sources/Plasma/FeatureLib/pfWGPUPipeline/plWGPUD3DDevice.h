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

#ifndef _plWGPUD3DDevice_h_
#define _plWGPUD3DDevice_h_

/*
 The Direct3D 9 fixed-function device state that pfDXPipeline drives, kept on the CPU so the
 DX material code can be ported nearly verbatim: pfMetalPipeline decides the structure,
 pfDXPipeline the behaviour.

 plDXPipeline sets device state by difference (hsGMatState::Differs against fOldLayerState,
 fTexturing, fLastEndingStage, fLayerUVWSrcs, fLayerXformFlags, the uint32_t(-1) blend-flag
 resets): a state it does not set keeps the value of an earlier draw, or of
 IInitDeviceState. That stale state is part of the behaviour (for example ALPHAFUNC stays
 GREATEREQUAL/ref 1 from plDXPipeline::IInitDeviceState until a blend change), so the state lives
 here, persists across draws exactly as on a D3D9 device, and is snapshotted into a WebGPU
 pipeline key and a uniform block at each draw (plWGPUDevice::DrawFixedFunction).

 Names and values are those of d3d9types.h (DirectX 9 SDK). Defaults are the device-creation
 defaults the SDK documents for each D3DRENDERSTATETYPE, D3DTEXTURESTAGESTATETYPE and
 D3DSAMPLERSTATETYPE entry.

 The d3d9types.h names live in namespace plWGPUD3D9, so that they stay out of the global
 namespace of every file that includes the pipeline's headers (plClient.cpp among them). A source
 file that ports plDXPipeline code opens the namespace with a using-directive, as plWaveSet7.cpp
 does for plShaderID; a header qualifies the names.
 */

#include <cstdint>
#include <cstring>
#include <vector>

class hsMatrix44;
class plWGPUTextureRef;
struct plWGPUShaderProgram;

namespace plWGPUD3D9
{
// The BOOL values the ported plDXPipeline code passes to SetRenderState (windef.h).
#ifndef TRUE
constexpr int TRUE = 1;
#endif
#ifndef FALSE
constexpr int FALSE = 0;
#endif

#ifndef HS_BUILD_FOR_WIN32
typedef uint32_t DWORD;
#else
typedef unsigned long DWORD; // minwindef.h's type, declared without windows.h (as pfDXPipeline/plDXTextFont.h does)
#endif
typedef DWORD D3DCOLOR;

struct D3DMATRIX
{
    float m[4][4];
};

struct D3DCOLORVALUE
{
    float r, g, b, a;
};

struct D3DVECTOR
{
    float x, y, z;
};

struct D3DMATERIAL9
{
    D3DCOLORVALUE Diffuse;
    D3DCOLORVALUE Ambient;
    D3DCOLORVALUE Specular;
    D3DCOLORVALUE Emissive;
    float         Power;
};

enum D3DLIGHTTYPE
{
    D3DLIGHT_POINT = 1,
    D3DLIGHT_SPOT = 2,
    D3DLIGHT_DIRECTIONAL = 3,
};

struct D3DLIGHT9
{
    D3DLIGHTTYPE  Type;
    D3DCOLORVALUE Diffuse;
    D3DCOLORVALUE Specular;
    D3DCOLORVALUE Ambient;
    D3DVECTOR     Position;
    D3DVECTOR     Direction;
    float         Range;
    float         Falloff;
    float         Attenuation0;
    float         Attenuation1;
    float         Attenuation2;
    float         Theta;
    float         Phi;
};

enum D3DRENDERSTATETYPE
{
    D3DRS_ZENABLE = 7,
    D3DRS_FILLMODE = 8,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25,
    D3DRS_DITHERENABLE = 26,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_FOGENABLE = 28,
    D3DRS_SPECULARENABLE = 29,
    D3DRS_FOGCOLOR = 34,
    D3DRS_FOGTABLEMODE = 35,
    D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37,
    D3DRS_FOGDENSITY = 38,
    D3DRS_RANGEFOGENABLE = 48,
    D3DRS_STENCILENABLE = 52,
    D3DRS_TEXTUREFACTOR = 60,
    D3DRS_CLIPPING = 136,
    D3DRS_LIGHTING = 137,
    D3DRS_AMBIENT = 139,
    D3DRS_FOGVERTEXMODE = 140,
    D3DRS_COLORVERTEX = 141,
    D3DRS_LOCALVIEWER = 142,
    D3DRS_NORMALIZENORMALS = 143,
    D3DRS_DIFFUSEMATERIALSOURCE = 145,
    D3DRS_SPECULARMATERIALSOURCE = 146,
    D3DRS_AMBIENTMATERIALSOURCE = 147,
    D3DRS_EMISSIVEMATERIALSOURCE = 148,
    D3DRS_VERTEXBLEND = 151,
    D3DRS_MULTISAMPLEANTIALIAS = 161,
    D3DRS_COLORWRITEENABLE = 168,
    D3DRS_BLENDOP = 171,
    D3DRS_ANTIALIASEDLINEENABLE = 176,
    D3DRS_DEPTHBIAS = 195,

    kD3DRSCount = 256
};

enum D3DTEXTURESTAGESTATETYPE
{
    D3DTSS_COLOROP = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_BUMPENVMAT00 = 7,
    D3DTSS_BUMPENVMAT01 = 8,
    D3DTSS_BUMPENVMAT10 = 9,
    D3DTSS_BUMPENVMAT11 = 10,
    D3DTSS_TEXCOORDINDEX = 11,
    D3DTSS_BUMPENVLSCALE = 22,
    D3DTSS_BUMPENVLOFFSET = 23,
    D3DTSS_TEXTURETRANSFORMFLAGS = 24,
    D3DTSS_COLORARG0 = 26,
    D3DTSS_ALPHAARG0 = 27,
    D3DTSS_RESULTARG = 28,

    kD3DTSSCount = 33
};

enum D3DSAMPLERSTATETYPE
{
    D3DSAMP_ADDRESSU = 1,
    D3DSAMP_ADDRESSV = 2,
    D3DSAMP_ADDRESSW = 3,
    D3DSAMP_MAGFILTER = 5,
    D3DSAMP_MINFILTER = 6,
    D3DSAMP_MIPFILTER = 7,
    D3DSAMP_MIPMAPLODBIAS = 8,
    D3DSAMP_MAXANISOTROPY = 10,

    kD3DSAMPCount = 14
};

enum D3DTRANSFORMSTATETYPE
{
    D3DTS_VIEW = 2,
    D3DTS_PROJECTION = 3,
    D3DTS_TEXTURE0 = 16,
    D3DTS_TEXTURE1 = 17,
    D3DTS_TEXTURE2 = 18,
    D3DTS_TEXTURE3 = 19,
    D3DTS_TEXTURE4 = 20,
    D3DTS_TEXTURE5 = 21,
    D3DTS_TEXTURE6 = 22,
    D3DTS_TEXTURE7 = 23,
    D3DTS_WORLD = 256,
};
// A macro in d3d9types.h; a function here, so that the name stays in the namespace.
constexpr D3DTRANSFORMSTATETYPE D3DTS_WORLDMATRIX(int index) { return (D3DTRANSFORMSTATETYPE)(index + 256); }

enum D3DTEXTUREOP
{
    D3DTOP_DISABLE = 1,
    D3DTOP_SELECTARG1 = 2,
    D3DTOP_SELECTARG2 = 3,
    D3DTOP_MODULATE = 4,
    D3DTOP_MODULATE2X = 5,
    D3DTOP_MODULATE4X = 6,
    D3DTOP_ADD = 7,
    D3DTOP_ADDSIGNED = 8,
    D3DTOP_ADDSIGNED2X = 9,
    D3DTOP_SUBTRACT = 10,
    D3DTOP_ADDSMOOTH = 11,
    D3DTOP_BLENDDIFFUSEALPHA = 12,
    D3DTOP_BLENDTEXTUREALPHA = 13,
    D3DTOP_BLENDFACTORALPHA = 14,
    D3DTOP_BLENDTEXTUREALPHAPM = 15,
    D3DTOP_BLENDCURRENTALPHA = 16,
    D3DTOP_PREMODULATE = 17,
    D3DTOP_MODULATEALPHA_ADDCOLOR = 18,
    D3DTOP_MODULATECOLOR_ADDALPHA = 19,
    D3DTOP_MODULATEINVALPHA_ADDCOLOR = 20,
    D3DTOP_MODULATEINVCOLOR_ADDALPHA = 21,
    D3DTOP_BUMPENVMAP = 22,
    D3DTOP_BUMPENVMAPLUMINANCE = 23,
    D3DTOP_DOTPRODUCT3 = 24,
    D3DTOP_MULTIPLYADD = 25,
    D3DTOP_LERP = 26,
};

// D3DTA_* argument selectors and modifiers
enum : DWORD
{
    D3DTA_SELECTMASK = 0x0000000f,
    D3DTA_DIFFUSE = 0x00000000,
    D3DTA_CURRENT = 0x00000001,
    D3DTA_TEXTURE = 0x00000002,
    D3DTA_TFACTOR = 0x00000003,
    D3DTA_SPECULAR = 0x00000004,
    D3DTA_TEMP = 0x00000005,
    D3DTA_CONSTANT = 0x00000006,
    D3DTA_COMPLEMENT = 0x00000010,
    D3DTA_ALPHAREPLICATE = 0x00000020,
};

enum D3DBLEND
{
    D3DBLEND_ZERO = 1,
    D3DBLEND_ONE = 2,
    D3DBLEND_SRCCOLOR = 3,
    D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA = 5,
    D3DBLEND_INVSRCALPHA = 6,
    D3DBLEND_DESTALPHA = 7,
    D3DBLEND_INVDESTALPHA = 8,
    D3DBLEND_DESTCOLOR = 9,
    D3DBLEND_INVDESTCOLOR = 10,
    D3DBLEND_SRCALPHASAT = 11,
};

enum D3DBLENDOP
{
    D3DBLENDOP_ADD = 1,
    D3DBLENDOP_SUBTRACT = 2,
    D3DBLENDOP_REVSUBTRACT = 3,
    D3DBLENDOP_MIN = 4,
    D3DBLENDOP_MAX = 5,
};

enum D3DCMPFUNC
{
    D3DCMP_NEVER = 1,
    D3DCMP_LESS = 2,
    D3DCMP_EQUAL = 3,
    D3DCMP_LESSEQUAL = 4,
    D3DCMP_GREATER = 5,
    D3DCMP_NOTEQUAL = 6,
    D3DCMP_GREATEREQUAL = 7,
    D3DCMP_ALWAYS = 8,
};

// D3DCOLORWRITEENABLE_* (D3DRS_COLORWRITEENABLE bits)
enum
{
    D3DCOLORWRITEENABLE_RED = 1 << 0,
    D3DCOLORWRITEENABLE_GREEN = 1 << 1,
    D3DCOLORWRITEENABLE_BLUE = 1 << 2,
    D3DCOLORWRITEENABLE_ALPHA = 1 << 3,
};

enum D3DCULL
{
    D3DCULL_NONE = 1,
    D3DCULL_CW = 2,
    D3DCULL_CCW = 3,
};

enum D3DFILLMODE
{
    D3DFILL_POINT = 1,
    D3DFILL_WIREFRAME = 2,
    D3DFILL_SOLID = 3,
};

enum D3DZBUFFERTYPE
{
    D3DZB_FALSE = 0,
    D3DZB_TRUE = 1,
};

enum D3DFOGMODE
{
    D3DFOG_NONE = 0,
    D3DFOG_EXP = 1,
    D3DFOG_EXP2 = 2,
    D3DFOG_LINEAR = 3,
};

enum D3DMATERIALCOLORSOURCE
{
    D3DMCS_MATERIAL = 0,
    D3DMCS_COLOR1 = 1,
    D3DMCS_COLOR2 = 2,
};

enum D3DVERTEXBLENDFLAGS
{
    D3DVBF_DISABLE = 0,
    D3DVBF_1WEIGHTS = 1,
    D3DVBF_2WEIGHTS = 2,
    D3DVBF_3WEIGHTS = 3,
};

enum D3DTEXTURETRANSFORMFLAGS
{
    D3DTTFF_DISABLE = 0,
    D3DTTFF_COUNT1 = 1,
    D3DTTFF_COUNT2 = 2,
    D3DTTFF_COUNT3 = 3,
    D3DTTFF_COUNT4 = 4,
    D3DTTFF_PROJECTED = 256,
};

// D3DTSS_TEXCOORDINDEX generation flags (the high word), equal to plLayerInterface's
// kUVWNormal/kUVWPosition/kUVWReflect (plDXPipeline::IUseTextureRef passes GetUVWSrc() through).
enum : DWORD
{
    D3DTSS_TCI_PASSTHRU = 0x00000000,
    D3DTSS_TCI_CAMERASPACENORMAL = 0x00010000,
    D3DTSS_TCI_CAMERASPACEPOSITION = 0x00020000,
    D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR = 0x00030000,
};

enum D3DTEXTUREADDRESS
{
    D3DTADDRESS_WRAP = 1,
    D3DTADDRESS_MIRROR = 2,
    D3DTADDRESS_CLAMP = 3,
    D3DTADDRESS_BORDER = 4,
};

enum D3DTEXTUREFILTERTYPE
{
    D3DTEXF_NONE = 0,
    D3DTEXF_POINT = 1,
    D3DTEXF_LINEAR = 2,
    D3DTEXF_ANISOTROPIC = 3,
};
} // namespace plWGPUD3D9

/** The persistent D3D9 fixed-function state of one device (see the file comment). */
class plWGPUD3DDevice
{
public:
    static constexpr int kNumStages = 8;
    static constexpr int kNumWorldMatrices = 2; // D3DVBF_1WEIGHTS is the most Plasma uses

    plWGPUD3D9::DWORD        fRenderState[plWGPUD3D9::kD3DRSCount];
    plWGPUD3D9::DWORD        fStageState[kNumStages][plWGPUD3D9::kD3DTSSCount];
    plWGPUD3D9::DWORD        fSamplerState[kNumStages][plWGPUD3D9::kD3DSAMPCount];
    plWGPUD3D9::D3DMATRIX    fWorld[kNumWorldMatrices];
    plWGPUD3D9::D3DMATRIX    fView;
    plWGPUD3D9::D3DMATRIX    fProjection;
    plWGPUD3D9::D3DMATRIX    fTexture[kNumStages];
    plWGPUD3D9::D3DMATERIAL9 fMaterial;
    plWGPUTextureRef*        fTextures[kNumStages];

    std::vector<plWGPUD3D9::D3DLIGHT9> fLights;        // by light index (SetLight)
    std::vector<uint8_t>               fLightEnabled;  // by light index (LightEnable)
    std::vector<plWGPUD3D9::DWORD>     fEnabledLights; // indices in enable order, at most 8 used

    // Bumped by every state change that a snapshot of the transforms depends on.
    uint32_t fTransformSerial;

    // The programmable pipeline (IDirect3DDevice9::SetVertexShader/SetPixelShader,
    // SetVertexShaderConstantF/SetPixelShaderConstantF). A null program is the fixed-function
    // stage. The constant registers persist across draws and programs, as on the device: DX
    // uploads registers 0..NumConsts-1 on every ISetShaders (plDXVertexShader::ISetConstants,
    // plDXPixelShader::ISetConstants) and the registers above keep earlier values. vs_1_1 has at
    // least 96 constant registers, ps_1_1 has 8 ("Registers - vs_1_1", "Registers - ps_1_X").
    static constexpr int       kNumVSConsts = 96;
    static constexpr int       kNumPSConsts = 8;
    const plWGPUShaderProgram* fVertexShader;
    const plWGPUShaderProgram* fPixelShader;
    float                      fVSConsts[kNumVSConsts][4];
    float                      fPSConsts[kNumPSConsts][4];

    // State the WebGPU snapshot does not carry, counted when used.
    uint32_t fUnsupportedFillMode;

    plWGPUD3DDevice() { Reset(); }

    /** The device-creation defaults (D3D9 SDK, per state type). */
    void Reset();

    void SetRenderState(plWGPUD3D9::D3DRENDERSTATETYPE state, plWGPUD3D9::DWORD value) { fRenderState[state] = value; }
    void SetTextureStageState(plWGPUD3D9::DWORD stage, plWGPUD3D9::D3DTEXTURESTAGESTATETYPE type,
                              plWGPUD3D9::DWORD value)
    {
        if (stage < kNumStages)
            fStageState[stage][type] = value;
    }
    void SetSamplerState(plWGPUD3D9::DWORD sampler, plWGPUD3D9::D3DSAMPLERSTATETYPE type, plWGPUD3D9::DWORD value)
    {
        if (sampler < kNumStages)
            fSamplerState[sampler][type] = value;
    }
    void SetTransform(plWGPUD3D9::D3DTRANSFORMSTATETYPE state, const plWGPUD3D9::D3DMATRIX* matrix);
    void SetMaterial(const plWGPUD3D9::D3DMATERIAL9* material) { fMaterial = *material; }
    void SetLight(plWGPUD3D9::DWORD index, const plWGPUD3D9::D3DLIGHT9* light);
    void LightEnable(plWGPUD3D9::DWORD index, bool enable);
    void SetTexture(plWGPUD3D9::DWORD stage, plWGPUTextureRef* texture)
    {
        if (stage < kNumStages)
            fTextures[stage] = texture;
    }
    void SetVertexShader(const plWGPUShaderProgram* program) { fVertexShader = program; }
    void SetPixelShader(const plWGPUShaderProgram* program) { fPixelShader = program; }
    void SetVertexShaderConstantF(uint32_t start, const float* data, uint32_t count)
    {
        for (uint32_t i = 0; i < count && start + i < uint32_t(kNumVSConsts); i++)
            memcpy(fVSConsts[start + i], data + 4 * i, sizeof(fVSConsts[0]));
    }
    // The values as set; the ps_1_1 range (+-MaxPixelShaderValue = 1) applies
    // when the draw uploads them (plWGPUDevice::IShaderUniformsAndTextures; ShaderPrelude.wgsl
    // pconst reads the clamped copy).
    void SetPixelShaderConstantF(uint32_t start, const float* data, uint32_t count)
    {
        for (uint32_t i = 0; i < count && start + i < uint32_t(kNumPSConsts); i++)
            memcpy(fPSConsts[start + i], data + 4 * i, sizeof(fPSConsts[0]));
    }

    float RenderStateFloat(plWGPUD3D9::D3DRENDERSTATETYPE state) const
    {
        float f;
        memcpy(&f, &fRenderState[state], sizeof(f));
        return f;
    }
    float SamplerStateFloat(plWGPUD3D9::DWORD sampler, plWGPUD3D9::D3DSAMPLERSTATETYPE type) const
    {
        float f;
        memcpy(&f, &fSamplerState[sampler][type], sizeof(f));
        return f;
    }
};

namespace plWGPUD3D9
{
/** DWORD bits of a float, as plDXPipeline's `*(DWORD*)(&f)` and F2DW. */
inline DWORD F2DW(float f)
{
    DWORD d;
    memcpy(&d, &f, sizeof(d));
    return d;
}

extern const D3DMATRIX d3dIdentityMatrix;

/** plDXPipeline::IMatrix44ToD3DMatrix: the transpose, or the
    identity for an identity-flagged matrix. */
D3DMATRIX& IMatrix44ToD3DMatrix(D3DMATRIX& dst, const hsMatrix44& src);
} // namespace plWGPUD3D9

#endif // _plWGPUD3DDevice_h_
