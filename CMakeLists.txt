cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE STRING "")
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum OS X deployment version")

# CMake 3.22 changes cmake_dependent_option from doing `if(${CONDITION})` to actually evaulating
# The cmake code `if(<condition>)` using cmake_language(EVAL). That's a breaking change that warns
# if we don't explicitly set the policy. We prefer the OLD policy to avoid surprises. The NEW policy
# would let us use parens, eg `[[FOO AND (BAR OR MEOW)]]`.
if(POLICY CMP0127)
    cmake_policy(SET CMP0127 OLD)
endif()

# When cross-compiling, we want CMake to find the host system's Python interpreter
if(POLICY CMP0190)
    cmake_policy(SET CMP0190 NEW)
endif()

# Deduplicate libraries on linker commands
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
    if(POLICY CMP0179)
        cmake_policy(SET CMP0179 NEW)
    endif()
endif()

include(CheckLanguage)
include(CMakeDependentOption)
include(Dependencies)
include(FeatureSummary)
include(MetalShaderSupport)
include(VcpkgToolchain)

project(Plasma)

# Set up Product Identification parameters
set(PRODUCT_BRANCH_ID   "1"         CACHE STRING "Branch ID")
set(PRODUCT_BUILD_ID    "918"       CACHE STRING "Build ID")
set(PRODUCT_BUILD_TYPE  "50"        CACHE STRING "Build Type")
set(PRODUCT_CORE_NAME   "UruLive"   CACHE STRING "Product Core Name")
set(PRODUCT_SHORT_NAME  "UruLive"   CACHE STRING "Product Short Name")
set(PRODUCT_LONG_NAME   "Uru Live"  CACHE STRING "Product Long Name")
set(PRODUCT_UUID        "ea489821-6c35-4bd0-9dae-bb17c585e680"
                                    CACHE STRING "Product UUID")
option(PRODUCT_EMBED_BUILD_INFO "Embed build revision information into plProduct" ON)
cmake_dependent_option(PRODUCT_EMBED_BUILD_TIME "Embed build time into plProduct" ON [[PRODUCT_EMBED_BUILD_INFO]] OFF)
# A release build embeds the full commit of the source tree it was built from (and the tag at that commit, if any)
# and stops when that tree has uncommitted changes, so the client never reports a revision that its published source
# does not match (cmake/BuildInfo.cmake).
option(PRODUCT_RELEASE_BUILD "Release build: embed the source tree's commit and refuse a tree with uncommitted changes" OFF)
if(PRODUCT_RELEASE_BUILD AND NOT PRODUCT_EMBED_BUILD_INFO)
    message(FATAL_ERROR "PRODUCT_RELEASE_BUILD needs PRODUCT_EMBED_BUILD_INFO=ON")
endif()
# A release build of the browser client gives the same bytes wherever the checkout and the build dir lie: its compiles
# name their sources by paths relative to the build dir (cmake/ReleaseSourcePaths.cmake). Before any target.
if(PRODUCT_RELEASE_BUILD AND EMSCRIPTEN)
    include(ReleaseSourcePaths)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_FOLDER "CMakePredefinedTargets")

# HeadSpin Configuration

# Define HS_DEBUGGING for debug builds
add_compile_definitions($<$<CONFIG:Debug>:HS_DEBUGGING>)

if(WIN32 AND NOT CYGWIN)
    add_definitions(-DHS_BUILD_FOR_WIN32)
endif(WIN32 AND NOT CYGWIN)

if(UNIX)
    # This is set for both Linux and Mac builds
    add_definitions(-DHS_BUILD_FOR_UNIX)
endif(UNIX)

if(APPLE)
    add_definitions(-DHS_BUILD_FOR_APPLE)
    set(CMAKE_XCODE_GENERATE_SCHEME FALSE)

    if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
        add_definitions(-DHS_BUILD_FOR_MACOS)

        option(PLASMA_MAC_UNIVERSAL "Build macOS multi-architecture universal builds?" OFF)
        if (PLASMA_MAC_UNIVERSAL)
            set(CMAKE_OSX_ARCHITECTURES "$(ARCHS_STANDARD)")
        endif()

        if(CMAKE_SYSTEM_VERSION VERSION_LESS 10) # Darwin 10 == Mac OS X 10.6
            set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
        endif()
    endif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
endif(APPLE)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_definitions(-DHS_BUILD_FOR_LINUX)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
# End HeadSpin Configuration

