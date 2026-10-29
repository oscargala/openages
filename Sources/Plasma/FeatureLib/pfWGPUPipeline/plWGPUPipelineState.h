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

#ifndef plWGPUPipelineState_h
#define plWGPUPipelineState_h

// The render pipeline states of the WebGPU pipeline, a port of
// plMetalPipelineState.h. A state names its shaders and fixed-function settings; the device
// turns it into a WGPURenderPipeline for the current attachment formats and caches it
// (plMetalDevice::PipelineState). Creation is synchronous (wgpuDeviceCreateRenderPipeline):
// Metal's asynchronous build and condition-variable wait (plMetalDevice::StartPipelineBuild,
// PipelineState) would never return on the single client thread.
//
// WebGPU bakes into the pipeline what Metal sets on the encoder (depth-stencil state, cull
// mode, primitive topology), so those are members of the state here and part of its
// equality and hash.

#include <webgpu/webgpu.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "plWGPUPipelineKey.h"

class plWGPUDevice;

enum plWGPUPipelineType
{
    // Unknown is for abstract types, don't use it
    Unknown = 0,
    MaterialShader,
    ShadowCaster,
    ShadowRender,
    Clear,
    Dynamic,
    Text
};

// The settings WebGPU takes at pipeline creation that Metal and D3D9 take as render state.
struct plWGPUFixedState
{
    WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
    WGPUFrontFace         frontFace = WGPUFrontFace_CCW;
    WGPUCullMode          cullMode = WGPUCullMode_None;
    WGPUCompareFunction   depthCompare = WGPUCompareFunction_Always;
    bool                  depthWrite = false;

    bool operator==(const plWGPUFixedState& o) const
    {
        return topology == o.topology && frontFace == o.frontFace && cullMode == o.cullMode &&
               depthCompare == o.depthCompare && depthWrite == o.depthWrite;
    }
    size_t Hash() const
    {
        return size_t(topology) ^ (size_t(frontFace) << 4) ^ (size_t(cullMode) << 8) ^
               (size_t(depthCompare) << 12) ^ (size_t(depthWrite) << 16);
    }
};

// The bind group layouts a state's shader uses (plWGPUDevice owns them).
enum class plWGPUBindLayout
{
    kUniformsOnly,       // group 0: one uniform buffer, dynamic offset
    kUniformsAndTexture, // group 0 as above; group 1: texture_2d<f32> + filtering sampler
    kFixedFunction,      // group 0 as above; group 1: 8 texture_2d, 8 texture_cube, 8 samplers
    kShader              // kFixedFunction's groups 0 and 1, in a layout object of its own
};

// MARK: Base pipeline state

class plWGPUPipelineState
{
public:
    plWGPUPipelineState(plWGPUDevice* device);
    virtual ~plWGPUPipelineState() = default;

    WGPURenderPipeline GetRenderPipelineState();
    bool               operator==(const plWGPUPipelineState& p) const
    {
        if (p.GetID() != GetID()) {
            return false;
        }
        return fFixed == p.fFixed && IsEqual(p);
    }
    virtual size_t               GetHash() const;
    virtual bool                 IsEqual(const plWGPUPipelineState& p) const = 0;
    virtual uint16_t             GetID() const { return plWGPUPipelineType::Unknown; }
    virtual plWGPUPipelineState* Clone() = 0;

    // The WGSL source (ShaderSrc/*.wgsl, embedded at build time) and its entry points.
    virtual const char*      GetShaderSource() const = 0;
    virtual const char*      GetVertexFunction() const = 0;
    virtual const char*      GetFragmentFunction() const = 0;
    virtual const char*      GetDescription() const = 0;
    virtual plWGPUBindLayout GetBindLayout() const = 0;

    // Returns false for no blending (the colour target's blend stays null).
    virtual bool               ConfigureBlend(WGPUBlendState& blend) const = 0;
    virtual WGPUColorWriteMask GetWriteMask() const { return WGPUColorWriteMask_All; }
    virtual void               ConfigureVertexDescriptor(std::vector<WGPUVertexBufferLayout>& layouts,
                                                         std::vector<WGPUVertexAttribute>&    attributes) const = 0;
    // The values of the WGSL pipeline-overridable constants each entry point uses.
    virtual void               ConfigureConstants(WGPUShaderStage stage, std::vector<WGPUConstantEntry>& constants) const {}

    const plWGPUFixedState& GetFixedState() const { return fFixed; }

protected:
    plWGPUDevice*    fDevice;
    plWGPUFixedState fFixed;
};

// MARK: Clear (plMetalPipelineState.h, plMetalClearPipelineState)

class plWGPUClearPipelineState : public plWGPUPipelineState
{
public:
    plWGPUClearPipelineState(plWGPUDevice* device, bool shouldClearColor, bool shouldClearDepth);

    bool IsEqual(const plWGPUPipelineState& p) const override
    {
        const plWGPUClearPipelineState* clearState = static_cast<const plWGPUClearPipelineState*>(&p);
        return clearState->fShouldClearDepth == fShouldClearDepth && clearState->fShouldClearColor == fShouldClearColor;
    }
    size_t GetHash() const override;

    uint16_t             GetID() const override { return plWGPUPipelineType::Clear; }
    plWGPUPipelineState* Clone() override { return new plWGPUClearPipelineState(*this); }

    const char*      GetShaderSource() const override;
    const char*      GetVertexFunction() const override { return "clearVertex"; }
    const char*      GetFragmentFunction() const override;
    const char*      GetDescription() const override { return "Clear"; }
    plWGPUBindLayout GetBindLayout() const override { return plWGPUBindLayout::kUniformsOnly; }

