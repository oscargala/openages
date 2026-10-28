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

// A port of pfMetalPipeline/plMetalDeviceRefs.cpp, with the light refs of
// pfDXPipeline/plDXDeviceRefs.cpp (plDXLightRef).

#include "plGLight/plLightInfo.h"
#include "plPipeline/plCubicRenderTarget.h"
#include "plWGPUDeviceRef.h"
#include "plWGPULightRef.h"
#include "plWGPUSettings.h"
#ifdef PLASMA_TEST_HOOKS
#include "plWGPUPipelineObserver.h"
#endif

using namespace plWGPUD3D9;

/*****************************************************************************
 ** Generic plWGPUDeviceRef Functions                                       **
 *****************************************************************************/
plWGPUDeviceRef::plWGPUDeviceRef()
    : fNext(),
      fBack()
{
}

plWGPUDeviceRef::~plWGPUDeviceRef()
{
    if (fNext != nullptr || fBack != nullptr)
        Unlink();
}

void plWGPUDeviceRef::Unlink()
{
    hsAssert(fBack, "plWGPUDeviceRef not in list");
    if (fNext)
        fNext->fBack = fBack;
    *fBack = fNext;

    fBack = nullptr;
    fNext = nullptr;
}

uint32_t plWGPUBufferPoolRef::fFrameTime(0);

void plWGPUDeviceRef::Link(plWGPUDeviceRef** back)
{
    hsAssert(fNext == nullptr && fBack == nullptr, "Trying to link a plWGPUDeviceRef that's already linked");

    fNext = *back;
    if (*back)
        (*back)->fBack = &fNext;
    fBack = back;
    *back = this;
}

/*****************************************************************************
 ** Vertex buffer cleanup Functions                                         **
 *****************************************************************************/

plWGPUVertexBufferRef::~plWGPUVertexBufferRef()
{
    delete[] fData;
    Release();
}

void plWGPUVertexBufferRef::Release()
{
    // plMetalVertexBufferRef::Release only marks the ref dirty; D3D9 frees the buffer
    // (plDXVertexBufferRef::Release), which a refill then recreates.
    ReleaseBuffers();
    SetDirty(true);
}

/*****************************************************************************
 ** Index buffer cleanup Functions                                          **
 *****************************************************************************/

plWGPUIndexBufferRef::~plWGPUIndexBufferRef()
{
    Release();
}

void plWGPUIndexBufferRef::Release()
{
    ReleaseBuffers();
    SetDirty(true);
}

/*****************************************************************************
 ** Texture cleanup Functions                                               **
 *****************************************************************************/

uint32_t plWGPUTextureRef::fReleaseSerial(0);

void plWGPUTextureRef::Release()
{
    // The device caches bind groups by texture view; a released view's address can be reused
    // by a new one, so every release invalidates that cache (plWGPUDevice::IFixedBindGroup).
    if (fTextureView || fTexture)
        fReleaseSerial++;
    if (fTextureView) {
        wgpuTextureViewRelease(fTextureView);
        fTextureView = nullptr;
    }
    if (fTexture) {
        wgpuTextureRelease(fTexture);
        fTexture = nullptr;
    }
    SetDirty(true);
}

plWGPUTextureRef::~plWGPUTextureRef()
{
    Release();

    if (fNext != nullptr || fBack != nullptr)
        Unlink();
}

/*****************************************************************************
 ** FrameBuffer cleanup Functions                                           **
 *****************************************************************************/

plWGPURenderTargetRef::~plWGPURenderTargetRef()
{
    Release();
}

void plWGPURenderTargetRef::Release()
{
#ifdef PLASMA_TEST_HOOKS
    // The test hooks' shader rows drop a recorded target whose ref this is
    // (plWGPUPipelineObserver::OnRenderTargetRelease).
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
        observer->OnRenderTargetRelease(this);
#endif
    // plDXRenderTargetRef::Release: a cubic target's release releases its faces' refs and marks
    // them dirty.
    if (fCubic) {
        if (plCubicRenderTarget* cubic = plCubicRenderTarget::ConvertNoRef(fOwner)) {
            for (int i = 0; i < 6; i++) {
                plWGPURenderTargetRef* face = (plWGPURenderTargetRef*)cubic->GetFace(i)->GetDeviceRef();
                if (face && face != this) {
                    face->Release();
                    face->SetDirty(true);
                }
            }
        }
    }
    fDepthFormat = WGPUTextureFormat_Undefined;
    if (fRenderView) {
        wgpuTextureViewRelease(fRenderView);
        fRenderView = nullptr;
    }
    if (fDepthView) {
        wgpuTextureViewRelease(fDepthView);
        fDepthView = nullptr;
    }
    if (fDepthBuffer) {
        wgpuTextureRelease(fDepthBuffer);
        fDepthBuffer = nullptr;
    }
    plWGPUTextureRef::Release();
    SetDirty(true);
}