# Custom configuration for Release (optimized) with PDBs
if(MSVC)
    if(NOT "RelWithPDB" IN_LIST CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES};RelWithPDB" CACHE STRING "" FORCE)
    endif()

    # Yeah, this looks strange. The semicolon is to establish an empty list item
    # for the configuration-less import.
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHPDB "Release;")

    # Need /Zi for the PDB
    set(CMAKE_C_FLAGS_RELWITHPDB   "${CMAKE_C_FLAGS_RELEASE} /Zi" CACHE STRING "")
    set(CMAKE_CXX_FLAGS_RELWITHPDB "${CMAKE_CXX_FLAGS_RELEASE} /Zi" CACHE STRING "")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHPDB "/INCREMENTAL:NO /DEBUG:FULL /OPT:REF,NOICF" CACHE STRING "")
    set(CMAKE_MODULE_LINKER_FLAGS_RELWITHPDB "/INCREMENTAL:NO /DEBUG:FULL /OPT:REF,NOICF" CACHE STRING "")
    set(CMAKE_RC_FLAGS_RELWITHPDB "" CACHE STRING "")
    set(CMAKE_SHARED_LINKER_FLAGS_RELWITHPDB "/INCREMENTAL:NO /DEBUG:FULL /OPT:REF,NOICF" CACHE STRING "")
    set(CMAKE_STATIC_LINKER_FLAGS_RELWITHPDB "" CACHE STRING "")
    mark_as_advanced(
        CMAKE_C_FLAGS_RELWITHPDB CMAKE_CXX_FLAGS_REL_WITHPDB CMAKE_EXE_LINKER_FLAGS_RELWITHPDB
        CMAKE_MODULE_LINKER_FLAGS_RELWITHPDB CMAKE_RC_FLAGS_RELWITHPDB CMAKE_SHARED_LINKER_FLAGS_RELWITHPDB
        CMAKE_STATIC_LINKER_FLAGS_RELWITHPDB
    )
endif()
# End custom release configuration

# Find all 3rd-party libraries that are required
find_package(ASIO 1.14 REQUIRED)
if(NOT EMSCRIPTEN)
    # curl is used only by the launcher and the Windows winmain; neither is built for wasm.
    find_package(CURL REQUIRED)
endif()
find_package(DirectX)
find_package(epoxy)
find_package(expat REQUIRED)
find_package(freetype REQUIRED)
find_package(JPEG REQUIRED)
find_package(libwebm)
find_package(Ogg REQUIRED)
find_package(OpenAL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Opus)
find_package(PhysX REQUIRED)
find_package(PNG REQUIRED)
find_package(Speex)
find_package(string_theory 3.4 REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
find_package(tl-expected REQUIRED)
find_package(Vorbis REQUIRED)
find_package(VPX)
find_package(ZLIB REQUIRED)

# The Python ABI the script glue (pfPython) is compiled for. 3 (default): the CPython 3 found below.
# 2: the py2 flavour (plClient-py2), linked with CPython 2.3.3 from the prefix PLASMA_WASM_PYTHON2
# (set by the wasm toolchain), which runs a shard's own Python 2.3 game code (MOULa's python.pak).
# One glue source compiles for both through pfPython/pyPy2Compat.h. The py2 flavour is built for
# Emscripten only.
set(PLASMA_PYTHON_ABI "3" CACHE STRING "Python ABI of the script glue: 3, or 2 for the py2 flavour (Emscripten)")
set_property(CACHE PLASMA_PYTHON_ABI PROPERTY STRINGS 2 3)

# The py2 flavour compiles and links against CPython 2.3 alone, so it needs no CPython 3 headers or
# library: only the interpreter that runs the build's own scripts, which a cross build finds itself
# (Sources/Plasma/Apps/plClient/CMakeLists.txt, Python3_HOST_EXECUTABLE).
if(PLASMA_PYTHON_ABI STREQUAL "2")
    if(NOT (${CMAKE_VERSION} VERSION_GREATER_EQUAL 4.0 AND CMAKE_CROSSCOMPILING))
        find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    endif()
elseif(${CMAKE_VERSION} VERSION_GREATER_EQUAL 4.0 AND CMAKE_CROSSCOMPILING)
    find_package(Python3 3.8 REQUIRED COMPONENTS Development)
else()
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter Development)
endif()

