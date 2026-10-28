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

#include "plWGPUTextFont.h"

#include <cstring>
#include <string_theory/format>

#include "HeadSpin.h"
#include "ShaderSrc/ShaderTypes.h"
#include "plWGPUPipeline.h"

static const char kTextFontWGSL[] =
#include "TextFontShader.wgsl.inc"
    ;

// Following number needs to be at least: 64 chars max in plTextFont drawn at any one time
//                                      * 4 primitives per char max (for bold text)
//                                      * 3 verts per primitive
constexpr uint32_t kNumVertsInBuffer = 4608;

std::vector<plTextFont::plFontVertex> plWGPUTextFont::fBuffer;
uint32_t                              plWGPUTextFont::fBufferCursor = 0;

static_assert(sizeof(hsPoint3) == 12, "plFontVertex is the 28-byte D3DFVF_XYZ|DIFFUSE|TEX1|TEXCOORDSIZE3 vertex");

//// Constructor & Destructor /////////////////////////////////////////////////

plWGPUTextFont::plWGPUTextFont(plPipeline* pipe, plWGPUDevice* device)
    : plTextFont(pipe),
      fTexture(),
      fTextureView(),
      fDevice(device),
      fPipeline((plWGPUPipeline*)pipe)
{
    if (fBuffer.empty())
        fBuffer.resize(kNumVertsInBuffer);
}

plWGPUTextFont::~plWGPUTextFont()
{
    DestroyObjects();
}

//// ICreateTexture ///////////////////////////////////////////////////////////

