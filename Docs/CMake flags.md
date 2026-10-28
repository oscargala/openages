CMake Flags
===========

These flags can be specified on the command-line when invoking CMake to generate
the project files, alongside CMake generators and toolsets to control what type
of build files are generated.

For example, to generate Ninja build files, building with debug libraries, and
enabling unit tests, you might run a command like this in the project root:

```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPLASMA_BUILD_TESTS=ON -S . -B build-ninja-debug
```

The resulting Ninja build files will be generated in a `build-ninja-debug`
subfolder and can be built either by running `ninja` directly or by running
CMake in the project root and specifying a specific target:

```
cmake --build build-ninja-debug --target plClient
```

To generate Visual Studio 2019 project files building a 32-bit Windows client,
invoke CMake with the following generator and arch options:
`-G "Visual Studio 16 2019" -A Win32`

Build Configuration Flags
-------------------------

* `USE_VCPKG`

  Whether to use the [vcpkg](https://vcpkg.io/) package manager to automatically
  build all dependencies. Default `ON` for Windows, `OFF` for other platforms.

* `CMAKE_BUILD_TYPE`

  Whether to make a `Debug` or `Release` build (or `RelWithDebInfo` to make a
  release build with debugging info). Default is probably `Debug`, but this is
  usually overridden by the build type as configured in the Visual Studio or
  Xcode IDEs. The browser client keeps its function names and the stack traces
  of its C++ exceptions in `Debug` and `RelWithDebInfo` builds only (and in
  every `PLASMA_TEST_HOOKS` build); a `Release` build leaves both out.

* `PLASMA_EXTERNAL_RELEASE`

  Whether this is an External release build (with internal developer tools such
  as the console disabled). Default is `OFF` (i.e., by default it will build an
  Internal client).

* `PLASMA_BUILD_CLIENT`

  Whether to build the plClient project. Default is `ON`.

* `PLASMA_BUILD_LAUNCHER`

  Whether to build the plUruLauncher project. Default is `ON`.

* `PLASMA_BUILD_TESTS`

  Whether to build the unit tests for the Plasma project. Default is `OFF`.

* `PLASMA_BUILD_TOOLS`

  Whether to build the non-client Plasma developer tools. Default is `ON`.

* `PLASMA_BUILD_MAX_PLUGIN`

  Whether to build the 3D Studio Max plugin. Default is `OFF` and cannot be
  turned `ON` if the 3DSMax SDK is not found. Can be set to `REQUIRED` to fail
  the build if the 3DSMax SDK is not found.

* `PLASMA_STACK_WALKER`

  Whether to build the Plasma crash handler program with stacktrace support.
  Default is `ON`.

* `PLASMA_BUILD_RESOURCE_DAT`

  Whether to build the resource.dat file resources at compile time. Default
  depends on whether Python, Pillow, and CairoSVG can be found (needed for
  generating PNG assets from the SVG source files before bundling into
  resource.dat).

* `PLASMA_UNITY_BUILD`

  Whether to allow multiple source files to be combined at build time into
  "Unified Sources" to speed up compilation times. Default depends on CMake
  version and whether optimizations are enabled.

* `PLASMA_USE_PCH`

  Whether to allow precompiled header files to be generated and used to speed up
  compilation times. Default depends on CMake version and whether optimizations
  are enabled.

* `PRODUCT_EMBED_BUILD_INFO`

  Whether to embed information about the git commit and branch in the product
  version. Default is `ON`.

* `PRODUCT_EMBED_BUILD_TIME`

  Whether to embed information about the build time in the product version.
  Default is based on the value of `PRODUCT_EMBED_BUILD_INFO`.

* `PRODUCT_RELEASE_BUILD`

  A release build: embeds the full commit of the source tree (read from the
  source directory, not the build directory) and the tag at that commit, or
  the commit's first 12 hex digits when it has none, and stops when the tree
  has changes that are not committed, untracked files included. Needs
  `PRODUCT_EMBED_BUILD_INFO`. The browser build sets it with
  `build-client.sh RELEASE=1`. Default is `OFF`.

* `PLASMA_WASM_SHELL_CONFIG`

  The browser client only: the `config.mjs` of the page shell. The client is
  built with its `ASSET_ORIGIN_PINS`, the number of asset origins the page and
  the client's asset cache admit, so that both use the same number. Default is
  the shell's `config.mjs` in this repository.


Build Feature Flags
-------------------

* `PLASMA_PIPELINE_DX`

  Whether to build Plasma with the DirectX rendering pipeline. Default is `ON`
  if the DirectX 9 SDK is found.

* `PLASMA_PIPELINE_GL`

  Whether to build Plasma with the (incomplete) OpenGL rendering pipeline.
  Default is `ON` if the libepoxy library is found.

* `PLASMA_PIPELINE_METAL`

  Whether to build Plasma with the Apple Metal rendering pipeline. Default is
  `ON` if compiling on an Apple system (i.e., macOS).

* `PLASMA_RESMGR_DEBUGGING`

  Whether to enable verbose plResManager debugging logs. Default is `OFF`.

* `USE_EFX`

  Whether to use EFX for environmental audio effects. Default is `ON` if the
  efx.h header file is found.

* `USE_EGL`

  Whether to use EGL as a backend option for the OpenGL rendering pipeline.
  Default is `ON` if libEGL is found.

* `USE_OPUS`

  Whether to use Opus as a higher quality voice chat codec. Default is `ON` if
  the Opus library is found.

* `USE_SPEEX`

  Whether to use Speex as a voice chat codec. Default is `ON` if the Speex
  library is found.

* `USE_VPX`

  Whether to use the VPX library for supporting WebM-encoded video files.
  Default is `ON` if libvpx is found.

* `USE_WEBM`

  Whether to use the libWebM library for supporting WebM-encoded video files.
  Default is `ON` if libwebm is found.

* `USE_CLANG_TIDY`

  Whether to enable using the clang-tidy sanitizer. Default is `OFF`.


Product Flags
-------------
You can't really change these without breaking compatibility with the MOULa
server.

* `PRODUCT_BRANCH_ID`

  The branch ID of the product. Default is `1`.

* `PRODUCT_BUILD_ID`

  The build ID of the product. Default is `918`.

* `PRODUCT_BUILD_TYPE`

  The build type of the product. Default is `50`.

* `PRODUCT_CORE_NAME`

  The product core name. Default is `UruLive`.

* `PRODUCT_SHORT_NAME`

  The product short name. Default is `UruLive`.

* `PRODUCT_LONG_NAME`

  The product's long name. Default is `Uru Live`.

* `PRODUCT_UUID`

  The product's UUID. Default is `ea489821-6c35-4bd0-9dae-bb17c585e680`.
