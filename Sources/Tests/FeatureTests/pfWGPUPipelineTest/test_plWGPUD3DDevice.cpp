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

#include <cstdint>
#include <cstring>
#include <map>

#include "pfWGPUPipeline/plWGPUD3DDevice.h"

using namespace plWGPUD3D9;

// The state plWGPUD3DDevice starts with: what a freshly created (or Reset) IDirect3DDevice9 holds,
// the state plDXPipeline starts from. The values are the device-state defaults the DirectX 9 SDK
// documents for each D3DRENDERSTATETYPE, D3DTEXTURESTAGESTATETYPE and D3DSAMPLERSTATETYPE entry.
// Each row names, in plDXPipeline.cpp (pfDXPipeline), where the DX pipeline first sets the state:
// IInitDeviceState (:688) overrides a default at device creation (ICreateDeviceObjects :1059) and
// after every device reset (IResetDevice :1932/:1952, Resize :2165/:2170); a state set later keeps its
// default until then; a state plDXPipeline never sets keeps its default for the whole session.

namespace
{
DWORD FloatBits(float f) { return F2DW(f); }

// The render states with a default other than 0. Every other index starts at 0.
const std::map<D3DRENDERSTATETYPE, DWORD> kRenderStateDefaults = {
    // TRUE with an automatic depth-stencil: ICreateDevice :1295 EnableAutoDepthStencil = TRUE.
    // IInitDeviceState :699
    {D3DRS_ZENABLE, D3DZB_TRUE},
    {D3DRS_FILLMODE, D3DFILL_SOLID},               // first set by IHandleMiscMode :5780/:5782
    {D3DRS_ZWRITEENABLE, TRUE},                    // IInitDeviceState :698
    {D3DRS_SRCBLEND, D3DBLEND_ONE},                // first set by IHandleFirstStageBlend :6138
    {D3DRS_DESTBLEND, D3DBLEND_ZERO},              // first set by IHandleFirstStageBlend :6139
    {D3DRS_CULLMODE, D3DCULL_CCW},                 // IInitDeviceState :701 (D3DCULL_CW, :691)
    {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},               // IInitDeviceState :697, the same value
    {D3DRS_ALPHAFUNC, D3DCMP_ALWAYS},              // IInitDeviceState :705 (GREATEREQUAL)
    {D3DRS_FOGEND, FloatBits(1.f)},                // first set by ISetFogParameters :4401
    {D3DRS_FOGDENSITY, FloatBits(1.f)},            // first set by ISetFogParameters :4408
    {D3DRS_TEXTUREFACTOR, 0xffffffff},             // opaque white; IInitDeviceState :715 (0)
    {D3DRS_CLIPPING, TRUE},                        // IInitDeviceState :700, the same value
    {D3DRS_LIGHTING, TRUE},                        // IInitDeviceState :713 (FALSE)
    {D3DRS_COLORVERTEX, TRUE},                     // never set: the default holds
    {D3DRS_LOCALVIEWER, TRUE},                     // IInitDeviceState :724, the same value
    {D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1},  // first set by ICalcLighting :4982
    {D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2}, // first set by ICalcLighting :4983
    {D3DRS_MULTISAMPLEANTIALIAS, TRUE},            // IInitDeviceState :708 (TRUE with FSAA caps)
    // All four channels; IPreprocessAvatarTextures :11781 is the only setter and puts "the
    // default" back at :11830.
    {D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                                 D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA},
    {D3DRS_BLENDOP, D3DBLENDOP_ADD}, // first set by IHandleFirstStageBlend :6249-6253
};
// The render states with a 0 default that plDXPipeline reads before or without setting them,
// listed so that each is checked by name:
//  ALPHATESTENABLE FALSE (IInitDeviceState :704 TRUE), ALPHAREF 0 (:706 1), DITHERENABLE FALSE (:711),
//  ALPHABLENDENABLE FALSE (IHandleFirstStageBlend :6137), FOGENABLE FALSE (ISetFogParameters :4321/:4327),
//  SPECULARENABLE FALSE (IInitDeviceState :712), FOGCOLOR 0 (ISetFogParameters :4384), FOGTABLEMODE and
//  FOGVERTEXMODE D3DFOG_NONE (ISetFogParameters :4376, :4399/:4407 through d3dFogType :4304/:4308), FOGSTART
//  0.0 (ISetFogParameters :4400), RANGEFOGENABLE FALSE (never set), STENCILENABLE FALSE (IInitDeviceState
//  :716), AMBIENT 0 (ICalcLighting :4986), NORMALIZENORMALS FALSE (IInitDeviceState :723 TRUE),
//  AMBIENTMATERIALSOURCE and EMISSIVEMATERIALSOURCE D3DMCS_MATERIAL (ICalcLighting :4985/:4984), VERTEXBLEND
//  D3DVBF_DISABLE (ISetupTransforms :2539), ANTIALIASEDLINEENABLE FALSE (IInitDeviceState :709), DEPTHBIAS 0
//  (ISetLayer :5246).
const D3DRENDERSTATETYPE kZeroRenderStates[] = {
    D3DRS_ALPHATESTENABLE,
    D3DRS_ALPHAREF,
    D3DRS_DITHERENABLE,
    D3DRS_ALPHABLENDENABLE,
    D3DRS_FOGENABLE,
    D3DRS_SPECULARENABLE,
    D3DRS_FOGCOLOR,
    D3DRS_FOGTABLEMODE,
    D3DRS_FOGSTART,
    D3DRS_RANGEFOGENABLE,
    D3DRS_STENCILENABLE,
    D3DRS_AMBIENT,
    D3DRS_FOGVERTEXMODE,
    D3DRS_NORMALIZENORMALS,
    D3DRS_AMBIENTMATERIALSOURCE,
    D3DRS_EMISSIVEMATERIALSOURCE,
    D3DRS_VERTEXBLEND,
    D3DRS_ANTIALIASEDLINEENABLE,
    D3DRS_DEPTHBIAS,
};

bool IsIdentity(const D3DMATRIX& m)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            if (m.m[i][j] != (i == j ? 1.f : 0.f))
                return false;
    return true;
}

