# A copy of vcpkg's triplets/community/wasm32-emscripten.cmake plus the browser client's
# global flags (pthreads, native wasm exceptions, wasm longjmp). Every object in the
# client must use exactly these, or wasm-ld refuses to mix them.
# PLASMA_WASM_DEPS, the deps prefix (build-deps.sh exports it), reaches the chainload toolchain below, which maps
# it to a stable name in every object (-ffile-prefix-map). It is untracked: where a prefix lives is no input of a
# package, and with the map no package depends on it.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSCRIPTEN_ROOT EMSDK PATH PLASMA_WASM_DEPS)

if(NOT DEFINED ENV{EMSCRIPTEN_ROOT})
   find_path(EMSCRIPTEN_ROOT "emcc")
else()
   set(EMSCRIPTEN_ROOT "$ENV{EMSCRIPTEN_ROOT}")
endif()

if(NOT EMSCRIPTEN_ROOT)
   if(NOT DEFINED ENV{EMSDK})
      message(FATAL_ERROR "The emcc compiler not found in PATH")
   endif()
   set(EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")
endif()

if(NOT EXISTS "${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
   message(FATAL_ERROR "Emscripten.cmake toolchain file not found")
endif()

set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
# Wrapper around Emscripten.cmake that appends the global flags (chainload ignores VCPKG_C_FLAGS).
get_filename_component(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../../cmake/emscripten/ChainloadToolchain.cmake" ABSOLUTE)

set(VCPKG_BUILD_TYPE release)
set(PLASMA_WASM_FLAGS "-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm")
# Recorded for vcpkg's ABI hash and for ports that read them directly; the chainload
# toolchain above is what actually applies them to CMake ports.
set(VCPKG_C_FLAGS "${PLASMA_WASM_FLAGS}")
set(VCPKG_CXX_FLAGS "${PLASMA_WASM_FLAGS}")
set(VCPKG_LINKER_FLAGS "${PLASMA_WASM_FLAGS}")

# OpenSSL compiles its module and engine directories into libcrypto (-DMODULESDIR, -DENGINESDIR:
# Configurations/unix-Makefile.tmpl, both $(libdir)/..., libdir from --prefix), and vcpkg configures make-based ports
# with --prefix set to the deps prefix's installed tree (vcpkg_configure_make), so the client carried that build
# host path (crypto/defaults.c returns MODULESDIR when a provider module is looked up). A browser build loads no
# modules or engines. Make's command-line variables, passed in MAKEFLAGS, override the Makefile's: the directories
# are given as a /usr/local install would have them, like the CPython build's prefix (build-deps.sh). This file is
# included in each port's build with PORT set (scripts/ports.cmake), so only OpenSSL's make sees it.
if(PORT STREQUAL "openssl")
    set(ENV{MAKEFLAGS} "MODULESDIR=/usr/local/lib/ossl-modules ENGINESDIR=/usr/local/lib/engines-3")
endif()
