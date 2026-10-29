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

// The WebGPU device enumerator (a port of plMetalEnumerate.mm) and the client thread's
// adapter/device/surface acquisition.

#include <emscripten/emscripten.h>
#include <pthread.h>

#include <cstring>
#include <string>
#include <string_theory/format>

#include "HeadSpin.h"
#include "plPipeline/hsG3DDeviceSelector.h"
#include "plPipeline/pl3DPipeline.h"
#include "plWGPULog.h"
#include "plWGPUPipeline.h"
#ifdef PLASMA_TEST_HOOKS
#include "plWGPUPipelineObserver.h"
#endif

extern "C" {
// plWGPUPipeline.js
int  plWGPU_textureSerial(void* texture);
void plWGPU_reportBootFailure(const char* message);
void plWGPU_reportFirstPresent();
}

namespace
{
struct State
{
    double    t0 = 0.0;
    pthread_t owner{};

    WGPUInstance instance = nullptr;
    WGPUSurface  surface = nullptr;
    WGPUAdapter  adapter = nullptr;
    WGPUDevice   device = nullptr;
    bool         bc = false;
    bool         ready = false;
    bool         failed = false;
    ST::string   adapterDesc;
    ST::string   adapterVendor;
    bool         specialisedFixed = false; // plWGPUEnumerate::SpecialisedFixedPrograms

    unsigned long errors = 0;

    // Device loss (plDXPipeline's fDeviceLost on D3DERR_DEVICELOST): set by the
    // device-lost callback on the client thread's event loop, read by the pipeline at
    // BeginRender only. A re-acquire re-requests the adapter and device with the callbacks below.
    bool          lost = false;
    bool          shuttingDown = false;
    bool          reacquiring = false;
    bool          releasing = false;   // inside Shutdown's own release of the device
    bool          shutDown = false;    // Shutdown has run: nothing is acquired any more
    WGPUDevice    oldDevice = nullptr; // the lost device, until the pipeline has let it go
    WGPUAdapter   oldAdapter = nullptr;
    unsigned long losses = 0;
    double        lostAt = 0.0;

    // A new canvas texture means the previous one was presented (NoteCanvasTexture).
    int  lastTexSerial = 0;
    bool firstPresentReported = false; // the page was told of the first presented frame
} s;

double Now() { return emscripten_get_now(); }

std::string View(WGPUStringView v)
{
    if (!v.data)
        return {};
    return v.length == WGPU_STRLEN ? std::string(v.data) : std::string(v.data, v.length);
}

WGPUStringView Str(const char* text) { return WGPUStringView{text, std::strlen(text)}; }

void Fail(const std::string& what)
{
    s.failed = true;
    plWGPULog("ACQUIRE FAILED: {}", what);
    plWGPU_reportBootFailure(what.c_str()); // tells the page only; the failure state above is unchanged
}

void OnUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*)
{
    // Every uncaptured WebGPU error is a renderer defect: the first 50 are logged as
    // "WebGPU: error ...", the rest only counted.
    s.errors++;
    if (s.errors <= 50)
        plWGPULog("error type {}: {}", int(type), View(msg));
    else if (s.errors == 51)
        plWGPULog("error: more than 50 errors; further ones are only counted");
}

void OnDeviceLost(WGPUDevice const* device, WGPUDeviceLostReason reason, WGPUStringView msg, void*, void*)
{
    // A lost device is D3D9's D3DERR_DEVICELOST (plDXPipeline::IResetDevice): the
    // pipeline's next BeginRender sees the flag. Not while shutting down, not for a device whose
    // creation failed (Fail() reports it) and not when the instance cancels the callback.
    // Shutdown's release of the device at exit destroys it: not a loss (that callback gets no device handle).
    if (s.releasing)
        return;
    const bool current = device && *device == s.device;
    plWGPULog("DEVICE LOST reason {}{}: {}", int(reason), current ? "" : " (not the current device)", View(msg));
    if (s.shuttingDown || !current || reason == WGPUDeviceLostReason_CallbackCancelled ||
        reason == WGPUDeviceLostReason_FailedCreation)
        return;
    s.lost = true;
    s.losses++;
    s.lostAt = Now();
}

void OnDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView msg, void*, void*)
{
    if (s.shutDown) {
        // A re-acquire still in flight at exit: nothing takes the device any more
        if (device)
            wgpuDeviceRelease(device);
        return;
    }
    if (status != WGPURequestDeviceStatus_Success || !device) {
        Fail("requestDevice: " + View(msg));
        return;
    }
    s.device = device;
    s.ready = true;
    if (s.reacquiring) {
        plWGPULog("DEVICE READY again, {.1f} ms after the loss (texture-compression-bc {})",
                  Now() - s.lostAt, s.bc ? "enabled" : "not available");
        return;
    }
    plWGPULog("DEVICE READY after {.1f} ms on the client thread (texture-compression-bc {})",
              Now() - s.t0, s.bc ? "enabled" : "not available");
}

void OnAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView msg, void*, void*)
{
    if (s.shutDown) {
        // A re-acquire still in flight at exit: nothing takes the adapter any more
        if (adapter)
            wgpuAdapterRelease(adapter);
        return;
    }
    if (status != WGPURequestAdapterStatus_Success || !adapter) {
        Fail("requestAdapter: " + View(msg));
        return;
    }
    s.adapter = adapter;
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    wgpuAdapterGetInfo(adapter, &info);
    s.adapterVendor = ST::string::from_utf8(View(info.vendor).c_str());
    s.adapterDesc = ST::string::from_utf8(View(info.description).c_str());
    if (s.adapterDesc.empty())
        s.adapterDesc = ST::string::from_utf8(View(info.architecture).c_str());
    plWGPULog("ADAPTER after {.1f} ms: vendor '{}' architecture '{}' description '{}' backend {}",
              Now() - s.t0, View(info.vendor), View(info.architecture), View(info.description), int(info.backendType));
    // The specialised fixed-function programs run only where they were measured to draw what the uber shader draws:
    // an Apple GPU on Metal in Dawn, whose adapter info has vendor "apple" and architecture "metal-<n>" (Chromium
    // reports "metal-3"). Two measurements make that claim. The pipeline bench drew every proven program against the
    // uber, bit for bit, which covers the arithmetic. The client's own frames of six posed scenes matched the uber
    // build's to the pixel, which covers what the bench cannot see: on Metal the frame also depends on which
    // interstage variables a fragment program reads. WebKit on Metal (vendor "apple", architecture "apple", as Safari
    // 27.0.1 reports it) has the bench's identity but no client frames yet, so it keeps the uber shader until its own
    // frames of those scenes match. Every other adapter keeps it because the fma pins of FixedPipelineShaders.wgsl
    // hold only where fma is fused, as Metal's is. A WebKit that reported "metal-<n>" would be admitted, so the
    // FIXED PROGRAMS line below says which way each adapter went.
    const std::string vendor = View(info.vendor), architecture = View(info.architecture);
    s.specialisedFixed = vendor == "apple" && architecture.rfind("metal-", 0) == 0;
    const char* admittedBy = "";
#ifdef PLASMA_TEST_HOOKS
    // A harness build may run WebKit on Metal specialised, to take the measurement above (the observer's switch). No
    // other adapter is ever offered.
    if (!s.specialisedFixed && vendor == "apple" && architecture == "apple") {
        plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get();
        if (observer && observer->AdmitFixedPrograms(vendor.c_str(), architecture.c_str())) {
            s.specialisedFixed = true;
            admittedBy = " by PLASMA_TEST_FIXED_PROGRAMS_WEBKIT=1";
        }
    }
#endif
    plWGPULog("FIXED PROGRAMS: {}{} (adapter vendor '{}' architecture '{}')",
              s.specialisedFixed ? "specialised" : "uber shader only", admittedBy, vendor, architecture);
    wgpuAdapterInfoFreeMembers(info);

    // DXT1/DXT5 upload as BC1/BC3 when the adapter has the optional feature, else they are
    // decompressed in software (plMetalDevice::InitDevice).
    s.bc = wgpuAdapterHasFeature(adapter, WGPUFeatureName_TextureCompressionBC);
    const WGPUFeatureName features[1] = {WGPUFeatureName_TextureCompressionBC};
    const size_t          featureCount = s.bc ? 1 : 0;

    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.label = Str("plWGPUPipeline device");
    dd.requiredFeatureCount = featureCount;
    dd.requiredFeatures = featureCount ? features : nullptr;
    dd.uncapturedErrorCallbackInfo.callback = OnUncapturedError;
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    dd.deviceLostCallbackInfo.callback = OnDeviceLost;
    WGPURequestDeviceCallbackInfo cbi = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    cbi.mode = WGPUCallbackMode_AllowSpontaneous;
    cbi.callback = OnDevice;
    wgpuAdapterRequestDevice(adapter, &dd, cbi);
}
} // namespace

