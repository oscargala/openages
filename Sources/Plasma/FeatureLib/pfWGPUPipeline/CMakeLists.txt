# /*==LICENSE==*
#
# CyanWorlds.com Engine - MMOG client, server and tools
# Copyright (C) 2011  Cyan Worlds, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Additional permissions under GNU GPL version 3 section 7
#
# If you modify this Program, or any covered work, by linking or
# combining it with any of RAD Game Tools Bink SDK, Autodesk 3ds Max SDK,
# NVIDIA PhysX SDK, Microsoft DirectX SDK, OpenSSL library, Independent
# JPEG Group JPEG library, Microsoft Windows Media SDK, or Apple QuickTime SDK
# (or a modified version of those libraries),
# containing parts covered by the terms of the Bink SDK EULA, 3ds Max EULA,
# PhysX SDK EULA, DirectX SDK EULA, OpenSSL and SSLeay licenses, IJG
# JPEG Library README, Windows Media SDK EULA, or QuickTime SDK EULA, the
# licensors of this Program grant you additional
# permission to convey the resulting work. Corresponding Source for a
# non-source form of such a combination shall include the source code for
# the parts of OpenSSL and IJG JPEG Library used as well as that of the covered
# work.
#
# You can contact Cyan Worlds, Inc. by email legal@cyan.com
#  or by snail mail at:
#       Cyan Worlds, Inc.
#       14617 N Newport Hwy
#       Mead, WA   99021
#
# *==LICENSE==*/

# pfWGPUPipeline, the browser's WebGPU pipeline (a port of pfMetalPipeline).
# Built only for Emscripten (PLASMA_PIPELINE_WGPU, root CMakeLists.txt).

set(pfWGPUPipeline_SOURCES
    plWGPUD3DDevice.cpp
    plWGPUDevice.cpp
    plWGPUDeviceRefs.cpp
    plWGPUEnumerate.cpp
    plWGPUFragmentShader.cpp
    plWGPUPipeline.cpp
    plWGPUPipelineAvatar.cpp
    plWGPUPipelineMaterial.cpp
    plWGPUPipelineShadows.cpp
    plWGPUPipelineState.cpp
    plWGPUPlateManager.cpp
    plWGPUShader.cpp
    plWGPUTextFont.cpp
    plWGPUTextureFormat.cpp
    plWGPUTexturePack.cpp
    plWGPUVertexShader.cpp
)

set(pfWGPUPipeline_HEADERS
    plWGPUD3DDevice.h
    plWGPUDevice.h
    plWGPUDeviceRef.h
    plWGPUFragmentShader.h
    plWGPULightRef.h
    plWGPULog.h
    plWGPUPipeline.h
    plWGPUPipeline_Private.h
    plWGPUPipelineKey.h
    plWGPUPipelineObserver.h
    plWGPUPipelineState.h
    plWGPUPlateManager.h
    plWGPUSettings.h
    plWGPUShader.h
    plWGPUTextFont.h
    plWGPUTextureFormat.h
    plWGPUTexturePack.h
    plWGPUVertexShader.h
    ShaderSrc/ShaderPrograms.inc
    ShaderSrc/ShaderTypes.h
    pfWGPUPipelineCreatable.h
)

set(pfWGPUPipeline_SHADERS
    Clear.wgsl
    GammaCorrection.wgsl
    FixedPipelineShaders.wgsl
    TextFontShader.wgsl
    ShaderPrelude.wgsl
    WaveSet7.wgsl
    CompCosines.wgsl
    BiasNormals.wgsl
    Grass.wgsl
    WaveDec1Lay_7.wgsl
    WaveDecEnv.wgsl
    WaveRip.wgsl
)
# The WGSL sources are embedded as raw string literals (ShaderSrc/<name>.wgsl ->
# <build>/ShaderSrc/<name>.wgsl.inc), the role of Metal's compiled shader library.
set(pfWGPUPipeline_SHADER_INCS)
foreach(shader ${pfWGPUPipeline_SHADERS})
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/ShaderSrc/${shader}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/ShaderSrc/${shader}.inc")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${input} -DOUTPUT=${output} -P "${CMAKE_CURRENT_SOURCE_DIR}/EmbedWGSL.cmake"
        DEPENDS "${input}" "${CMAKE_CURRENT_SOURCE_DIR}/EmbedWGSL.cmake"
        COMMENT "Embedding WGSL ${shader}"
        VERBATIM
    )
    list(APPEND pfWGPUPipeline_SHADER_INCS "${output}")
endforeach()

plasma_library(pfWGPUPipeline
    FOLDER FeatureLib
    SOURCES ${pfWGPUPipeline_SOURCES} ${pfWGPUPipeline_HEADERS} ${pfWGPUPipeline_SHADER_INCS}
)

target_link_libraries(pfWGPUPipeline
    PUBLIC
        CoreLib
        pnNucleusInc
        plPipeline
    PRIVATE
        pnEncryption
        plResMgr
        plStatusLog
    INTERFACE
        pnFactory
)

target_include_directories(pfWGPUPipeline PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ShaderSrc")
# webgpu/webgpu.h of the emdawnwebgpu port (the client links the port, Apps/plClient/CMakeLists.txt)
target_compile_options(pfWGPUPipeline PUBLIC --use-port=emdawnwebgpu:cpp_bindings=false)
target_link_options(pfWGPUPipeline INTERFACE "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/plWGPUPipeline.js")

source_group("Source Files" FILES ${pfWGPUPipeline_SOURCES})
source_group("Header Files" FILES ${pfWGPUPipeline_HEADERS})
source_group("Shaders" FILES ${pfWGPUPipeline_SHADER_INCS})