// Changes every state a draw can change.
void Dirty(plWGPUD3DDevice& d)
{
    for (int s = 0; s < kD3DRSCount; s++)
        d.fRenderState[s] = 0x5a5a5a5a;
    for (int i = 0; i < plWGPUD3DDevice::kNumStages; i++) {
        for (int t = 0; t < kD3DTSSCount; t++)
            d.SetTextureStageState(i, D3DTEXTURESTAGESTATETYPE(t), 0x5a);
        for (int t = 0; t < kD3DSAMPCount; t++)
            d.SetSamplerState(i, D3DSAMPLERSTATETYPE(t), 0x5a);
        d.SetTexture(i, reinterpret_cast<plWGPUTextureRef*>(uintptr_t(0x1000 + i)));
    }
    D3DMATRIX m = {};
    m.m[0][1] = 2.f;
    d.SetTransform(D3DTS_VIEW, &m);
    d.SetTransform(D3DTS_PROJECTION, &m);
    d.SetTransform(D3DTS_WORLD, &m);
    d.SetTransform(D3DTS_WORLDMATRIX(1), &m);
    for (int i = 0; i < plWGPUD3DDevice::kNumStages; i++)
        d.SetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0 + i), &m);
    D3DMATERIAL9 mat = {};
    mat.Power = 7.f;
    mat.Diffuse.r = 1.f;
    d.SetMaterial(&mat);
    D3DLIGHT9 light = {};
    light.Type = D3DLIGHT_POINT;
    d.SetLight(3, &light);
    d.LightEnable(3, true);
    d.SetVertexShader(reinterpret_cast<const plWGPUShaderProgram*>(uintptr_t(0x2000)));
    d.SetPixelShader(reinterpret_cast<const plWGPUShaderProgram*>(uintptr_t(0x3000)));
    const float c[4] = {1.f, 2.f, 3.f, 4.f};
    d.SetVertexShaderConstantF(95, c, 1);
    d.SetPixelShaderConstantF(7, c, 1);
    d.fUnsupportedFillMode = 9;
}
} // namespace

TEST(plWGPUD3DDevice, render_states_start_at_the_d3d9_defaults)
{
    plWGPUD3DDevice d;
    for (int s = 0; s < kD3DRSCount; s++) {
        auto        it = kRenderStateDefaults.find(D3DRENDERSTATETYPE(s));
        const DWORD expected = it != kRenderStateDefaults.end() ? it->second : 0;
        EXPECT_EQ(d.fRenderState[s], expected) << "render state " << s;
    }
    for (D3DRENDERSTATETYPE s : kZeroRenderStates)
        EXPECT_EQ(d.fRenderState[s], DWORD(0)) << "render state " << s;
    EXPECT_EQ(d.RenderStateFloat(D3DRS_FOGSTART), 0.f);
    EXPECT_EQ(d.RenderStateFloat(D3DRS_FOGEND), 1.f);
    EXPECT_EQ(d.RenderStateFloat(D3DRS_FOGDENSITY), 1.f);
}