void plWGPUTextFont::ICreateTexture(uint16_t* data)
{
    // D3DFMT_A4R4G4B4 (plDXTextFont::ICreateTexture), which WebGPU lacks: each 4-bit channel n
    // samples as n/15, stored here as n*17 in rgba8unorm. (Metal multiplies by 255 into a
    // uint8_t, plMetalTextFont::ICreateTexture, which keeps only 0 or 255 for odd nibbles.)
    if (fTextureView)
        wgpuTextureViewRelease(fTextureView);
    if (fTexture)
        wgpuTextureRelease(fTexture);

    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = WGPUStringView{"Font texture", WGPU_STRLEN};
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.size = {fTextureWidth, fTextureHeight, 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    fTexture = wgpuDeviceCreateTexture(fDevice->fWGPUDevice, &desc);

    std::vector<uint8_t> outData(size_t(fTextureWidth) * fTextureHeight * 4);
    for (size_t i = 0; i < size_t(fTextureWidth) * fTextureHeight; i++) {
        const uint16_t v = data[i];
        outData[i * 4 + 0] = uint8_t(((v >> 8) & 0xf) * 17);  // R
        outData[i * 4 + 1] = uint8_t(((v >> 4) & 0xf) * 17);  // G
        outData[i * 4 + 2] = uint8_t((v & 0xf) * 17);         // B
        outData[i * 4 + 3] = uint8_t(((v >> 12) & 0xf) * 17); // A
    }

    WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    dst.texture = fTexture;
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = fTextureWidth * 4;
    layout.rowsPerImage = fTextureHeight;
    WGPUExtent3D size = {fTextureWidth, fTextureHeight, 1};
    wgpuQueueWriteTexture(fDevice->fQueue, &dst, outData.data(), outData.size(), &layout, &size);
#ifdef PLASMA_TEST_HOOKS
    fDevice->NoteUpload(plWGPUDevice::kUploadOther, outData.size(), ST::format("text font {}", fFace), "create");
#endif

    fTextureView = wgpuTextureCreateView(fTexture, nullptr);
}

//// IInitStateBlocks /////////////////////////////////////////////////////////

void plWGPUTextFont::IInitStateBlocks()
{
    // The state block (plDXTextFont::IInitStateBlocks) is the pipeline state of the font draws.
}

//// DestroyObjects ///////////////////////////////////////////////////////////

void plWGPUTextFont::DestroyObjects()
{
    if (fTextureView)
        wgpuTextureViewRelease(fTextureView);
    if (fTexture)
        wgpuTextureRelease(fTexture);
    fTextureView = nullptr;
    fTexture = nullptr;
    fInitialized = false;
}

WGPURenderPassEncoder plWGPUTextFont::IBindState(WGPUPrimitiveTopology topology)
{
    WGPURenderPassEncoder pass = fDevice->CurrentRenderCommandEncoder();
    if (!pass || !fTextureView)
        return nullptr;

    plWGPUTextFontPipelineState state(fDevice, topology);
    fDevice->SetPipeline(pass, state);

    // D3DTS_PROJECTION of plDXTextFont::SaveStates: the vertices range over
    // (0..screenWidth, 0..screenHeight).
    plWGPUTextUniforms uniforms = {};
    uniforms.transform[0] = 2.0f / (float)fPipe->Width();
    uniforms.transform[5] = -2.0f / (float)fPipe->Height();
    uniforms.transform[10] = 1.0f;
    uniforms.transform[12] = -1.0f;
    uniforms.transform[13] = 1.0f;
    uniforms.transform[15] = 1.0f;
    if (!fDevice->BindUniforms(pass, &uniforms, sizeof(uniforms)))
        return nullptr;

    // D3DSAMP_MINFILTER/MAGFILTER POINT, MIPFILTER NONE
    fDevice->BindTexture(pass, fTextureView, fDevice->GetPointSampler());
    return pass;
}

//// IDrawPrimitive ///////////////////////////////////////////////////////////

void plWGPUTextFont::IDrawPrimitive(uint32_t count, plFontVertex* array)
{
    // plDXTextFont::IDrawPrimitive
    plFontVertex* v;

    if (fBufferCursor && (fBufferCursor + count * 3 < kNumVertsInBuffer)) {
        // We can append to it
        v = fBuffer.data() + fBufferCursor;
        fBufferCursor += count * 3;
    } else {
        // Gotta start over
        FlushDraws();

        if (count * 3 > kNumVertsInBuffer) {
            // D3D9's discard lock of more than the buffer holds fails and draws nothing.
            hsAssert(false, "Failed to lock vertex buffer for writing");
            return;
        }
        fBufferCursor = count * 3;
        v = fBuffer.data();
    }

    if (v != nullptr && array != nullptr) {
        memcpy(v, array, count * sizeof(plFontVertex) * 3);
    }
}

//// IDrawLines ///////////////////////////////////////////////////////////////

void plWGPUTextFont::IDrawLines(uint32_t count, plFontVertex* array)
{
    // plDXTextFont::IDrawLines: DrawPrimitiveUP of a line list, outside the batch.
    if (count == 0 || array == nullptr)
        return;

    WGPURenderPassEncoder pass = IBindState(WGPUPrimitiveTopology_LineList);
    if (!pass)
        return;
    if (!fDevice->BindVertexBytes(pass, 0, array, count * 2 * sizeof(plFontVertex)))
        return;
    wgpuRenderPassEncoderDraw(pass, count * 2, 1, 0, 0);
}

//// FlushDraws ///////////////////////////////////////////////////////////////
//  Flushes out and finishes any drawing left to be done.

void plWGPUTextFont::FlushDraws()
{
    // plDXTextFont::FlushDraws
    if (fBufferCursor > 0) {
        WGPURenderPassEncoder pass = IBindState(WGPUPrimitiveTopology_TriangleList);
        if (pass && fDevice->BindVertexBytes(pass, 0, fBuffer.data(), fBufferCursor * sizeof(plFontVertex))) {
            wgpuRenderPassEncoderDraw(pass, fBufferCursor, 1, 0, 0);
        }
        fBufferCursor = 0;
    }
}

//// SaveStates ///////////////////////////////////////////////////////////////

void plWGPUTextFont::SaveStates()
{
    // plDXTextFont::SaveStates. The text state and transform are bound per draw (IBindState);
    // nothing of the scene's state needs saving, each draw sets its own pipeline.
    if (!fInitialized)
        IInitObjects();
}

//// RestoreStates ////////////////////////////////////////////////////////////

void plWGPUTextFont::RestoreStates()
{
}

// MARK: Text pipeline state

plWGPUTextFontPipelineState::plWGPUTextFontPipelineState(plWGPUDevice* device, WGPUPrimitiveTopology topology)
    : plWGPUPipelineState(device)
{
    // plDXTextFont::IInitStateBlocks: CULLMODE CCW, ZENABLE with ZFUNC ALWAYS and ZWRITEENABLE.
    // D3D9 culls counter-clockwise triangles; plTextFont's are clockwise on screen.
    fFixed.topology = topology;
    fFixed.frontFace = WGPUFrontFace_CCW;
    fFixed.cullMode = WGPUCullMode_Front;
    fFixed.depthCompare = WGPUCompareFunction_Always;
    fFixed.depthWrite = true;
}

const char* plWGPUTextFontPipelineState::GetShaderSource() const
{
    return kTextFontWGSL;
}

bool plWGPUTextFontPipelineState::ConfigureBlend(WGPUBlendState& blend) const
{
    // ALPHABLENDENABLE, SRCBLEND SRCALPHA, DESTBLEND INVSRCALPHA (same factors for alpha)
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha = blend.color;
    return true;
}

void plWGPUTextFontPipelineState::ConfigureVertexDescriptor(std::vector<WGPUVertexBufferLayout>& layouts,
                                                            std::vector<WGPUVertexAttribute>&    attributes) const
{
    // plFontVertex: D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0)
    attributes.resize(3);
    attributes[0] = WGPU_VERTEX_ATTRIBUTE_INIT;
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1] = WGPU_VERTEX_ATTRIBUTE_INIT;
    attributes[1].format = WGPUVertexFormat_Unorm8x4;
    attributes[1].offset = 12;
    attributes[1].shaderLocation = 1;
    attributes[2] = WGPU_VERTEX_ATTRIBUTE_INIT;
    attributes[2].format = WGPUVertexFormat_Float32x3;
    attributes[2].offset = 16;
    attributes[2].shaderLocation = 2;

    WGPUVertexBufferLayout layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.arrayStride = 28;
    layout.attributeCount = attributes.size();
    layout.attributes = attributes.data();
    layouts.push_back(layout);
}
