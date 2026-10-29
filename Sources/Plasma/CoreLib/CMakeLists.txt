set(CoreLib_SOURCES
    hsBitVector.cpp
    hsBounds.cpp
    hsCpuID.cpp
    hsDebug.cpp
    hsEndian.cpp
    hsExceptions.cpp
    hsExceptionStack.cpp
    hsFastMath.cpp
    hsFILELock.cpp
    hsGeometry3.cpp
    hsMatrix33.cpp
    hsMatrix44.cpp
    hsQuat.cpp
    hsRefCnt.cpp
    hsStream.cpp
    hsSystemInfo.cpp
    hsThread.cpp
    pcSmallRect.cpp
    plCmdParser.cpp
    plFileSystem.cpp
    plGeneric.cpp
    plLoadMask.cpp
    plProduct.cpp
    plViewTransform.cpp
    $<$<PLATFORM_ID:Windows>:hsWindows.cpp>
)

if(CMAKE_USE_WIN32_THREADS_INIT)
    list(APPEND CoreLib_SOURCES hsThread_Win.cpp)
elseif(CMAKE_USE_PTHREADS_INIT)
    list(APPEND CoreLib_SOURCES hsThread_Unix.cpp)
else()
    message(FATAL_ERROR "The thread library CMake provided for your platform is not supported.")
endif()

set(CoreLib_HEADERS
    HeadSpin.h
    hsAlignedAllocator.hpp
    hsBitCast.h
    hsBitVector.h
    hsBounds.h
    hsColorRGBA.h
    hsCpuID.h
    hsDarwin.h
    hsDebug.h
    hsEndian.h
    hsExceptions.h
    hsExceptionStack.h
    hsFastMath.h
    hsFILELock.h
    hsGeometry3.h
    hsLockGuard.h
    hsMain.inl
    hsMath.h
    hsMatrix44.h
    hsMatrixMath.h
    hsOptionalCall.h
    hsPoint2.h
    hsPoolVector.h
    hsQuat.h
    hsRefCnt.h
    hsSIMD.h
    hsStream.h
    hsStringTokenizer.h
    hsSystemInfo.h
    hsThread.h
    hsWindows.h
    pcSmallRect.h
    plCmdParser.h
    plFileSystem.h
    plGeneric.h
    plLoadMask.h
    plProduct.h
    plQuality.h
    plRenderLevel.h
    plTweak.h
    plViewTransform.h
)

plasma_library(CoreLib
    FOLDER CoreLib
    SOURCES ${CoreLib_SOURCES} ${CoreLib_HEADERS}
    PRECOMPILED_HEADERS _CoreLibPch.h
)
plasma_target_simd_sources(CoreLib SSE3 hsMatrix44_SSE3.cpp)
# A release build embeds its commit in plProduct.cpp, the one file that includes hsBuildInfo.inl. As a ThinLTO module
# the commit would enter that module's hash, and with it the names ThinLTO gives the functions it promotes, and the link
# order, so two commits would differ far beyond the commit string. Compiled without LTO (and so without the PCH, which
# was built for LTO, and outside a unity source), plProduct.cpp is a plain object, and its commit and tag are arrays
# outside the linker's string pool (cmake/hsBuildInfo.inl.cmake): the commit changes only their bytes.
if(PRODUCT_RELEASE_BUILD AND CMAKE_INTERPROCEDURAL_OPTIMIZATION AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(plProduct.cpp PROPERTIES
        COMPILE_OPTIONS -fno-lto
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
    )
endif()
target_link_libraries(
    CoreLib
    PUBLIC
        string_theory
        Threads::Threads
        $<$<AND:$<CONFIG:Debug>,$<BOOL:${USE_VLD}>>:VLD::VLD>
        "$<$<PLATFORM_ID:Darwin>:-framework Accelerate>"
        "$<$<PLATFORM_ID:Darwin>:-framework CoreFoundation>"
)
target_include_directories(
    CoreLib
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR} # For hsBuildInfo.inl
)

source_group("Source Files" FILES ${CoreLib_SOURCES})
source_group("Header Files" FILES ${CoreLib_HEADERS})

set(BUILD_INFO_FILE "${CMAKE_CURRENT_BINARY_DIR}/hsBuildInfo.inl")
set(BUILD_INFO_COMMAND ${CMAKE_COMMAND}
    -DPRODUCT_BRANCH_ID=${PRODUCT_BRANCH_ID}
    -DPRODUCT_BUILD_ID=${PRODUCT_BUILD_ID}
    -DPRODUCT_BUILD_TYPE=${PRODUCT_BUILD_TYPE}
    -DPRODUCT_CORE_NAME=${PRODUCT_CORE_NAME}
    -DPRODUCT_SHORT_NAME=${PRODUCT_SHORT_NAME}
    -DPRODUCT_LONG_NAME=${PRODUCT_LONG_NAME}
    -DPRODUCT_UUID=${PRODUCT_UUID}
    -DPRODUCT_EMBED_BUILD_INFO=${PRODUCT_EMBED_BUILD_INFO}
    -DPRODUCT_EMBED_BUILD_TIME=${PRODUCT_EMBED_BUILD_TIME}
    -DPRODUCT_RELEASE_BUILD=${PRODUCT_RELEASE_BUILD}
    "-DPRODUCT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake"
)
add_custom_target(pcBuildInfo
    COMMAND ${BUILD_INFO_COMMAND}
    BYPRODUCTS "${BUILD_INFO_FILE}"
)
set_target_properties(pcBuildInfo PROPERTIES FOLDER CoreLib)
add_dependencies(CoreLib pcBuildInfo)
