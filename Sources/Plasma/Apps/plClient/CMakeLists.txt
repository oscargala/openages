set(external_SCRIPTS
    external/create_resource_dat.py
    external/makeres.py
    external/render_svg.py
    external/scalergba.py
)

set(external_SOURCES
    external/Cursor_Base.svg
    external/Linking_Book.svg
    external/Loading_Text_rasterfont.svg
    external/Voice_Chat.svg
)
find_program(
    PNGCRUSH_EXECUTABLE pngcrush
    DOC "Path to pngcrush"
)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 4.0 AND CMAKE_CROSSCOMPILING)
    set(Python3_ARTIFACTS_PREFIX "_HOST")
    find_package(Python3 3.8 COMPONENTS Interpreter)
endif()

if(Python3_EXECUTABLE AND NOT Python3_HOST_EXECUTABLE)
    set(Python3_HOST_EXECUTABLE "${Python3_EXECUTABLE}")
endif()

# Can we generate the resource.dat file?
python_test_modules(
    MODULES cairosvg PIL
    REQUIREMENTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/external/requirements.txt"
)

cmake_dependent_option(PLASMA_BUILD_RESOURCE_DAT "Do we want to build resource.dat?" ON "cairosvg_FOUND AND PIL_FOUND" OFF)
cmake_dependent_option(RESOURCE_OPTIMIZE "Optimize the images in resource.dat" ON "PLASMA_BUILD_RESOURCE_DAT AND PNGCRUSH_EXECUTABLE" OFF)
cmake_dependent_option(RESOURCE_BRUTE "Allow pngcrush brute-force optimization" OFF "PLASMA_BUILD_RESOURCE_DAT AND PNGCRUSH_EXECUTABLE" OFF)

if(PLASMA_BUILD_RESOURCE_DAT)
    set(external_DAT "${CMAKE_CURRENT_BINARY_DIR}/resource.dat")
    if(RESOURCE_OPTIMIZE)
        list(APPEND OPTIMIZE_ARGUMENT --pngcrush "${PNGCRUSH_EXECUTABLE}")
    endif()
    if(RESOURCE_BRUTE)
        list(APPEND OPTIMIZE_ARGUMENT "--brute")
    endif()

    add_custom_command(
        OUTPUT ${external_DAT}
        COMMAND "${Python3_HOST_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/makeres.py"
                --render --package
                ${OPTIMIZE_ARGUMENT}
                -i "${CMAKE_CURRENT_SOURCE_DIR}/external"
                -w "${CMAKE_CURRENT_BINARY_DIR}"
                -o "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS ${external_SCRIPTS} ${external_SOURCES}
    )

    install(
        FILES "${external_DAT}"
        DESTINATION client
    )
endif()

set(plClient_HEADERS
    plClient.h
    plClientCreatable.h
    plClientLoader.h
    plClientLogin.h
    plClientUpdateFormat.h
)

set(plClient_SOURCES
    pfAllCreatables.cpp
    plAllCreatables.cpp
    plClient.cpp
    plClientCreatable.cpp
    plClientLoader.cpp
    plClientLogin.cpp
    pnAllCreatables.cpp
)

set(plClient_RESOURCES
)

set(plClient_TEXT
    ${Plasma_SOURCE_DIR}/Docs/ReleaseNotes/ReleaseNotes.txt
)

if(WIN32)
    list(APPEND plClient_SOURCES
        win32/plClient_Win.cpp
        win32/winmain.cpp
    )
    list(APPEND plClient_RESOURCES
        win32/res/plClient.exe.manifest
        win32/res/plClient.rc
        win32/res/resource.h
        win32/res/banner.bmp
        win32/res/cnsl1.bin
        win32/res/Dirt.ICO
        win32/res/headspin.ico
    )
