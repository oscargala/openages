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

# pfWGPUPipeline is built only for Emscripten. Its units that hold no WebGPU type (the D3D9 device
# state, the pipeline keys, the D3D9 texture levels and formats, the fixed-function fragment programs)
# are built here from their sources, so that they are tested natively.
set(pfWGPUPipelineTest_SOURCES
    test_plWGPUD3DDevice.cpp
    test_plWGPUFixedFragment.cpp
    test_plWGPUPipelineKey.cpp
    test_plWGPUTextureFormat.cpp
    ${PLASMA_SOURCE_ROOT}/FeatureLib/pfWGPUPipeline/plWGPUD3DDevice.cpp
    ${PLASMA_SOURCE_ROOT}/FeatureLib/pfWGPUPipeline/plWGPUFixedFragment.cpp
    ${PLASMA_SOURCE_ROOT}/FeatureLib/pfWGPUPipeline/plWGPUTextureFormat.cpp
)

# The fixed-function shader the client embeds, which the fragment programs' test binds; the test includes the image
# check's record of the programs it compared (fixed_fragment_programs.inc: the traced draw states and SHA-256 digests,
# OpenSSL's).
plasma_test(test_pfWGPUPipeline SOURCES ${pfWGPUPipelineTest_SOURCES})
target_compile_definitions(
    test_pfWGPUPipeline
    PRIVATE
        PLASMA_FIXED_PIPELINE_WGSL="${PLASMA_SOURCE_ROOT}/FeatureLib/pfWGPUPipeline/ShaderSrc/FixedPipelineShaders.wgsl"
)
target_link_libraries(
    test_pfWGPUPipeline
    PRIVATE
        CoreLib
        pnNucleusInc
        plGImage
        plPubUtilInc
        pfAnimation
        pfAudio
        pfCamera
        pfConditional
        pfMessage
        OpenSSL::Crypto
        gtest_main
)
