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

// The D3D9 fixed-function device state of pfWGPUPipeline (see plWGPUD3DDevice.h).

#include "plWGPUD3DDevice.h"

#include <algorithm>

#include "hsMatrix44.h"

using namespace plWGPUD3D9;

const D3DMATRIX plWGPUD3D9::d3dIdentityMatrix{{{1.f, 0.f, 0.f, 0.f},
                                               {0.f, 1.f, 0.f, 0.f},
                                               {0.f, 0.f, 1.f, 0.f},
                                               {0.f, 0.f, 0.f, 1.f}}};

void plWGPUD3DDevice::Reset()
{
    // The defaults of a freshly created IDirect3DDevice9 (D3D9 SDK: D3DRENDERSTATETYPE,
    // D3DTEXTURESTAGESTATETYPE, D3DSAMPLERSTATETYPE, "Device state defaults"). Only the states
    // plDXPipeline sets or the fixed-function pipeline reads are listed; the rest stay 0.
    memset(fRenderState, 0, sizeof(fRenderState));
    fVertexShader = nullptr;
    fPixelShader = nullptr;
    memset(fVSConsts, 0, sizeof(fVSConsts));
    memset(fPSConsts, 0, sizeof(fPSConsts));
    fRenderState[D3DRS_ZENABLE] = D3DZB_TRUE; // the device has an auto depth-stencil
    fRenderState[D3DRS_FILLMODE] = D3DFILL_SOLID;
    fRenderState[D3DRS_ZWRITEENABLE] = 1;
    fRenderState[D3DRS_ALPHATESTENABLE] = 0;
    fRenderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    fRenderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    fRenderState[D3DRS_CULLMODE] = D3DCULL_CCW;
    fRenderState[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    fRenderState[D3DRS_ALPHAREF] = 0;
    fRenderState[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    fRenderState[D3DRS_ALPHABLENDENABLE] = 0;
    fRenderState[D3DRS_FOGENABLE] = 0;
    fRenderState[D3DRS_SPECULARENABLE] = 0;
    fRenderState[D3DRS_FOGCOLOR] = 0;
    fRenderState[D3DRS_FOGTABLEMODE] = D3DFOG_NONE;
    fRenderState[D3DRS_FOGSTART] = F2DW(0.f);
    fRenderState[D3DRS_FOGEND] = F2DW(1.f);
    fRenderState[D3DRS_FOGDENSITY] = F2DW(1.f);
    fRenderState[D3DRS_TEXTUREFACTOR] = 0xffffffff;
    fRenderState[D3DRS_CLIPPING] = 1;
    fRenderState[D3DRS_LIGHTING] = 1;
    fRenderState[D3DRS_AMBIENT] = 0;
    fRenderState[D3DRS_FOGVERTEXMODE] = D3DFOG_NONE;
    fRenderState[D3DRS_COLORVERTEX] = 1;
    fRenderState[D3DRS_LOCALVIEWER] = 1;
    fRenderState[D3DRS_NORMALIZENORMALS] = 0;
    fRenderState[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    fRenderState[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    fRenderState[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    fRenderState[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    fRenderState[D3DRS_VERTEXBLEND] = D3DVBF_DISABLE;
    fRenderState[D3DRS_MULTISAMPLEANTIALIAS] = 1;
    fRenderState[D3DRS_COLORWRITEENABLE] = 0x0000000f;
    fRenderState[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    fRenderState[D3DRS_DEPTHBIAS] = 0;

    memset(fStageState, 0, sizeof(fStageState));
    memset(fSamplerState, 0, sizeof(fSamplerState));
    for (int i = 0; i < kNumStages; i++) {
        fStageState[i][D3DTSS_COLOROP] = i ? D3DTOP_DISABLE : D3DTOP_MODULATE;
        fStageState[i][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        fStageState[i][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        fStageState[i][D3DTSS_ALPHAOP] = i ? D3DTOP_DISABLE : D3DTOP_SELECTARG1;
        fStageState[i][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        fStageState[i][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        fStageState[i][D3DTSS_TEXCOORDINDEX] = i;
        fStageState[i][D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
        fStageState[i][D3DTSS_COLORARG0] = D3DTA_CURRENT;
        fStageState[i][D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
        fStageState[i][D3DTSS_RESULTARG] = D3DTA_CURRENT;

        fSamplerState[i][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
        fSamplerState[i][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
        fSamplerState[i][D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        fSamplerState[i][D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
        fSamplerState[i][D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        fSamplerState[i][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        fSamplerState[i][D3DSAMP_MIPMAPLODBIAS] = F2DW(0.f);
        fSamplerState[i][D3DSAMP_MAXANISOTROPY] = 1;

        fTexture[i] = d3dIdentityMatrix;
        fTextures[i] = nullptr;
    }
    for (D3DMATRIX& world : fWorld)
        world = d3dIdentityMatrix;
    fView = d3dIdentityMatrix;
    fProjection = d3dIdentityMatrix;
    memset(&fMaterial, 0, sizeof(fMaterial));

    fLights.clear();
    fLightEnabled.clear();
    fEnabledLights.clear();
    fTransformSerial = 0;
    fUnsupportedFillMode = 0;
}

void plWGPUD3DDevice::SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix)
{
    if (state == D3DTS_VIEW) {
        fView = *matrix;
    } else if (state == D3DTS_PROJECTION) {
        fProjection = *matrix;
    } else if (state >= D3DTS_TEXTURE0 && state <= D3DTS_TEXTURE7) {
        fTexture[state - D3DTS_TEXTURE0] = *matrix;
        return;
    } else if (state >= D3DTS_WORLD && state < D3DTS_WORLD + kNumWorldMatrices) {
        fWorld[state - D3DTS_WORLD] = *matrix;
    } else {
        return;
    }
    fTransformSerial++;
}

void plWGPUD3DDevice::SetLight(DWORD index, const D3DLIGHT9* light)
{
    if (index >= fLights.size()) {
        fLights.resize(index + 1);
        fLightEnabled.resize(index + 1, 0);
    }
    fLights[index] = *light;
}

void plWGPUD3DDevice::LightEnable(DWORD index, bool enable)
{
    // A light that was never set is a default light in D3D9; plDXPipeline only enables lights
    // whose UpdateD3DInfo has run (IMakeLightRef, ISelectLights), so every index here is set.
    if (index >= fLightEnabled.size()) {
        fLights.resize(index + 1);
        fLightEnabled.resize(index + 1, 0);
    }
    if (bool(fLightEnabled[index]) == enable)
        return;
    fLightEnabled[index] = enable ? 1 : 0;
    if (enable) {
        fEnabledLights.push_back(index);
    } else {
        auto it = std::find(fEnabledLights.begin(), fEnabledLights.end(), index);
        if (it != fEnabledLights.end())
            fEnabledLights.erase(it);
    }
}

D3DMATRIX& plWGPUD3D9::IMatrix44ToD3DMatrix(D3DMATRIX& dst, const hsMatrix44& src)
{
    // plDXPipeline::IMatrix44ToD3DMatrix
    if (src.fFlags & hsMatrix44::kIsIdent) {
        dst = d3dIdentityMatrix;
    } else {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                dst.m[j][i] = src.fMap[i][j];
    }

    return dst;
}