/*****************************************************************************
 ** Light cleanup and update Functions (plDXDeviceRefs.cpp, plDXLightRef)   **
 *****************************************************************************/

void plWGPULightRef::UpdateD3DInfo(plWGPUD3DDevice* dev, plWGPULightSettings* settings)
{
    plDirectionalLightInfo* dirOwner;
    plOmniLightInfo*        omniOwner;
    plSpotLightInfo*        spotOwner;
    const float             maxRange = 32767.f;

    /// Properties that are set for all types
    fD3DDevice = dev;
    fParentSettings = settings;

    memset(&fD3DInfo, 0, sizeof(D3DLIGHT9));
    auto setColor = [](D3DCOLORVALUE& v, const hsColorRGBA& color) {
        v.r = color.r;
        v.g = color.g;
        v.b = color.b;
        v.a = color.a;
    };
    setColor(fD3DInfo.Diffuse, fOwner->GetDiffuse());
    setColor(fD3DInfo.Ambient, fOwner->GetAmbient());
    setColor(fD3DInfo.Specular, fOwner->GetSpecular());

    if ((omniOwner = plOmniLightInfo::ConvertNoRef(fOwner)) != nullptr) {
        fD3DInfo.Type = D3DLIGHT_POINT;

        hsPoint3 position = omniOwner->GetWorldPosition();
        fD3DInfo.Position.x = position.fX;
        fD3DInfo.Position.y = position.fY;
        fD3DInfo.Position.z = position.fZ;

        if (omniOwner->GetRadius() == 0)
            fD3DInfo.Range = maxRange;
        else
            fD3DInfo.Range = omniOwner->GetRadius();
        fD3DInfo.Attenuation0 = omniOwner->GetConstantAttenuation();
        fD3DInfo.Attenuation1 = omniOwner->GetLinearAttenuation();
        fD3DInfo.Attenuation2 = omniOwner->GetQuadraticAttenuation();

        // If the light is a spot, but it has a projected texture, then
        // the cone attenuation is handled by the texture. We're only using
        // the D3D light for distance attenuation and the N*L term. So
        // we can just leave the D3D light as the cheaper and more stable
        // Omni light. (plDXLightRef::UpdateD3DInfo)
        if (!omniOwner->GetProjection() && (spotOwner = plSpotLightInfo::ConvertNoRef(fOwner))) {
            fD3DInfo.Type = D3DLIGHT_SPOT;

            hsVector3 direction = spotOwner->GetWorldDirection();
            fD3DInfo.Direction.x = direction.fX;
            fD3DInfo.Direction.y = direction.fY;
            fD3DInfo.Direction.z = direction.fZ;

            fD3DInfo.Falloff = spotOwner->GetFalloff();
            fD3DInfo.Theta = spotOwner->GetSpotInner() * 2;
            fD3DInfo.Phi = spotOwner->GetSpotOuter() * 2;
        }
    } else if ((dirOwner = plDirectionalLightInfo::ConvertNoRef(fOwner)) != nullptr) {
        fD3DInfo.Type = D3DLIGHT_DIRECTIONAL;

        hsVector3 direction = dirOwner->GetWorldDirection();
        fD3DInfo.Direction.x = direction.fX;
        fD3DInfo.Direction.y = direction.fY;
        fD3DInfo.Direction.z = direction.fZ;
    } else {
        hsAssert(false, "Unrecognized light type passed to plWGPULightRef::UpdateD3DInfo()");
        return;
    }

    fD3DDevice->SetLight(fD3DIndex, &fD3DInfo);
    fScale = 1.f;
}

plWGPULightRef::~plWGPULightRef()
{
    Release();
}

void plWGPULightRef::Release()
{
    // Ensure that this light is disabled
    if (fD3DDevice) {
        fD3DDevice->LightEnable(fD3DIndex, false);
        fD3DDevice = nullptr;
    }

    if (fParentSettings) {
        fParentSettings->fEnabledFlags.SetBit(fD3DIndex, false);
        fParentSettings->ReleaseD3DIndex(fD3DIndex);
        fParentSettings = nullptr;
    }
    fD3DIndex = 0;

    SetDirty(true);
}
