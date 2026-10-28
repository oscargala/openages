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

#include "plWGPUShader.h"

#include <map>
#include <string>

#include "HeadSpin.h"
#include "plSurface/plShader.h"
#include "plWGPULog.h"
#include "plWGPUPipeline.h"

//// The program table ////////////////////////////////////////////////////////////

static const plWGPUShaderProgram kPrograms[] = {
#include "ShaderSrc/ShaderPrograms.inc"
};
static_assert(std::size(kPrograms) == plWGPUShaderProgram::kNumPrograms, "ShaderPrograms.inc and kNumPrograms disagree");

// The family modules (CMakeLists.txt embeds each ShaderSrc/*.wgsl). Every module is the prelude, the D3D9
// device side of both program kinds, followed by the family's programs; the product and the harness build
// compile the same text.
static const char kPreludeWGSL[] =
#include "ShaderPrelude.wgsl.inc"
    ;
static const char kWaveSet7WGSL[] =
#include "WaveSet7.wgsl.inc"
    ;
static const char kCompCosinesWGSL[] =
#include "CompCosines.wgsl.inc"
    ;
static const char kBiasNormalsWGSL[] =
#include "BiasNormals.wgsl.inc"
    ;
static const char kGrassWGSL[] =
#include "Grass.wgsl.inc"
    ;
static const char kWaveDec1Lay_7WGSL[] =
#include "WaveDec1Lay_7.wgsl.inc"
    ;
static const char kWaveDecEnvWGSL[] =
#include "WaveDecEnv.wgsl.inc"
    ;
static const char kWaveRipWGSL[] =
#include "WaveRip.wgsl.inc"
    ;

const plWGPUShaderProgram* plWGPUShaderProgram::Find(plShaderID::ID id)
{
    for (const plWGPUShaderProgram& p : kPrograms) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

uint32_t plWGPUShaderProgram::Index() const
{
    return uint32_t(this - kPrograms);
}

const plWGPUShaderProgram* plWGPUShaderProgram::ByIndex(uint32_t index)
{
    return index < kNumPrograms ? &kPrograms[index] : nullptr;
}

const char* plWGPUShaderProgram::ModuleSource(const char* family)
{
    static const std::pair<const char*, const char*> kFamilies[] = {
        {"WaveSet7", kWaveSet7WGSL},
        {"CompCosines", kCompCosinesWGSL},
        {"BiasNormals", kBiasNormalsWGSL},
        {"Grass", kGrassWGSL},
        {"WaveDec1Lay_7", kWaveDec1Lay_7WGSL},
        {"WaveDecEnv", kWaveDecEnvWGSL},
        {"WaveRip", kWaveRipWGSL},
    };
    // Built once per family; the strings live as long as the program (the device caches its
    // modules by source pointer).
    static std::map<std::string, std::string> modules;
    auto                                      it = modules.find(family);
    if (it != modules.end())
        return it->second.c_str();
    for (const auto& f : kFamilies) {
        if (strcmp(f.first, family) == 0) {
            std::string& src = modules[family];
            src = kPreludeWGSL;
            src += "\n";
            src += f.second;
            return src.c_str();
        }
    }
    return nullptr;
}

//// plWGPUShader /////////////////////////////////////////////////////////////////

plWGPUShader::plWGPUShader(plShader* owner)
    : fOwner(owner),
      fPipe(),
      fProgram()
{
    // The plDXShader and plMetalShader constructors
    owner->SetDeviceRef(this);
}

plWGPUShader::~plWGPUShader()
{
    fPipe = nullptr;
    fErrorString.clear();
}

void plWGPUShader::SetOwner(plShader* owner)
{
    // plDXShader::SetOwner
    if (owner != fOwner) {
        Release();
        fOwner = owner;
        owner->SetDeviceRef(this);
    }
}

bool plWGPUShader::IOnError(ST::string errStr)
{
    // plDXShader::IOnError
    fErrorString = std::move(errStr);
    fOwner->Invalidate();
    hsStatusMessage(fErrorString.c_str());
    return false;
}

bool plWGPUShader::ICreate(plWGPUPipeline* pipe)
{
    // plDXVertexShader::ICreate, plDXPixelShader::ICreate: the program from the compiled-in
    // table (CreateVertexShader/CreatePixelShader of the bytecode); here the WGSL entry point
    // generated from that bytecode.
    fProgram = nullptr;
    fPipe = nullptr;
    fErrorString.clear();

    const plShaderDecl* decl = fOwner->GetDecl();
    if (!decl || !decl->GetCodes())
        return IOnError(ST_LITERAL("Shaders must be compiled into the engine."));

    fProgram = plWGPUShaderProgram::Find(decl->GetID());
    if (!fProgram) {
        // A program outside the ported set is not supported: the pipeline counts the request,
        // logs it once per program and skips the pass (plWGPUPipeline::INoteUnsupportedProgram).
        fErrorString = ST::format("{}: shader program {} ({}) is not supported",
                                  fOwner->GetKeyName(), int(decl->GetID()), decl->GetFileName());
        return false;
    }
    if (fProgram->pixel != fOwner->IsPixelShader()) {
        fProgram = nullptr;
        return IOnError(ST::format("{}: program {} is the wrong type", fOwner->GetKeyName(), decl->GetFileName()));
    }

    fPipe = pipe;
    return true;
}

const plWGPUShaderProgram* plWGPUShader::GetShader(plWGPUPipeline* pipe)
{
    // plDXVertexShader::GetShader
    if (!fProgram) {
        if (!ICreate(pipe))
            return nullptr;
    }

    if (!ISetConstants(pipe))
        return nullptr;

    return fProgram;
}