    bool               ConfigureBlend(WGPUBlendState& blend) const override { return false; }
    WGPUColorWriteMask GetWriteMask() const override
    {
        return fShouldClearColor ? WGPUColorWriteMask_All : WGPUColorWriteMask_None;
    }
    void ConfigureVertexDescriptor(std::vector<WGPUVertexBufferLayout>& layouts,
                                   std::vector<WGPUVertexAttribute>&    attributes) const override
    {
    }

private:
    bool fShouldClearDepth;
    bool fShouldClearColor;
};

// MARK: Fixed function (plMetalPipelineState.h plMetalMaterialPassPipelineState)

// The pipeline of a fixed-function pass (FixedPipelineShaders.wgsl), by plWGPUFixedFunctionKey and fragment program:
// program 0 is the uber shader, any other the device's specialised fragment program of that id (plWGPUFixedFragment,
// plWGPUDevice::IFixedProgram), whose module text is source. The device keeps a program's text for its whole life and
// never reuses an id, so the text's address names the module (plWGPUDevice::ShaderModule).
class plWGPUFixedFunctionPipelineState : public plWGPUPipelineState
{
public:
    plWGPUFixedFunctionPipelineState(plWGPUDevice* device, const plWGPUFixedFunctionKey& key, const plWGPUFixedState& fixed,
                                     uint32_t program = 0, const char* source = nullptr);

    bool IsEqual(const plWGPUPipelineState& p) const override
    {
        const plWGPUFixedFunctionPipelineState* o = static_cast<const plWGPUFixedFunctionPipelineState*>(&p);
        return o->fKey.Pack() == fKey.Pack() && o->fProgram == fProgram;
    }
    size_t GetHash() const override;

    uint16_t                      GetID() const override { return plWGPUPipelineType::MaterialShader; }
    plWGPUPipelineState*          Clone() override { return new plWGPUFixedFunctionPipelineState(*this); }
    const plWGPUFixedFunctionKey& GetKey() const { return fKey; }
    uint32_t                      GetProgram() const { return fProgram; }

    /** The uber shader's WGSL, FixedPipelineShaders.wgsl as embedded: the source the specialised programs are cut from. */
    static const char* UberSource();

    const char*      GetShaderSource() const override;
    const char*      GetVertexFunction() const override { return "fixedVertex"; }
    const char*      GetFragmentFunction() const override { return "fixedFragment"; }
    const char*      GetDescription() const override { return "Fixed function pass"; }
    plWGPUBindLayout GetBindLayout() const override { return plWGPUBindLayout::kFixedFunction; }

    bool               ConfigureBlend(WGPUBlendState& blend) const override;
    WGPUColorWriteMask GetWriteMask() const override;
    void               ConfigureVertexDescriptor(std::vector<WGPUVertexBufferLayout>& layouts,
                                                 std::vector<WGPUVertexAttribute>&    attributes) const override;
    void               ConfigureConstants(WGPUShaderStage stage, std::vector<WGPUConstantEntry>& constants) const override;

    /** The byte offsets of one vertex of the given buffer format (D3D FVF order). */
    struct VertexLayout
    {
        uint32_t stride;
        int32_t  weight; // -1: absent
        int32_t  normal;
        int32_t  diffuse;
        int32_t  specular;
        int32_t  uvw; // first channel
        uint32_t numUVWs;
    };
    static VertexLayout Layout(uint8_t vertexFormat, uint8_t vertexKind);

private:
    plWGPUFixedFunctionKey fKey;
    uint32_t               fProgram; // 0: the uber shader
    const char*            fSource;  // the program's module text (fProgram != 0)
};

// MARK: Shader programs (plMetalPipelineState.h, plMetalDynamicMaterialPipelineState)

struct plWGPUShaderProgram;

// The pipeline of a pass with D3D9 shader programs, by plWGPUShaderKey.
class plWGPUShaderPipelineState : public plWGPUPipelineState
{
public:
    plWGPUShaderPipelineState(plWGPUDevice* device, const plWGPUShaderKey& key, const plWGPUFixedState& fixed,
                              const plWGPUShaderProgram* vs, const plWGPUShaderProgram* ps);
    bool IsEqual(const plWGPUPipelineState& p) const override
    {
        const plWGPUShaderPipelineState* o = static_cast<const plWGPUShaderPipelineState*>(&p);
        return o->fKey.Pack() == fKey.Pack();
    }
    size_t                 GetHash() const override;
    uint16_t               GetID() const override { return plWGPUPipelineType::Dynamic; }
    plWGPUPipelineState*   Clone() override { return new plWGPUShaderPipelineState(*this); }
    const plWGPUShaderKey& GetKey() const { return fKey; }
    const char*            GetShaderSource() const override;
    const char*            GetVertexFunction() const override;
    const char*            GetFragmentFunction() const override;
    const char*            GetDescription() const override { return GetFragmentFunction(); }
    plWGPUBindLayout       GetBindLayout() const override { return plWGPUBindLayout::kShader; }
    bool                   ConfigureBlend(WGPUBlendState& blend) const override;
    WGPUColorWriteMask     GetWriteMask() const override;
    void                   ConfigureVertexDescriptor(std::vector<WGPUVertexBufferLayout>& layouts,
                                                     std::vector<WGPUVertexAttribute>&    attributes) const override;
    void                   ConfigureConstants(WGPUShaderStage stage, std::vector<WGPUConstantEntry>& constants) const override;

private:
    plWGPUShaderKey            fKey;
    const plWGPUShaderProgram* fVS;
    const plWGPUShaderProgram* fPS;
};

#endif /* plWGPUPipelineState_h */