TEST(plWGPUD3DDevice, texture_stage_states_start_at_the_d3d9_defaults)
{
    // Stage 0 modulates the texture by the diffuse colour and takes the texture's alpha; the
    // other stages are disabled. plDXPipeline sets COLOROP and the arguments per layer
    // (IHandleStageBlend, IHandleTextureMode, IStageStop :6956); COLORARG0, ALPHAARG0
    // and RESULTARG are never set, so D3DTA_CURRENT holds for the whole session.
    plWGPUD3DDevice d;
    for (DWORD i = 0; i < DWORD(plWGPUD3DDevice::kNumStages); i++) {
        const DWORD* t = d.fStageState[i];
        EXPECT_EQ(t[D3DTSS_COLOROP], DWORD(i == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE)) << "stage " << i;
        EXPECT_EQ(t[D3DTSS_COLORARG1], DWORD(D3DTA_TEXTURE)) << "stage " << i;
        EXPECT_EQ(t[D3DTSS_COLORARG2], DWORD(D3DTA_CURRENT)) << "stage " << i;
        EXPECT_EQ(t[D3DTSS_ALPHAOP], DWORD(i == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE)) << "stage " << i;
        EXPECT_EQ(t[D3DTSS_ALPHAARG1], DWORD(D3DTA_TEXTURE)) << "stage " << i;
        EXPECT_EQ(t[D3DTSS_ALPHAARG2], DWORD(D3DTA_CURRENT)) << "stage " << i;
        // The bump-environment matrix and luminance terms: 0 until IHandleBumpEnv sets them.
        for (D3DTEXTURESTAGESTATETYPE b : {D3DTSS_BUMPENVMAT00, D3DTSS_BUMPENVMAT01, D3DTSS_BUMPENVMAT10,
                                           D3DTSS_BUMPENVMAT11, D3DTSS_BUMPENVLSCALE, D3DTSS_BUMPENVLOFFSET})
            EXPECT_EQ(t[b], DWORD(0)) << "stage " << i << " state " << b;
        EXPECT_EQ(t[D3DTSS_TEXCOORDINDEX], i);                              // IInitDeviceState :740, the same value
        EXPECT_EQ(t[D3DTSS_TEXTURETRANSFORMFLAGS], DWORD(D3DTTFF_DISABLE)); // IInitDeviceState :762 (COUNT2)
        EXPECT_EQ(t[D3DTSS_COLORARG0], DWORD(D3DTA_CURRENT));
        EXPECT_EQ(t[D3DTSS_ALPHAARG0], DWORD(D3DTA_CURRENT));
        EXPECT_EQ(t[D3DTSS_RESULTARG], DWORD(D3DTA_CURRENT));
    }
}

TEST(plWGPUD3DDevice, sampler_states_start_at_the_d3d9_defaults)
{
    // Point sampling with no mipmapping and wrapping addresses. IInitDeviceState sets ADDRESSU (:741) and
    // ADDRESSV (:742), MIPMAPLODBIAS (:743), the filters and MAXANISOTROPY (:749);
    // ADDRESSW is never set, so D3DTADDRESS_WRAP holds.
    plWGPUD3DDevice d;
    for (DWORD i = 0; i < DWORD(plWGPUD3DDevice::kNumStages); i++) {
        const DWORD* s = d.fSamplerState[i];
        EXPECT_EQ(s[D3DSAMP_ADDRESSU], DWORD(D3DTADDRESS_WRAP)) << "sampler " << i;
        EXPECT_EQ(s[D3DSAMP_ADDRESSV], DWORD(D3DTADDRESS_WRAP)) << "sampler " << i;
        EXPECT_EQ(s[D3DSAMP_ADDRESSW], DWORD(D3DTADDRESS_WRAP)) << "sampler " << i;
        EXPECT_EQ(s[D3DSAMP_MAGFILTER], DWORD(D3DTEXF_POINT)) << "sampler " << i;
        EXPECT_EQ(s[D3DSAMP_MINFILTER], DWORD(D3DTEXF_POINT)) << "sampler " << i;
        EXPECT_EQ(s[D3DSAMP_MIPFILTER], DWORD(D3DTEXF_NONE)) << "sampler " << i;
        EXPECT_EQ(d.SamplerStateFloat(i, D3DSAMP_MIPMAPLODBIAS), 0.f) << "sampler " << i;
        EXPECT_EQ(s[D3DSAMP_MAXANISOTROPY], DWORD(1)) << "sampler " << i;
    }
}

