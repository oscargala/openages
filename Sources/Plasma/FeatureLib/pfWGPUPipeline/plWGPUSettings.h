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

#ifndef _plWGPUSettings_h_
#define _plWGPUSettings_h_

// The general, fog and light settings of pfWGPUPipeline, ports of plDXGeneralSettings,
// plDXFogSettings and plDXLightSettings (pfDXPipeline/plDXSettings.h; their Reset functions in
// plDXPipeline.cpp), with the members the ported material code reads.

#include <vector>

#include "hsBitVector.h"
#include "hsColorRGBA.h"
#include "hsPoolVector.h"
#include "plWGPUD3DDevice.h"

class plFogEnvironment;
class plLightInfo;
class plWGPULightRef;
class plWGPUPipeline;
struct plWGPUShaderProgram;

class plWGPUGeneralSettings
{
public:
    uint32_t                   fD3DCaps;
    uint32_t                   fMaxUVWSrc;
    uint8_t                    fMaxAnisotropicSamples;
    bool                       fCurrAnisotropy;
    bool                       fCantProj;
    bool                       fIsIntel;
    bool                       fVeryAnnoyingTextureInvalidFlag;
    // plDXGeneralSettings::fNoGammaCorrect (set in plDXPipeline::ISetCaps): false, the windowed
    // client's SetGamma returns true; no caller reads the return value.
    bool                       fNoGammaCorrect;
    // The programs set on the device (plDXPipeline.h fCurrVertexShader/fCurrPixelShader).
    const plWGPUShaderProgram* fCurrVertexShader;
    const plWGPUShaderProgram* fCurrPixelShader;

    // plDXGeneralSettings::Reset. The current vertex and index
    // buffer refs are not kept: WebGPU binds the buffers with each draw.
    void Reset()
    {
        fCurrVertexShader = nullptr;
        fCurrPixelShader = nullptr;
        fD3DCaps = 0;
        fMaxUVWSrc = 8;
        fMaxAnisotropicSamples = 0;
        fCurrAnisotropy = false;
        fCantProj = false;
        fIsIntel = false;
        fVeryAnnoyingTextureInvalidFlag = false;
        fNoGammaCorrect = false;
    }
};

class plWGPUFogSettings
{
public:
    const plFogEnvironment* fEnvPtr; // nil means no fog
    plWGPUD3D9::D3DFOGMODE  fMode;
    uint8_t                 fIsVertex;
    uint8_t                 fIsShader;
    uint32_t                fHexColor;
    float                   fStart;
    float                   fEnd;
    float                   fDensity;
    hsColorRGBA             fColor;

    void Reset()
    {
        fEnvPtr = nullptr;
        fMode = plWGPUD3D9::D3DFOG_NONE;
        fIsVertex = 0;
        fIsShader = 0;
        fHexColor = 0;
        fStart = fEnd = fDensity = 0.0f;
        fColor.Set(0, 0, 0, 0);
    }
};

class plWGPULightSettings
{
public:
    hsBitVector                   fUsedFlags;
    hsBitVector                   fEnabledFlags;
    hsBitVector                   fHoldFlags;
    uint32_t                      fNextIndex, fLastIndex;
    uint16_t                      fTime;
    plLightInfo*                  fActiveList;
    plWGPULightRef*               fRefList;
    plWGPUPipeline*               fPipeline;
    std::vector<plLightInfo*>     fProjEach;
    std::vector<plLightInfo*>     fProjAll;
    hsPoolVector<plWGPULightRef*> fShadowLights; // plDXLightSettings::fShadowLights

    plWGPULightSettings()
        : fNextIndex(), fLastIndex(), fTime(), fActiveList(), fRefList(), fPipeline()
    {
    }

    // Sets member variables to initial states. Does NOT release anything.
    void     Reset(plWGPUPipeline* pipe);
    // Releases/deletes anything associated with these settings
    void     Release();
    // Reserve a D3D light index
    uint32_t ReserveD3DIndex();
    // Release a reserved D3D light index
    void     ReleaseD3DIndex(uint32_t idx);
};

#endif // _plWGPUSettings_h_