elseif(APPLE)
    list(APPEND plClient_SOURCES
        Mac-Cocoa/main.mm
        Mac-Cocoa/NSString+StringTheory.mm
        Mac-Cocoa/PLSKeyboardEventMonitor.mm
        Mac-Cocoa/PLSView.mm
        Mac-Cocoa/PLSLoginController.mm
        Mac-Cocoa/PLSLoginWindowController.mm
        Mac-Cocoa/PLSPatcherWindowController.mm
        Mac-Cocoa/PLSPatcher.mm
        Mac-Cocoa/PLSServerStatus.mm
    )
    list(APPEND plClient_HEADERS
        Mac-Cocoa/NSString+StringTheory.h
        Mac-Cocoa/PLSKeyboardEventMonitor.h
        Mac-Cocoa/PLSView.h
        Mac-Cocoa/PLSLoginController.h
        Mac-Cocoa/PLSLoginWindowController.h
        Mac-Cocoa/PLSPatcherWindowController.h
        Mac-Cocoa/PLSPatcher.h
        Mac-Cocoa/PLSServerStatus.h
    )
    list(APPEND plClient_RESOURCES
        $<$<VERSION_GREATER_EQUAL:${XCODE_VERSION},26.0>:Mac-Cocoa/AppIcon.icon>
        Mac-Cocoa/banner.png
        Mac-Cocoa/banner@2x.png
        Mac-Cocoa/MainMenu.xib
        Mac-Cocoa/PLSLoginWindowController.xib
        Mac-Cocoa/PLSPatcherWindowController.xib
    )
    set_source_files_properties("Mac-Cocoa/AppIcon.icon" PROPERTIES
        XCODE_EXPLICIT_FILE_TYPE "folder.iconcomposer.icon"
        MACOSX_PACKAGE_LOCATION Resources
    )
elseif(EMSCRIPTEN)
    list(APPEND plClient_SOURCES
        emscripten/main.cpp
        emscripten/plEmscriptenCwd.cpp
        emscripten/plEmscriptenRuntime.cpp
        emscripten/plEmscriptenInput.cpp
        emscripten/plEmscriptenFullscreen.cpp
        emscripten/plEmscriptenPatcher.cpp
        emscripten/plEmscriptenDisplay.cpp
        emscripten/plEmscriptenFrameHooks.cpp
        emscripten/plEmscriptenFrameLoop.cpp
        emscripten/plEmscriptenLogin.cpp
        emscripten/plEmscriptenNullDevice.cpp
        emscripten/plEmscriptenPoll.cpp
        emscripten/plEmscriptenStdio.cpp
        emscripten/plEmscriptenStorage.cpp
    )
else()
    list(APPEND plClient_SOURCES
        main.cpp
    )
endif()

plasma_executable(plClient CLIENT INSTALL_PDB
    FOLDER Apps
    SOURCES
        ${plClient_SOURCES} ${plClient_HEADERS}
        ${plClient_TEXT} ${plClient_RESOURCES}
)

if(APPLE)
    # We need to filter out the XIB files so the source XML files don't get bundled
    set(plClient_XCODE_RESOURCES ${plClient_RESOURCES})
    list(FILTER plClient_XCODE_RESOURCES EXCLUDE REGEX "\\.x(ib|cassets)$")

    set_target_properties(plClient PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Mac-Cocoa/Info.plist.in"
        # Hard coding the app name here intentionally.
        # Should prevent macOS from tripping over secured resources being
        # accessed from two different apps. In theory - plClient is just
        # the debug version of UruExplorer. It also prevents invalid characters.
        # GUI identifier should follow the format of a reverse domain string.
        # https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFBundles/AccessingaBundlesContents/AccessingaBundlesContents.html
        MACOSX_BUNDLE_GUI_IDENTIFIER org.Huru.UruExplorer
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        RESOURCE "${plClient_XCODE_RESOURCES}"
        XCODE_ATTRIBUTE_ASSETCATALOG_OTHER_FLAGS "$<$<VERSION_GREATER_EQUAL:${XCODE_VERSION},26>:--enable-icon-stack-fallback-generation=disabled>"
        XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE "YES"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
        XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS[variant=Debug] "YES"
        XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS[variant=RelWithDebInfo] "YES"
        XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
        XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/Mac-Cocoa/plClient.entitlements"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.Huru.UruExplorer
    )
    target_compile_options(plClient PRIVATE -fobjc-arc)
    target_sources(plClient PRIVATE Mac-Cocoa/Assets.xcassets)
    set_source_files_properties(Mac-Cocoa/Assets.xcassets ${RESOURCES} PROPERTIES
         MACOSX_PACKAGE_LOCATION Resources
    )

    if(PLASMA_APPLE_DEVELOPMENT_TEAM_ID)
            set_target_properties(plClient PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${PLASMA_APPLE_DEVELOPMENT_TEAM_ID}
            )
    endif()