if(PLASMA_PYTHON_ABI STREQUAL "2")
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "PLASMA_PYTHON_ABI=2 (the py2 flavour) is built for Emscripten only")
    endif()
    if(NOT EXISTS "${PLASMA_WASM_PYTHON2}/lib/libpython2.3.a" OR
       NOT EXISTS "${PLASMA_WASM_PYTHON2}/include/python2.3/patchlevel.h")
        message(FATAL_ERROR "PLASMA_PYTHON_ABI=2: no CPython 2.3 prefix at '${PLASMA_WASM_PYTHON2}' "
            "(the deps build's python2 step, build-deps.sh python2, installs one; or set PLASMA_WASM_PYTHON2=<prefix>)")
    endif()
    add_library(PlasmaPython2::Python STATIC IMPORTED)
    set_target_properties(PlasmaPython2::Python PROPERTIES
        IMPORTED_LOCATION "${PLASMA_WASM_PYTHON2}/lib/libpython2.3.a"
        INTERFACE_INCLUDE_DIRECTORIES "${PLASMA_WASM_PYTHON2}/include/python2.3"
        INTERFACE_COMPILE_DEFINITIONS "PLASMA_PYTHON_ABI=2"
        # The 2.3 headers declare `register` parameters (an error in C++17) and take `char*`
        # names that the glue passes as string literals.
        INTERFACE_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:CXX>:-Wno-register;-Wno-writable-strings>")
    set(PLASMA_PYTHON_TARGET PlasmaPython2::Python)
    message(STATUS "Python ABI 2: ${PLASMA_WASM_PYTHON2}/lib/libpython2.3.a (py2 flavour, plClient-py2)")
elseif(PLASMA_PYTHON_ABI STREQUAL "3")
    # The selector pfPython tests (pfPython/pyPy2Compat.h), as the py2 target above carries it.
    set_property(TARGET Python3::Python APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS "PLASMA_PYTHON_ABI=3")
    set(PLASMA_PYTHON_TARGET Python3::Python)
else()
    message(FATAL_ERROR "PLASMA_PYTHON_ABI must be 2 or 3, not '${PLASMA_PYTHON_ABI}'")
endif()

if(APPLE)
    find_package(Security REQUIRED)
    find_package(Metal)
    find_package(LibArchive REQUIRED)
    find_package(VideoToolbox)

    if(TARGET Metal::Metal)
        check_language(Metal)
    endif()
elseif(EMSCRIPTEN)
    # Emscripten sets UNIX, but there is no fontconfig, libsecret, Wayland or X11 in a
    # browser. libuuid is provided by Emscripten itself: <uuid/uuid.h> is in the sysroot
    # and libuuid.js (uuid_generate via crypto.getRandomValues) is linked by default.
    if(NOT TARGET Uuid::Uuid)
        add_library(Uuid::Uuid INTERFACE IMPORTED)
    endif()
elseif(UNIX)
    find_package(LIBSECRET)
    find_package(Fontconfig REQUIRED)
    find_package(Uuid REQUIRED)
    find_package(Wayland 1.20 COMPONENTS Client)
    find_package(X11 COMPONENTS Xrandr)
elseif(WIN32)
    if(PLASMA_BUILD_MAX_PLUGIN STREQUAL "REQUIRED")
        set(_3dsm_FIND_ARG "REQUIRED")
    endif()
    find_package(3dsm ${_3dsm_FIND_ARG})
    find_package(VLD)
endif()

include(PythonUtils)
include(PlasmaTargets)

option(PLASMA_EXTERNAL_RELEASE "Is this release intended for the general public?" OFF)
if(PLASMA_EXTERNAL_RELEASE)
    add_definitions(-DPLASMA_EXTERNAL_RELEASE)
endif(PLASMA_EXTERNAL_RELEASE)

# The harness build: ON compiles the test, comparison and investigation instruments and adds the testhooks/
# overlay that holds them. OFF is the product build, the only one that is deployed or published.
option(PLASMA_TEST_HOOKS "Build the test-hook instruments and the testhooks/ overlay (harness builds only)" OFF)
if(PLASMA_TEST_HOOKS)
    # Where that overlay is: this tree's testhooks/ by default, or a test checkout of its own, so the source tree
    # needs no overlay of its own.
    set(PLASMA_TEST_HOOKS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/testhooks" CACHE PATH
        "The test-hook overlay a harness build (PLASMA_TEST_HOOKS ON) compiles")
    if(NOT EXISTS "${PLASMA_TEST_HOOKS_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "PLASMA_TEST_HOOKS is ON, but PLASMA_TEST_HOOKS_DIR (${PLASMA_TEST_HOOKS_DIR}) holds no overlay")
    endif()
    add_definitions(-DPLASMA_TEST_HOOKS)
endif(PLASMA_TEST_HOOKS)

# Pipeline Renderers
cmake_dependent_option(PLASMA_PIPELINE_DX "Enable DirectX rendering pipeline?" ON "DirectX_FOUND" OFF)
cmake_dependent_option(PLASMA_PIPELINE_GL "Enable OpenGL rendering pipeline?" ON "TARGET epoxy::epoxy" OFF)
cmake_dependent_option(PLASMA_PIPELINE_METAL "Enable Metal rendering pipeline?" ON "TARGET Metal::Metal AND CMAKE_Metal_COMPILER" OFF)

if(PLASMA_PIPELINE_DX)
    add_definitions(-DPLASMA_PIPELINE_DX)
endif(PLASMA_PIPELINE_DX)

if(PLASMA_PIPELINE_GL)
    add_definitions(-DPLASMA_PIPELINE_GL)
endif(PLASMA_PIPELINE_GL)

if(PLASMA_PIPELINE_METAL)
    add_definitions(-DPLASMA_PIPELINE_METAL)
    enable_language(Metal)
endif(PLASMA_PIPELINE_METAL)

# The browser's WebGPU pipeline (pfWGPUPipeline, a port of pfMetalPipeline), built for Emscripten only.
cmake_dependent_option(PLASMA_PIPELINE_WGPU "Enable WebGPU rendering pipeline?" ON "EMSCRIPTEN" OFF)
if(PLASMA_PIPELINE_WGPU)
    add_definitions(-DPLASMA_PIPELINE_WGPU)
endif(PLASMA_PIPELINE_WGPU)

# Allow us to disable certain parts of the build
option(PLASMA_BUILD_CLIENT "Do we want to build plClient?" ON)
cmake_dependent_option(PLASMA_BUILD_MAX_PLUGIN "Do we want to build the 3ds Max plugin?" OFF "TARGET 3dsm AND NOT PLASMA_EXTERNAL_RELEASE" OFF)
option(PLASMA_BUILD_LAUNCHER "Do we want to build plUruLauncher?" ON)
option(PLASMA_BUILD_TOOLS "Do we want to build the Plasma tools?" ON)
option(PLASMA_BUILD_TESTS "Do we want to build the unit tests?" OFF)
option(PLASMA_STACK_WALKER "Do we want to use the stack walker for crash handling?" ON)

cmake_dependent_option(USE_EFX "Use EFX for environmental audio effects" ON "OPENAL_HAS_EFX" OFF)
cmake_dependent_option(USE_EGL "Use EGL for OpenGL rendering" ON "epoxy_HAS_EGL" OFF)
cmake_dependent_option(USE_OPUS "Use Opus audio for voice chat" ON "TARGET Opus::opus" OFF)
cmake_dependent_option(USE_SPEEX "Use Speex audio for voice chat" ON "TARGET Speex::speex" OFF)
cmake_dependent_option(USE_VIDEOTOOLBOX "Use VideoToolbox movie player" ON "TARGET VideoToolbox::VideoToolbox" OFF)
cmake_dependent_option(USE_VPX "Use VPX for movie support" ON "TARGET VPX::VPX" OFF)
cmake_dependent_option(USE_WEBM "Use WebM movie player" ON "TARGET Opus::opus AND TARGET libwebm::libwebm AND USE_VPX" OFF)
cmake_dependent_option(USE_VLD "Build and link with Visual Leak Detector" OFF "TARGET VLD::VLD" OFF)
cmake_dependent_option(USE_WAYLAND "Build with Wayland compositor support" ON "TARGET Wayland::Client" OFF)
cmake_dependent_option(USE_X11 "Build with X11 support" ON "TARGET X11::X11" OFF)
cmake_dependent_option(USE_XRANDR "Build with X11 XRandR support" ON "TARGET X11::Xrandr" OFF)

# Configure compile-time compiler-specific flags and other config files
include(cmake/CompilerChecks.cmake)

configure_file("${PROJECT_SOURCE_DIR}/cmake/hsConfig.h.cmake" "${PROJECT_BINARY_DIR}/hsConfig.h")
include_directories(${PROJECT_BINARY_DIR})

if(PLASMA_BUILD_TOOLS)
    # Custom dummy target for compiling all tools
    add_custom_target(tools)
    set_target_properties(tools PROPERTIES
        FOLDER Tools
        XCODE_GENERATE_SCHEME TRUE
    )
endif()

include(TestBigEndian)
test_big_endian(BIG_ENDIAN)
if(BIG_ENDIAN)
    add_definitions(-DHS_BIG_ENDIAN)
endif()

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
    add_definitions(-D_SCL_SECURE_NO_DEPRECATE)
endif(MSVC)

set(PLASMA_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Sources/Plasma")
set(PLASMA_TOOLS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Sources/Tools")
set(PLASMA_MAX_PLUGIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Sources/MaxPlugin")

add_subdirectory(Sources/Plasma)
add_subdirectory(Scripts)

if(PLASMA_TEST_HOOKS)
    # The binary dir is named, so the overlay builds in <build>/testhooks wherever its sources are.
    add_subdirectory("${PLASMA_TEST_HOOKS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/testhooks")
endif(PLASMA_TEST_HOOKS)

if(PLASMA_BUILD_MAX_PLUGIN)
    add_subdirectory(Sources/MaxPlugin)
endif()

if(PLASMA_BUILD_TESTS)
    enable_testing()
    add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIGURATION>)
    set_target_properties(check PROPERTIES FOLDER Tests)
    add_subdirectory(Sources/Tests EXCLUDE_FROM_ALL)
endif(PLASMA_BUILD_TESTS)

if(PLASMA_BUILD_TOOLS)
    add_subdirectory(Sources/Tools)
endif()

# This must be called only after all Qt targets are declared
plasma_deploy_qt()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
