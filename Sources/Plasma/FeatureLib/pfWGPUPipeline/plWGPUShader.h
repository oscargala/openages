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

#ifndef plWGPUShader_inc
#define plWGPUShader_inc

// The shader device refs, a port of plMetalShader.h (structure) with the DX behaviour of
// plDXShader/plDXVertexShader/plDXPixelShader:
// the ref is created on first use, linked into the pipeline's fVShaderRefList/fPShaderRefList and
// released by IReleaseShaders; GetShader creates the program on first use and uploads the
// constant block 0..NumConsts-1 on every call (plDXVertexShader::GetShader, ISetConstants and
// their plDXPixelShader counterparts).

#include <cstdint>
#include <string_theory/string>

#include "plSurface/plShaderTable.h"
#include "plWGPUDeviceRef.h"

class plShader;
class plWGPUPipeline;

/** A program the browser carries: its WGSL module (family) and entry point, generated from the
    plSurface/<name>.h bytecode (ShaderSrc/ShaderPrograms.inc, ShaderSrc/gen/d3d9_to_wgsl.py). */
struct plWGPUShaderProgram
{
    plShaderID::ID id;
    const char*    family; // module name, pfMetalPipeline/ShaderSrc/<family>.metal
    const char*    entry;  // WGSL entry point (the plShaderTable name)
    bool           pixel;
    bool           writesFog; // the vertex program writes oFog
    uint8_t        samples;   // the pixel program: stages its tex/texm3x3vspec sample (bits)
    uint32_t       tokens;    // DWORDs in the bytecode

    /** The program for a plShaderTable ID, or null for a program this pipeline does not support
        (the caller logs, counts and skips the pass). */
    static const plWGPUShaderProgram* Find(plShaderID::ID id);
    /** Index in the program table (0..kNumPrograms-1), for counters and pipeline keys. */
    uint32_t                          Index() const;
    static const plWGPUShaderProgram* ByIndex(uint32_t index);
    /** The WGSL module source of a family: ShaderPrelude.wgsl (the device side of both program
        kinds), then the family's programs. */
    static const char*                ModuleSource(const char* family);
    static constexpr uint32_t         kNumPrograms = 14;
};
// A pipeline key keeps each program index in 4 bits (plWGPUShaderKey::Pack).
static_assert(plWGPUShaderProgram::kNumPrograms <= 16, "plWGPUShaderKey::Pack keeps a program index in 4 bits");

class plWGPUShader : public plWGPUDeviceRef
{
protected:
    plShader*                  fOwner;
    plWGPUPipeline*            fPipe;
    const plWGPUShaderProgram* fProgram;
    ST::string                 fErrorString;

    bool         ICreate(plWGPUPipeline* pipe);
    bool         IOnError(ST::string errStr);
    virtual bool ISetConstants(plWGPUPipeline* pipe) = 0;

public:
    plWGPUShader(plShader* owner);
    virtual ~plWGPUShader();

    void       SetOwner(plShader* owner);
    ST::string GetErrorString() const { return fErrorString; }

    /** plDXVertexShader::GetShader / plDXPixelShader::GetShader: the program, created on first
        use, with the constants uploaded; null on error (unsupported programs included). */
    const plWGPUShaderProgram* GetShader(plWGPUPipeline* pipe);
};

#endif // plWGPUShader_inc