endif()

if(PLASMA_PIPELINE_METAL)
    target_embed_metal_shader_libraries(plClient
        pfMetalPipelineShadersMSL21
        pfMetalPipelineShadersMSL23
    )

    if(NOT DEFINED CMAKE_XCODE_VERSION_MAJOR OR
        CMAKE_XCODE_VERSION_MAJOR VERSION_GREATER_EQUAL 13)
        target_embed_metal_shader_libraries(plClient
            pfMetalPipelineShadersMSL30
        )
    endif()
endif()

if(PLASMA_BUILD_RESOURCE_DAT)
    target_sources(plClient PRIVATE ${external_SCRIPTS} ${external_SOURCES} ${external_DAT})
endif()

target_link_libraries(
    plClient
    PRIVATE
        CoreLib

        # For the "all creatables"
        pnNucleusInc
        plPubUtilInc
        pfFeatureInc

        # Everything else used in this target.
        pnDispatch
        pnFactory
        pnKeyedObject
        pnMessage
        pnNetBase
        pnSceneObject
        plAgeLoader
        plAnimation
        plAudio
        plAvatar
        plClientResMgr
        plDrawable
        plFile
        plGImage
        plGLight
        plInputCore
        plMessage
        plMessageBox
        plModifier
        plNetClient
        plNetCommon
        plNetGameLib
        plPhysX
        plPipeline
        plProgressMgr
        plResMgr
        plScene
        plSDL
        plStatGather
        plStatusLog
        plUnifiedTime
        pfAnimation
        pfAudio
        pfCharacter
        pfConsole
        pfConsoleCore
        $<$<PLATFORM_ID:Windows>:pfCrashHandler>
        pfDisplayHelpers
        pfGameGUIMgr
        pfJournalBook
        pfLocalizationMgr
        pfMoviePlayer
        pfPasswordStore
        pfPatcher
        pfPython
        $<$<BOOL:${PLASMA_PIPELINE_DX}>:pfDXPipeline>
        $<$<BOOL:${PLASMA_PIPELINE_GL}>:pfGLPipeline>
        $<$<BOOL:${PLASMA_PIPELINE_METAL}>:pfMetalPipeline>
        $<$<BOOL:${PLASMA_PIPELINE_WGPU}>:pfWGPUPipeline>
        $<$<NOT:$<PLATFORM_ID:Emscripten>>:CURL::libcurl>
        "$<$<PLATFORM_ID:Darwin>:-framework Cocoa>"
        "$<$<PLATFORM_ID:Darwin>:-framework QuartzCore>"
        # Only need IOKit when targeting Mac OS X < 10.9
        "$<$<AND:$<PLATFORM_ID:Darwin>,$<VERSION_LESS:${CMAKE_OSX_DEPLOYMENT_TARGET},10.9>>:-framework IOKit>"
        $<$<PLATFORM_ID:Darwin>:${LibArchive_LIBRARIES}>
)
target_include_directories(plClient PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(EMSCRIPTEN)
    # The login dialog's banner (Mac-Cocoa/banner@2x.png, the Mac login window's art) compiled into
    # the module as bytes; the file is rewritten only when the picture changes.
    set(_plasma_banner "${CMAKE_CURRENT_SOURCE_DIR}/Mac-Cocoa/banner@2x.png")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_plasma_banner}")
    file(READ "${_plasma_banner}" _plasma_banner_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _plasma_banner_bytes "${_plasma_banner_hex}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/plEmscriptenLoginBanner.inc.tmp" "${_plasma_banner_bytes}\n")
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/plEmscriptenLoginBanner.inc.tmp"
        "${CMAKE_CURRENT_BINARY_DIR}/plEmscriptenLoginBanner.inc" COPYONLY)
    target_include_directories(plClient PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    unset(_plasma_banner_hex)
    unset(_plasma_banner_bytes)
    # Pinned WasmFS virtual adapters; SDK/dependency sources remain read-only.
    # plEmscriptenCwd.cpp builds on WasmFS internals (system/lib/wasmfs), which are no stable API, so
    # it compiles only against the Emscripten release that web/build/sources.lock pins. The toolchain reads that
    # version (cmake/emscripten/Toolchain.cmake, PLASMA_EMSCRIPTEN_PINNED_VERSION) and plEmscriptenCwd.cpp checks the
    # compiler against it.
    if(NOT PLASMA_EMSCRIPTEN_PINNED_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
        message(FATAL_ERROR "plClient: no pinned Emscripten version (PLASMA_EMSCRIPTEN_PINNED_VERSION): configure the "
            "browser client with cmake/emscripten/Toolchain.cmake")
    endif()
    set(_plasma_emscripten_pin
        PLASMA_EMSCRIPTEN_PINNED_MAJOR=${CMAKE_MATCH_1}
        PLASMA_EMSCRIPTEN_PINNED_MINOR=${CMAKE_MATCH_2}
        PLASMA_EMSCRIPTEN_PINNED_TINY=${CMAKE_MATCH_3})
    set_source_files_properties(emscripten/plEmscriptenCwd.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${EMSCRIPTEN_ROOT_PATH}/system/lib/wasmfs"
        COMPILE_DEFINITIONS "${_plasma_emscripten_pin}")
    # plEmscriptenStdio.cpp mirrors musl's FILE layout (system/lib/libc/musl/src/internal/stdio_impl.h), no API either,
    # and checks the compiler against the same pin.
    set_source_files_properties(emscripten/plEmscriptenStdio.cpp PROPERTIES
        COMPILE_DEFINITIONS "${_plasma_emscripten_pin}")
    unset(_plasma_emscripten_pin)
    # The Python target. plClient's own sources call no Python API: the frame loop's re-entry counter is the private
    # test harness's, which links the target itself. The link is kept as it was.
    target_link_libraries(plClient PRIVATE ${PLASMA_PYTHON_TARGET})
    if(PLASMA_PYTHON_ABI STREQUAL "2")
        # The py2 flavour: the same client with the script glue compiled for
        # CPython 2.3.3 (root CMakeLists.txt PLASMA_PYTHON_ABI), written as plClient-py2.{js,wasm}.
        # CPython 2.3 has no call trampolines (Objects/methodobject.c calls every C function
        # directly), so the PY_CALL_TRAMPOLINE check below is for ABI 3 only.
        set_target_properties(plClient PROPERTIES OUTPUT_NAME plClient-py2)
    else()
        # JSPI suspensions happen inside Python frames (a present can run there, as it does on native).
        # A CPython built with PY_CALL_TRAMPOLINE calls every C method, getter and setter through a
        # JS frame, across which JSPI cannot suspend: a suspension there traps. Such a libpython is
        # refused: its EM_JS trampolines (_PyCFunctionWithKeywords_TrampolineCall,
        # _PyImport_InitFunc_TrampolineCall, descr_get/set_trampoline_call) are symbols of the
        # archive. The check reruns whenever the archive changes.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${Python3_LIBRARY}")
        execute_process(COMMAND "${CMAKE_NM}" "${Python3_LIBRARY}"
            RESULT_VARIABLE _plasma_py_nm OUTPUT_VARIABLE _plasma_py_syms ERROR_QUIET)
        if(NOT _plasma_py_nm EQUAL 0 OR NOT _plasma_py_syms MATCHES "PyRun_String")
            message(FATAL_ERROR "plClient: cannot read the symbols of ${Python3_LIBRARY} with ${CMAKE_NM} "
                "(result ${_plasma_py_nm}); the PY_CALL_TRAMPOLINE check needs them")
        endif()
        if(_plasma_py_syms MATCHES "TrampolineCall|trampoline_call")
            # The message names the fix, not a script: CPython defines PY_CALL_TRAMPOLINE only when
            # the build asks for it (-DPY_CALL_TRAMPOLINE, bpo-47162), and the deps build's python step does not.
            message(FATAL_ERROR "plClient: ${Python3_LIBRARY} was built with PY_CALL_TRAMPOLINE (it has its "
                "EM_JS trampolines); JSPI cannot suspend across them. Use a CPython built without "
                "-DPY_CALL_TRAMPOLINE, as the deps build's python step builds it, from PLASMA_WASM_DEPS or "
                "PLASMA_WASM_PYTHON.")
        endif()
        message(STATUS "plClient: ${Python3_LIBRARY} PY_CALL_TRAMPOLINE=0")
        unset(_plasma_py_nm)
        unset(_plasma_py_syms)
    endif()
    # wasm32 module: main() runs on a pthread (the browser main thread stays free for the
    # event loop); the heap grows on demand; the runtime exits when main returns.
    # WasmFS: the game data is read lazily over HTTP (fetch backend) and the user folder
    # lives in OPFS; the legacy JS FS runs on the browser main thread and cannot block
    # on a fetch. Stacks: 8 MiB for main() (the Linux/macOS main-thread default), 1 MiB
    # for every other thread (the Windows default the client shipped with).
    # stringToUTF8, lengthBytesUTF8: for EM_ASM code, which cannot name the library functions it calls (the private
    # test harness's login).
    # getExceptionMessage: pre.js names an uncaught C++ exception by its type and what() with it.
    # ENV: pre.js installs the client's environment from the page's configuration (Module.plasma.config)
    # into it; it is not exported, so the page has no handle on the environment of its own.
    set(_plasma_runtime_helpers "$stringToUTF8,$lengthBytesUTF8,$getExceptionMessage,$ENV")
    if(PLASMA_WASM_NATIVE_AUDIO)
        string(APPEND _plasma_runtime_helpers ",$plasmaNativeAudio")
    else()
        string(APPEND _plasma_runtime_helpers ",$plasmaOpenALConeCompat,$plasmaOpenALDistanceCompat,$plasmaOpenALOffsetCompat,$plasmaOpenALQueryCompat")
    endif()
    # The asset cache keeps a fixed number of asset origins pinned
    # (emscripten/plEmscriptenAssetCache.js), and the page refuses a deployment whose asset roots name more distinct
    # origins than that (the page shell's config.mjs). The two must agree, so the number is defined once, as
    # ASSET_ORIGIN_PINS in the page shell's config.mjs, and the client is built with the same value as
    # PlasmaAssetOriginPins (a pre-js), for the cache's pin rule and the texture pack fetch's origin check
    # (pfWGPUPipeline/plWGPUPipeline.js). PLASMA_WASM_SHELL_CONFIG names that file; its default is the shell's own
    # folder in this repository.
    set(PLASMA_WASM_SHELL_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/shell/config.mjs" CACHE FILEPATH
        "The browser page shell's config.mjs: the client is built with its ASSET_ORIGIN_PINS")
    if(NOT EXISTS "${PLASMA_WASM_SHELL_CONFIG}")
        message(FATAL_ERROR "plClient: no page shell config.mjs at '${PLASMA_WASM_SHELL_CONFIG}': "
            "set PLASMA_WASM_SHELL_CONFIG to the shell's config.mjs, which defines ASSET_ORIGIN_PINS")
    endif()
    file(STRINGS "${PLASMA_WASM_SHELL_CONFIG}" _plasma_pins_line REGEX "^export const ASSET_ORIGIN_PINS = [0-9]+;$")
    list(LENGTH _plasma_pins_line _plasma_pins_count)    # (file(STRINGS) returns the line's ';' escaped)
    if(NOT _plasma_pins_count EQUAL 1 OR NOT _plasma_pins_line MATCHES "^export const ASSET_ORIGIN_PINS = ([0-9]+)")
        message(FATAL_ERROR "${PLASMA_WASM_SHELL_CONFIG}: no single 'export const ASSET_ORIGIN_PINS = <n>;' line")
    endif()
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/asset-origin-pins.js" CONTENT
        "// Generated by plClient's CMakeLists.txt from the page shell's ASSET_ORIGIN_PINS: do not edit.\n\
var PlasmaAssetOriginPins = ${CMAKE_MATCH_1};\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PLASMA_WASM_SHELL_CONFIG}")
    unset(_plasma_pins_line)
    unset(_plasma_pins_count)
    # Function names (the wasm name section, --profiling-funcs) and C++ exceptions that carry a JS
    # stack trace (-sEXCEPTION_STACK_TRACES), so that a trap or an uncaught exception prints a stack that names its
    # functions: in Debug and RelWithDebInfo builds, and in every harness build (the private test harness's). The Release
    # build, the one that ships, has neither. Its stacks name frames only as wasm-function[<index>], and an uncaught
    # exception reaches the page without a stack (pre.js still names its type and what()). In exchange the module is
    # smaller, and a throw unwinds inside the wasm instead of through a JS call (Emscripten then links the release
    # libc++abi).
    set(_plasma_debug_config "$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>")
    target_link_options(plClient PRIVATE
        # The client script defines the factory createPlasmaClient(moduleArg), which the page calls with the module's
        # arguments (shell/boot.mjs). The runtime (its environment, memory and libraries) lives inside the factory, out
        # of reach of the page's other scripts; each pthread worker loads the script and starts its own.
        -sMODULARIZE=1
        -sEXPORT_NAME=createPlasmaClient
        # The client runs only in a browser: on a cross-origin isolated page (shell/boot.mjs), so never from a file://
        # URL, and in that page's pthread workers. Without Node.js among its environments the script has none of
        # Emscripten's Node.js paths, and its top-level names are the factory and isPthread, with which a worker's copy
        # of the script starts its runtime (Emscripten's src/modularize.js).
        -sENVIRONMENT=web,worker
        # The wasm memory on the module object, which the factory returns: the page's moduleArg, held by the page's
        # module alone. When the client exits the page logs the memory's size, its largest in the run, since a
        # WebAssembly memory only grows (shell/boot.mjs).
        -sEXPORTED_RUNTIME_METHODS=wasmMemory
        -sPROXY_TO_PTHREAD=1
        -sEXIT_RUNTIME=1
        -sALLOW_MEMORY_GROWTH=1
        -sMAXIMUM_MEMORY=4GB
        -sFORCE_FILESYSTEM=1
        -sWASMFS=1
        -sSTACK_SIZE=8MB
        -sDEFAULT_PTHREAD_STACK_SIZE=1MB
        # Audio compatibility postsets correct the pinned OpenAL backend before main starts.
        "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=${_plasma_runtime_helpers}"
        "$<${_plasma_debug_config}:--profiling-funcs>"
        "$<${_plasma_debug_config}:-sEXCEPTION_STACK_TRACES=1>"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/pre.js"
        "--pre-js=${CMAKE_CURRENT_BINARY_DIR}/asset-origin-pins.js"
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenFetchBackend.js"
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenAssetCache.js"
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenAssetCatalog.js"
        # The JS half of pnAsyncCore's WebSocket socket backend
        "--js-library=${PLASMA_SOURCE_ROOT}/NucleusLib/pnAsyncCore/pnAcWebSocket.js"
        # getcwd() under a WasmFS mount point returns "/" (plEmscriptenCwd.cpp)
        -Wl,--wrap=getcwd
        -Wl,--wrap=chdir
        # WasmFS poll() ignores its timeout; asio's timer thread would spin (plEmscriptenPoll.cpp).
        # Its wait ends at a write to a pipe: pipe2 and close keep the pipes' write ends, write
        # wakes the waiting poll().
        -Wl,--wrap=__syscall_poll
        -Wl,--wrap=__syscall_pipe2
        -Wl,--wrap=__wasi_fd_close
        -Wl,--wrap=__wasi_fd_write
        # A 64 KiB stdio buffer for read-only game-data FILEs, and their seeks within it (plEmscriptenStdio.cpp)
        -Wl,--wrap=fopen
        -Wl,--wrap=fclose
        -Wl,--wrap=fseeko
        -Wl,--wrap=fseek
        -Wl,--wrap=ungetc
        -Wl,--wrap=ungetwc
        # WebGPU from the client thread: Dawn's webgpu.h for Emscripten, and the page's
        # #canvas transferred to main()'s pthread as an OffscreenCanvas (libpthread.js:
        # Module.canvas becomes that OffscreenCanvas in the pthread)
        --use-port=emdawnwebgpu:cpp_bindings=false
        -sOFFSCREENCANVAS_SUPPORT=1
        "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
        # [F1] the frame loop: one tick per requestAnimationFrame of the client thread,
        # entered through WebAssembly.promising; JS Promise Integration lets long calls
        # (loads, the intro movie) suspend to the next animation frame and present
        # (plEmscriptenFrameLoop.h). JSPI does not transform the wasm (unlike ASYNCIFY,
        # which does not combine with -fwasm-exceptions).
        -sJSPI
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenFrameLoop.js"
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenStorage.js"
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenInput.js"
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenFullscreen.js"
        # The page halves of the display, the login dialog and the patcher's launcher dialog
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenDisplay.js"
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenLogin.js"
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenPatcher.js"
    )
    set_property(TARGET plClient APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/pre.js"
        "${CMAKE_CURRENT_BINARY_DIR}/asset-origin-pins.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenFetchBackend.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenAssetCache.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenAssetCatalog.js"
        "${PLASMA_SOURCE_ROOT}/NucleusLib/pnAsyncCore/pnAcWebSocket.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenFrameLoop.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenStorage.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenInput.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenFullscreen.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenDisplay.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenLogin.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenPatcher.js"
        # Linked through pfWGPUPipeline's and plMessageBox's interface link options
        "${PLASMA_SOURCE_ROOT}/FeatureLib/pfWGPUPipeline/plWGPUPipeline.js"
        "${PLASMA_SOURCE_ROOT}/PubUtilLib/plMessageBox/hsMessageBox_Emscripten.js"
    )
    if(PLASMA_TEST_HOOKS)
        # The harness build's count of the WasmFS backend reads under the game-data FILEs (plEmscriptenStdio.cpp).
        # The product does not link this wrap.
        target_link_options(plClient PRIVATE -Wl,--wrap=__wasi_fd_read)
    endif()
    # The page's audio activation, one rule for both audio outputs below (each registers its AudioContexts with it)
    target_link_options(plClient PRIVATE
        "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenAudioActivation.js")
    set_property(TARGET plClient APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenAudioActivation.js")
    # The observers of the input, audio, movies and journeys, and the --wrap link flags of the movie
    # and native-audio observers, are the private test harness's.
    if(PLASMA_WASM_NATIVE_AUDIO)
        target_link_options(plClient PRIVATE
            "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenNativeAudio.js")
        set_property(TARGET plClient APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenNativeAudio.js")
    else()
        foreach(_audio_adapter Cones Distance Offset Lifecycle Queries)
            target_link_options(plClient PRIVATE
                "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenOpenAL${_audio_adapter}.js")
            set_property(TARGET plClient APPEND PROPERTY LINK_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/emscripten/plEmscriptenOpenAL${_audio_adapter}.js")
        endforeach()
    endif()
    # Link without -flto: with -sWASMFS, Emscripten 6.0.12's ThinLTO system libraries fail
    # to link ("import module mismatch for symbol: __wasi_fd_sync", libwasmfs-mt vs
    # libc-mt). The Plasma libraries stay ThinLTO bitcode; wasm-ld still optimizes them
    # together at link time, only the system libraries are the prebuilt non-LTO variants.
    set_target_properties(plClient PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
endif()

# The browser build keeps its web file names (plClient.js/plClient.wasm) in both
# flavours. They are page asset paths, not the executable the self-patcher's manifests name
# (plManifest::ClientExecutable); the flavour is the product string compiled into the module
# (plProduct::ProductString, "External.Release").
if(PLASMA_EXTERNAL_RELEASE AND NOT EMSCRIPTEN)
    set_target_properties(plClient PROPERTIES OUTPUT_NAME "UruExplorer")
endif()

if(APPLE AND NOT CMAKE_CROSSCOMPILING)
    # CMake will attempt to strip rpaths, thus preventing fixup_bundle from finding libraries.
    # Not sure why - turning that off.
    set_target_properties(plClient PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
    install(
        CODE
        [[
            include("BundleUtilities")
            fixup_bundle("${CMAKE_INSTALL_PREFIX}/client/$<TARGET_FILE_NAME:plClient>.app" "" "${searchDirs}" IGNORE_ITEM Python)
            execute_process(COMMAND codesign --sign - --deep --force "${CMAKE_INSTALL_PREFIX}/client/$<TARGET_FILE_NAME:plClient>.app")
        ]]
        DESTINATION client
    )
endif()

if(PLASMA_BUILD_RESOURCE_DAT)
    source_group("Client Resource Scripts" FILES ${external_SCRIPTS})
    source_group("Client Resource Images" FILES ${external_SOURCES})
endif()
source_group("Source Files" FILES ${plClient_SOURCES})
source_group("Header Files" FILES ${plClient_HEADERS})
source_group("Text" FILES ${plClient_TEXT})
source_group("Resources" FILES ${plClient_RESOURCES})