TEST(plWGPUD3DDevice, transforms_start_as_identity)
{
    // World, view, projection and the eight texture transforms. IInitDeviceState sets
    // D3DTS_TEXTURE0 (:717), D3DTS_WORLD (:718) and every texture transform (sTextureStages, :760) to the identity.
    plWGPUD3DDevice d;
    for (const D3DMATRIX& w : d.fWorld)
        EXPECT_TRUE(IsIdentity(w));
    EXPECT_TRUE(IsIdentity(d.fView));
    EXPECT_TRUE(IsIdentity(d.fProjection));
    for (const D3DMATRIX& t : d.fTexture)
        EXPECT_TRUE(IsIdentity(t));
    EXPECT_EQ(d.fTransformSerial, uint32_t(0));
}

TEST(plWGPUD3DDevice, no_material_light_texture_or_program_at_start)
{
    // No texture bound (IInitDeviceState :739 binds none), no light set or enabled (plDXPipeline
    // sets them per span: IMakeLightRef :4652, ISelectLights :4794), a zero material until
    // ICalcLighting :4981, the fixed-function stage (no vertex or pixel program) with zero
    // constant registers.
    plWGPUD3DDevice d;
    for (plWGPUTextureRef* t : d.fTextures)
        EXPECT_EQ(t, nullptr);
    EXPECT_TRUE(d.fLights.empty());
    EXPECT_TRUE(d.fLightEnabled.empty());
    EXPECT_TRUE(d.fEnabledLights.empty());
    const D3DMATERIAL9 zero = {};
    EXPECT_EQ(memcmp(&d.fMaterial, &zero, sizeof(zero)), 0);
    EXPECT_EQ(d.fVertexShader, nullptr);
    EXPECT_EQ(d.fPixelShader, nullptr);
    for (const auto& c : d.fVSConsts)
        for (float f : c)
            EXPECT_EQ(f, 0.f);
    for (const auto& c : d.fPSConsts)
        for (float f : c)
            EXPECT_EQ(f, 0.f);
    EXPECT_EQ(d.fUnsupportedFillMode, uint32_t(0));
}

TEST(plWGPUD3DDevice, reset_returns_every_state_to_its_start)
{
    // IDirect3DDevice9::Reset returns the device to its default state; plDXPipeline resets the
    // device (IResetDevice :1932, Resize :2165) and runs IInitDeviceState again (:1952, :2170).
    plWGPUD3DDevice fresh;
    plWGPUD3DDevice d;
    Dirty(d);
    d.Reset();
    EXPECT_EQ(memcmp(d.fRenderState, fresh.fRenderState, sizeof(fresh.fRenderState)), 0);
    EXPECT_EQ(memcmp(d.fStageState, fresh.fStageState, sizeof(fresh.fStageState)), 0);
    EXPECT_EQ(memcmp(d.fSamplerState, fresh.fSamplerState, sizeof(fresh.fSamplerState)), 0);
    EXPECT_EQ(memcmp(d.fWorld, fresh.fWorld, sizeof(fresh.fWorld)), 0);
    EXPECT_EQ(memcmp(&d.fView, &fresh.fView, sizeof(fresh.fView)), 0);
    EXPECT_EQ(memcmp(&d.fProjection, &fresh.fProjection, sizeof(fresh.fProjection)), 0);
    EXPECT_EQ(memcmp(d.fTexture, fresh.fTexture, sizeof(fresh.fTexture)), 0);
    EXPECT_EQ(memcmp(&d.fMaterial, &fresh.fMaterial, sizeof(fresh.fMaterial)), 0);
    EXPECT_EQ(memcmp(d.fTextures, fresh.fTextures, sizeof(fresh.fTextures)), 0);
    EXPECT_EQ(memcmp(d.fVSConsts, fresh.fVSConsts, sizeof(fresh.fVSConsts)), 0);
    EXPECT_EQ(memcmp(d.fPSConsts, fresh.fPSConsts, sizeof(fresh.fPSConsts)), 0);
    EXPECT_TRUE(d.fLights.empty());
    EXPECT_TRUE(d.fLightEnabled.empty());
    EXPECT_TRUE(d.fEnabledLights.empty());
    EXPECT_EQ(d.fVertexShader, nullptr);
    EXPECT_EQ(d.fPixelShader, nullptr);
    EXPECT_EQ(d.fTransformSerial, uint32_t(0));
    EXPECT_EQ(d.fUnsupportedFillMode, uint32_t(0));
}