double plWGPULogTime()
{
    return (Now() - s.t0) / 1000.0;
}

void plWGPUEnumerate::StartAcquire(double t0Ms)
{
    s.owner = pthread_self();
    s.t0 = t0Ms;

    // TimedWaitAny: CaptureScreen's GPU readback waits with wgpuInstanceWaitAny, which
    // needs it for a non-zero timeout (emdawnwebgpu webgpu.cpp WGPUInstanceImpl::WaitAny) and
    // suspends the tick under JSPI (emwgpuWaitAny__async).
    const WGPUInstanceFeatureName instanceFeatures[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor        id = WGPU_INSTANCE_DESCRIPTOR_INIT;
    id.requiredFeatureCount = 1;
    id.requiredFeatures = instanceFeatures;
    s.instance = wgpuCreateInstance(&id);
    if (!s.instance) {
        Fail("wgpuCreateInstance with TimedWaitAny");
        return;
    }
    // Everything acquired here is released at exit by Shutdown, which the app calls.
    // The OffscreenCanvas the page transferred to the client thread
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector src = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    src.selector = Str("#canvas");
    WGPUSurfaceDescriptor sd = WGPU_SURFACE_DESCRIPTOR_INIT;
    sd.nextInChain = &src.chain;
    sd.label = Str("plWGPUPipeline canvas");
    s.surface = wgpuInstanceCreateSurface(s.instance, &sd);
    if (!s.surface) {
        Fail("wgpuInstanceCreateSurface('#canvas')");
        return;
    }

    WGPURequestAdapterOptions opts = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    opts.compatibleSurface = s.surface;
    WGPURequestAdapterCallbackInfo cbi = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    cbi.mode = WGPUCallbackMode_AllowSpontaneous;
    cbi.callback = OnAdapter;
    wgpuInstanceRequestAdapter(s.instance, &opts, cbi);
    plWGPULog("requestAdapter issued (resolves when this thread returns to its event loop)");
}

bool         plWGPUEnumerate::Ready() { return s.ready; }
bool         plWGPUEnumerate::Failed() { return s.failed; }
WGPUDevice   plWGPUEnumerate::Device() { return s.device; }
WGPUSurface  plWGPUEnumerate::Surface() { return s.surface; }
WGPUInstance plWGPUEnumerate::Instance() { return s.instance; }

void plWGPUEnumerate::Shutdown()
{
    // At exit, after the pipeline (and every object it made on the device) is gone, or after an exit that never made
    // one (a failed acquire, no client, no suitable device): the device, then the adapter, the surface and the
    // instance. The device goes after everything made on it, as plDXPipeline::IReleaseDeviceObjects releases
    // fD3DDevice last. The rest lives for the process, as hsGDirect3D's IDirect3D9 does (a static released at exit),
    // so a pipeline deleted and made again (plClient::InitPipeline) still finds the device. The app calls it from
    // Finish() (Apps/plClient/emscripten/main.cpp) on the client thread, after the client is shut down and before the
    // static destructors, so before emdawnwebgpu's event manager (a function-local static that the instance registers
    // with) is destroyed.
    // Only on the client thread: on another, this thread's WebGPU objects do not exist. Nothing to release when no
    // instance was made (no acquire, or wgpuCreateInstance failed): every other object comes from it. It logs through
    // plWGPULog only (printf): the status logs may be gone by now.
    if (s.shutDown || !s.instance || !pthread_equal(pthread_self(), s.owner))
        return;
    s.shutDown = true;
    s.shuttingDown = true;
    s.ready = false;
    s.releasing = true; // the release destroys the device and runs its lost callback now: not a loss
    for (WGPUDevice* device : {&s.device, &s.oldDevice}) {
        if (*device)
            wgpuDeviceRelease(*device);
        *device = nullptr;
    }
    s.releasing = false;
    for (WGPUAdapter* adapter : {&s.adapter, &s.oldAdapter}) {
        if (*adapter)
            wgpuAdapterRelease(*adapter);
        *adapter = nullptr;
    }
    if (s.surface)
        wgpuSurfaceRelease(s.surface);
    s.surface = nullptr;
    if (s.instance)
        wgpuInstanceRelease(s.instance);
    s.instance = nullptr;
    plWGPULog("released the device, the adapter, the canvas surface and the instance");
}

bool          plWGPUEnumerate::DeviceLost() { return s.lost; }
unsigned long plWGPUEnumerate::DeviceLosses() { return s.losses; }
void          plWGPUEnumerate::SetShuttingDown() { s.shuttingDown = true; }

void plWGPUEnumerate::StartReacquire()
{
    // The adapter and device again, with the same callbacks; nothing waits: the pipeline's
    // BeginRender returns true until Ready() (as plDXPipeline::IResetDevice while the device is lost).
    if (s.reacquiring)
        return;
    s.reacquiring = true;
    s.ready = false;
    s.failed = false;
    // The pipeline still holds the lost device's objects; they are released before the handle
    // (FinishReacquire).
    s.oldDevice = s.device;
    s.device = nullptr;
    s.oldAdapter = s.adapter;
    s.adapter = nullptr;
    WGPURequestAdapterOptions opts = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    opts.compatibleSurface = s.surface;
    WGPURequestAdapterCallbackInfo cbi = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    cbi.mode = WGPUCallbackMode_AllowSpontaneous;
    cbi.callback = OnAdapter;
    wgpuInstanceRequestAdapter(s.instance, &opts, cbi);
    plWGPULog("device lost: requestAdapter issued again");
}

bool plWGPUEnumerate::FinishReacquire()
{
    if (!s.reacquiring || !s.ready)
        return false;
    s.reacquiring = false;
    s.lost = false;
    if (s.oldDevice)
        wgpuDeviceRelease(s.oldDevice);
    s.oldDevice = nullptr;
    if (s.oldAdapter)
        wgpuAdapterRelease(s.oldAdapter);
    s.oldAdapter = nullptr;
    return true;
}
unsigned long plWGPUEnumerate::ErrorCount() { return s.errors; }

bool plWGPUEnumerate::SpecialisedFixedPrograms() { return s.specialisedFixed; }

void plWGPUEnumerate::Enumerate(std::vector<hsG3DDeviceRecord>& records, hsDisplayHndl display)
{
    // plMetalEnumerate::Enumerate. Only a device that exists gets a record; the null
    // device's record is always there (plEmscriptenNullDevice.cpp).
    if (!s.ready || !s.device)
        return;

    hsG3DDeviceRecord devRec;
    devRec.SetG3DDeviceType(hsG3DDeviceSelector::kDevTypeWebGPU);
    devRec.SetDriverName(ST_LITERAL("WebGPU"));
    devRec.SetDeviceDesc(s.adapterDesc.empty() ? s.adapterVendor : s.adapterDesc);
    devRec.SetDriverDesc(ST_LITERAL("emdawnwebgpu"));
    devRec.SetDriverVersion(ST_LITERAL("1"));

    devRec.SetCap(hsG3DDeviceSelector::kCapsMipmap);
    devRec.SetCap(hsG3DDeviceSelector::kCapsPerspective);
    // D3DPTEXTURECAPS_CUBEMAP and D3DPTEXTURECAPS_MIPCUBEMAP (hsGDirect3DTnLEnumerate::ITryDirect3DTnLDevice):
    // cube textures and their mip chains are core WebGPU. plDXPipeline makes cube maps only with the first,
    // and mipmaps them only with the second (IMakeCubicTextureRef).
    devRec.SetCap(hsG3DDeviceSelector::kCapsCubicTextures);
    devRec.SetCap(hsG3DDeviceSelector::kCapsCubicMipmap);
    if (s.bc)
        devRec.SetCap(hsG3DDeviceSelector::kCapsCompressTextures);
    devRec.SetCap(hsG3DDeviceSelector::kCapsDoesSmallTextures);
    devRec.SetCap(hsG3DDeviceSelector::kCapsPixelShader);
    devRec.SetCap(hsG3DDeviceSelector::kCapsHardware);
    devRec.SetLayersAtOnce(8);
    // The card's MaxAnisotropy, 0 when it is <= 1 (hsGDirect3DTnLEnumerate::ITryDirect3DTnLDevice):
    // WebGPU samplers take maxAnisotropy up to 16 (plWGPUPipeline::GetMaxAnisotropicSamples). The
    // default settings read it (plClient::IDetectAudioVideoSettings).
    devRec.SetMaxAnisotropicSamples(16);

    // Every mode lists the multisample types 2..16 the device takes
    // (hsGDirect3DTnLEnumerate::IFindFSAATypes, ITryDirect3DTnLDevice): a WebGPU render attachment
    // takes a sample count of 1 or 4, so the list is {4}, on the fallback mode too. The default
    // settings read it (plClient::IDetectAudioVideoSettings), and both "closest <=" AA searches
    // search it (plWGPUPipeline::ResetDisplayDevice, plWGPUDevice::SetMSAASampleCount; in DX
    // plDXPipeline::ICreateDevice and ResetDisplayDevice).
    plDisplayHelper* displayHelper = plDisplayHelper::GetInstance();
    for (const auto& mode : displayHelper->GetSupportedDisplayModes(display)) {
        hsG3DDeviceMode& devMode = devRec.GetModes().emplace_back(mode.Width, mode.Height, mode.ColorDepth);
        devMode.AddFSAAType(4);
    }
    if (devRec.GetModes().empty()) {
        hsG3DDeviceMode& devMode = devRec.GetModes().emplace_back(
            hsG3DDeviceSelector::kDefaultWidth,
            hsG3DDeviceSelector::kDefaultHeight,
            hsG3DDeviceSelector::kDefaultDepth);
        devMode.AddFSAAType(4);
    }
    // The default mode is the page's canvas (the screen mode follows it).
    devRec.SetDefaultModeIndex(0);

    // The D3D9 client's default chipset tweaks (hsGDirect3DTnLEnumerate::IFudgeDirectXDevice, as
    // plGLEnumerate.cpp's fillDeviceRecord): LOD bias, Z bias and fog ratings, and
    // kCapsDoesSmallTextures cleared.
    devRec.SetFudgeFactors(hsG3DDeviceRecord::kDefaultChipset);
    records.emplace_back(devRec);
}

void plWGPUEnumerate::NoteCanvasTexture(WGPUTexture texture)
{
    const int serial = plWGPU_textureSerial(texture);
#ifdef PLASMA_TEST_HOOKS
    // The test hooks' frame and present-gap counters.
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
        observer->OnPresent(serial != s.lastTexSerial);
#endif
    if (serial == s.lastTexSerial)
        return;
    // The run's second canvas texture: the client's first frame has been presented. Until then the
    // page keeps its loading line over the black canvas; now it steps aside for the client's own
    // screens (the page's plasma.firstFrame). Once per run; the call does not wait for the page.
    if (!s.firstPresentReported && s.lastTexSerial > 0) {
        s.firstPresentReported = true;
        plWGPU_reportFirstPresent();
    }
    s.lastTexSerial = serial;
}

void plWGPUEnumerate::LogStats()
{
#ifdef PLASMA_TEST_HOOKS
    // The test hooks' STATS line; the product build prints none.
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
        observer->OnLoopReport();
#endif
}
