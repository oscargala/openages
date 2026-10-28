# Third-party notices

This repository holds the CyanWorlds.com Engine (Plasma) client as the H-uru Plasma project maintains it, plus a
port of that client to web browsers, which runs on WebAssembly and WebGPU. The engine's own code is under the GNU
General Public License version 3 with the additional permissions in `COPYING.txt`. This file covers the code of other
projects that the clients are built with.

The browser client is built in two flavours:

- **plClient**, which embeds CPython 3.12.9;
- **plClient-py2**, which embeds CPython 2.3.3, for shards whose game code is written for Python 2.3.

Each flavour is one WebAssembly module and its JavaScript loader. Everything listed under "Browser client" below is
compiled or linked into them. A static page kit for hosting adds the page files, which belong to this project, and
the two fonts under "Page kit". The native clients that upstream Plasma builds for Windows, macOS and Linux use most of
the same libraries; "Native builds" lists the ones only they use.

Each entry gives the version, the licence, the builds it is part of, and how the build pins it:

- a record of `web/build/sources.lock`, which fixes every download of the browser build by URL and SHA-256, and every
  git input by commit;
- or a vcpkg port. The ports come from the vcpkg registry at the commit of the `vcpkg` record (also the root
  `vcpkg.json`'s `builtin-baseline`), or from `Scripts/Ports` in this repository. Each port checks the SHA-512 of the
  archive it downloads.

For every component of the browser client and the page kit, the licence text follows its entry. Each text was copied
from the pinned source itself; the line before it names the file in that source and the SHA-256 of the text as given
here (decoded as UTF-8, with LF line endings and without trailing blank lines). Copyright notices of individual
source files stay in the sources, which the pins identify.

## Summary

<!-- summary -->
| Component | Version | Licence | Builds | Pinned by |
|---|---|---|---|---|
| [OpenSSL](#openssl) | 3.6.3 | Apache-2.0 | browser, native | `vcpkg`, port `openssl` |
| [libjpeg-turbo](#libjpeg-turbo) | 3.2.0 | IJG AND BSD-3-Clause AND Zlib | browser, native | `vcpkg`, port `libjpeg-turbo` |
| [zlib](#zlib) | 1.3.2 | Zlib | browser, native | `vcpkg`, port `zlib` |
| [libpng](#libpng) | 1.6.58 | libpng-2.0 | browser, native | `vcpkg`, port `libpng` |
| [FreeType](#freetype) | 2.14.3 | FTL (chosen from FTL OR GPL-2.0-or-later), with MIT-style BDF and PCF drivers | browser, native | `vcpkg`, port `freetype` |
| [Brotli](#brotli) | 1.2.0 | MIT | browser, native | `vcpkg`, port `brotli` |
| [bzip2](#bzip2) | 1.0.8 | bzip2-1.0.6 | browser, native | `vcpkg`, port `bzip2` |
| [Expat](#expat) | 2.8.2 | MIT | browser, native | `vcpkg`, port `expat` |
| [libogg](#libogg) | 1.3.6 | BSD-3-Clause | browser, native | `vcpkg`, port `libogg` |
| [libvorbis](#libvorbis) | 1.3.7 | BSD-3-Clause | browser, native | `vcpkg`, port `libvorbis` |
| [Opus](#opus) | 1.5.2 | BSD-3-Clause | browser, native | `vcpkg`, port `opus` |
| [Speex](#speex) | 1.2.1 (commit 5dceaaf3) | BSD-3-Clause | browser, native | `vcpkg`, port `speex` |
| [NVIDIA PhysX SDK](#nvidia-physx-sdk) | 4.1.2 (commit 93c6dd21) | BSD-3-Clause | browser, native | overlay port `physx` |
| [libvpx](#libvpx) | 1.15.2 (browser), 1.16.0 (native) | BSD-3-Clause, with a patent grant | browser, native | `libvpx`; port `libvpx` (native) |
| [libwebm](#libwebm) | 1.0.0.32 | BSD-3-Clause, with a patent grant | browser, native | `libwebm`; port `libwebm` (native) |
| [Asio](#asio) | 1.32.0 | BSL-1.0 | browser, native | `vcpkg`, port `asio` |
| [string_theory](#string_theory) | 3.9 | MIT | browser, native | `vcpkg`, port `string-theory` |
| [tl::expected](#tlexpected) | 1.3.1 | CC0-1.0 | browser, native | `vcpkg`, port `tl-expected` |
| [CPython 3.12.9](#cpython-3129) | 3.12.9 | Python-2.0 | browser (plClient), native | `python-3.12.9`; overlay port `python3` (native) |
| [libmpdec](#libmpdec) | 2.5.1, in CPython 3.12.9 | BSD-2-Clause | browser (plClient), native | `python-3.12.9` |
| [HACL*](#hacl) | in CPython 3.12.9 | MIT | browser (plClient), native | `python-3.12.9` |
| [CPython 2.3.3](#cpython-233) | 2.3.3 | Python-2.0 (the licence of Python 2.3.3) | browser (plClient-py2) | `cypython-2.3.3`, `cypython-2.3.3-proof`, `cypython-2.3.3-cwe`, `cypython-2.3.3-ou-delta` |
| [Emscripten](#emscripten) | 6.0.12 | MIT OR NCSA | browser | `emsdk`, `emsdk-6.0.12-linux-x86_64` |
| [musl libc](#musl-libc) | 1.2.6, in Emscripten 6.0.12 | MIT | browser | `emsdk`, `emsdk-6.0.12-linux-x86_64` |
| [LLVM C++ runtime libraries](#llvm-c-runtime-libraries) | 22.1.8, in Emscripten 6.0.12 | Apache-2.0 WITH LLVM-exception | browser | `emsdk`, `emsdk-6.0.12-linux-x86_64` |
| [dlmalloc](#dlmalloc) | 2.8.6, in Emscripten 6.0.12 | CC0-1.0 | browser | `emsdk`, `emsdk-6.0.12-linux-x86_64` |
| [Emdawnwebgpu](#emdawnwebgpu) | v20260423.175430, in Emscripten 6.0.12 | (MIT OR NCSA) AND BSD-3-Clause | browser | `emsdk`, `emsdk-6.0.12-linux-x86_64` |
| [Liberation Mono](#liberation-mono) | 2.1.5 | OFL-1.1 | kit | file `web/deploy/fonts/LiberationMono-Regular.ttf` |
| [DejaVu Sans Bold](#dejavu-sans-bold) | 2.37 | Bitstream-Vera AND Arev; DejaVu changes public domain | kit | file `web/deploy/fonts/DejaVuSans-Bold.ttf` |
| [curl](#native-builds) | 8.21.0 | curl AND ISC AND BSD-3-Clause | native | `vcpkg`, port `curl` |
| [Fontconfig](#native-builds) | 2.17.1 | MIT | native | `vcpkg`, port `fontconfig` |
| [libarchive](#native-builds) | 3.8.7 | BSD-2-Clause | native | `vcpkg`, port `libarchive` |
| [libepoxy](#native-builds) | 1.5.10 | MIT | native | overlay port `libepoxy` |
| [OpenAL Soft](#native-builds) | 1.24.3 | LGPL-2.0-or-later | native | `vcpkg`, port `openal-soft` |
| [{fmt}](#native-builds) | 11.0.2 | MIT | native | `vcpkg`, port `fmt` |
| [libuuid](#native-builds) | 1.0.3 | BSD-3-Clause | native | `vcpkg`, port `libuuid` |
| [Emscripten SDK](#build-tools) | 6.0.12, with Node.js 24.19.0 | MIT OR NCSA; its LLVM, Binaryen and Node.js under their own licences | build | `emsdk`, `emsdk-6.0.12-linux-x86_64`, `emsdk-node-24.19.0-linux-x86_64` |
| [CMake](#build-tools) | 4.4.3 | BSD-3-Clause | build | `cmake-4.4.3-linux-x86_64` |
| [Ninja](#build-tools) | 1.13.2 | Apache-2.0 | build | `ninja-1.13.2-linux-x86_64` |
| [vcpkg](#files-from-other-projects) | tool 2026-07-27, registry 9e593bb1 | MIT | build | `vcpkg`, `vcpkg-tool-macos`, `vcpkg-tool-linux-x86_64`, `vcpkg-tool-linux-arm64` |
| [vcpkg helper ports](#build-tools) | 2024-04-23, 2026-07-21, 2025-05-29, 2026-07-09 | MIT | build | `vcpkg`, port `vcpkg-cmake`, port `vcpkg-cmake-config`, port `vcpkg-cmake-get-vars`, port `vcpkg-make` |
| [GNU M4](#build-tools) | 1.4.21 | GPL-3.0-or-later | build | `m4` |
| [GNU Autoconf](#build-tools) | 2.72 | GPL-3.0-or-later, with the Autoconf exception | build | `autoconf` |
| [GNU Automake](#build-tools) | 1.17 | GPL-2.0-or-later | build | `automake` |
| [GNU Libtool](#build-tools) | 2.6.2 | GPL-2.0-or-later, with the Libtool exception | build | `libtool` |
| [GNU Autoconf Archive](#build-tools) | 2024.10.16 | GPL-3.0-or-later, with the Autoconf exception | build | `autoconf-archive` |
| [CairoSVG and Pillow](#build-tools) | 2.9.0 and 12.2.0 | LGPL-3.0-or-later; MIT-CMU | build | overlay port `python3-cairosvg` |
<!-- /summary -->

Builds: **browser** is both browser flavours, **browser (plClient)** and **browser (plClient-py2)** one of them;
**kit** is the static page kit; **native** is the native clients and tools; **build** is a tool that only runs while
building and is not distributed.

## Browser client

The vcpkg ports below build the upstream release archive named in the port, with the port's own patches applied
(`ports/<name>/` of the vcpkg registry at the pinned commit; PhysX's port is in `Scripts/Ports/physx`).

### OpenSSL

The client's cryptography (`libcrypto` only; `libssl` is not linked). OpenSSL 3 is under the Apache License 2.0;
its source carries no NOTICE file.

@@TEXT openssl@@

### libjpeg-turbo

JPEG reading and writing (`libjpeg`, the libjpeg API). This software is based in part on the work of the Independent JPEG
Group. libjpeg-turbo's licence file names the licences of its parts; the IJG licence is the "LEGAL ISSUES" section of
its `README.ijg`, which follows it.

@@TEXT libjpeg-turbo@@

@@TEXT libjpeg-turbo-ijg@@

### zlib

Compression, used by the engine, libpng, FreeType and CPython 3.12.9.

@@TEXT zlib@@

### libpng

PNG reading and writing.

@@TEXT libpng@@

### FreeType

Font rendering. FreeType is offered under either the FreeType License (FTL) or the GPL version 2 or later; this
project uses it under the FTL. As the FTL requires:

Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

The build includes the BDF and PCF font drivers, which carry their own X11-style licences (below); the same terms
cover `src/base/fthash.c`, which came from the BDF driver. FreeType's gzip support uses zlib (above) instead of its
own copy, and the files taken from HarfBuzz are compiled only with HarfBuzz support, which this build does not
enable.

@@TEXT freetype@@

@@TEXT freetype-ftl@@

@@TEXT freetype-bdf@@

@@TEXT freetype-pcf@@

### Brotli

The Brotli decoder (`libbrotlidec`, `libbrotlicommon`), which FreeType uses for WOFF2 fonts.

@@TEXT brotli@@

### bzip2

Used by FreeType, for bzip2-compressed fonts, and by CPython 3.12.9's `bz2` module.

@@TEXT bzip2@@

### Expat

XML parsing, used by the engine's localisation manager and by CPython 3.12.9's `pyexpat`.

@@TEXT expat@@

### libogg

The Ogg container, under the Vorbis decoder.

@@TEXT libogg@@

### libvorbis

Vorbis audio decoding (`libvorbis`, `libvorbisfile`).

@@TEXT libvorbis@@

### Opus

The Opus codec, for voice chat and the sound of the game's movies. Its `LICENSE_PLEASE_READ.txt` points to the IETF patent
statements of Xiph.Org, Microsoft, Skype and Broadcom for the codec's patent licences.

@@TEXT opus@@

### Speex

The Speex codec, for voice chat.

@@TEXT speex@@

### NVIDIA PhysX SDK

Physics. Built by this repository's overlay port in `Scripts/Ports/physx`, from NVIDIA's release at commit
93c6dd21 with these patches applied:

- `force-suffixes.patch`, `apple-silicon.patch` and `fix-m1-mac.patch`: three commits of pull request 569 to
  NVIDIA's PhysX repository, which their authors contributed there (build suffixes, Apple Silicon support). They are
  not this project's work: upstream Plasma's port carries them, unchanged here.
- `fix-compiler-flag.patch`, `win-arm64.patch` and `remove-werror.patch`: build fixes from upstream Plasma's port,
  unchanged.
- `emscripten.patch`: this project's change for the browser build, which lets PhysX's Linux build configuration
  target wasm32 (scalar math, no CUDA, and a `size_t` overload of its hash function). It is offered under the same
  BSD 3-Clause licence as the PhysX files it changes, below.

The client links PhysX, PhysXCommon, PhysXFoundation, PhysXExtensions, PhysXCooking, PhysXCharacterKinematic and
PhysXPvdSDK.

@@TEXT physx@@

### libvpx

The VP9 decoder for the game's movies (VP8, the encoders, the tools and libyuv are disabled). The browser build
compiles release 1.15.2 from the archive the `libvpx` record pins; native builds use vcpkg's port.

@@TEXT libvpx@@

@@TEXT libvpx-patents@@

### libwebm

The WebM container parser for the game's movies. The browser build compiles the archive the `libwebm` record pins;
native builds use vcpkg's port of the same release.

@@TEXT libwebm@@

@@TEXT libwebm-patents@@

### Asio

Header-only networking and asynchronous I/O library, compiled into the engine's network core.

@@TEXT asio@@

### string_theory

Header-only string library, used throughout the engine.

@@TEXT string-theory@@

### tl::expected

Header-only `expected` type. It is dedicated to the public domain; the dedication follows.

@@TEXT tl-expected@@

### CPython 3.12.9

The Python interpreter of plClient, which runs a shard's Python 3 game code. It is built from the release archive
without changes, configured for Emscripten. The page kit carries no Python standard library: the client imports
what the shard's game code package provides.

@@TEXT cpython3@@

CPython's `Doc/license.rst` lists software that CPython incorporates. These sections apply to code this build
compiles: the Mersenne Twister in `_random`, SipHash in `Python/pyhash.c`, `Python/dtoa.c`, the hash table of
`Python/hashtable.c`, and `audioop`. libmpdec and HACL* have their own entries below. CPython's BLAKE2 code is
dedicated to the public domain (CC0 1.0). The sections on OpenSSL, libffi, the bundled Expat and zlib, kqueue,
getaddrinfo and the Python-only modules do not apply: this build compiles none of that code, and uses the Expat,
zlib and bzip2 entries above.

@@TEXT cpython3-mt@@

@@TEXT cpython3-siphash@@

@@TEXT cpython3-dtoa@@

@@TEXT cpython3-cfuhash@@

@@TEXT cpython3-audioop@@

### libmpdec

The decimal arithmetic library under CPython's `decimal` module (`libmpdec`), version 2.5.1 as CPython 3.12.9
bundles it in `Modules/_decimal/libmpdec`. Its licence, as CPython's `Doc/license.rst` gives it:

@@TEXT libmpdec@@

### HACL*

Formally verified hash functions (MD5, SHA-1, SHA-2 and SHA-3) under CPython's `hashlib`, from the HACL* project, as
CPython 3.12.9 bundles them in `Modules/_hacl` (`libHacl_Hash_SHA2` and the hash modules). Each file carries this
licence:

@@TEXT hacl@@

### CPython 2.3.3

The Python interpreter of plClient-py2, which runs game code written for Python 2.3. The source is the
`Cypython-2.3.3` tree of OpenUru's CWE-ou-minkata repository at commit 7978641e, which is Cyan Worlds' tree from
the 2011 CyanWorlds.com Engine release plus OpenUru's changes. The build rebuilds that exact tree from Cyan's
release (the first commit of the H-uru Plasma repository) and the vendored OpenUru delta, and checks it against the
pinned git tree id and file manifest.

CPython 2.3.3 is under its own licence, not the GPL: the Python Software Foundation licence for Python 2.3.3, which
includes the BeOpen, CNRI and CWI terms of earlier releases. The text follows. It is also in
`web/build/python2/LICENSE-PSF.txt`, and the build installs it with the library as `lib/python2.3/LICENSE.txt`.

As clause 3 of that licence asks of a derived work, these are this build's changes to Python 2.3.3:

- Cyan's 2011 tree, which differs from python.org's 2.3.3 release: Cyan's static-build configuration and project
  files, and code changes in 49 files;
- OpenUru's delta (`web/build/python2/cypython-ou-minkata.patch`; its preamble lists the changes);
- the four patches in `web/build/python2/patches/`, each of which explains itself;
- the Emscripten configuration in `web/build/python2/config.c` and `web/build/python2/pyconfig.h`, filled in from
  CPython's templates.

The patches, `config.c` and `pyconfig.h` are offered under the same Python 2.3.3 licence as the files they change or
derive from, so the whole CPython 2.3.3 tree stays under one licence. The scripts that build it are under the GPL.

@@TEXT cpython2@@

Two parts of the compiled tree carry their own notices: the PCRE regular expression code in `Modules/pypcre.c`
(unchanged by this build) and the Mersenne Twister in `Modules/_randommodule.c`.

@@TEXT cpython2-pcre@@

@@TEXT cpython2-mt@@

### Emscripten

The compiler toolchain of the browser build, whose runtime is part of the client: the JavaScript loader and runtime
(including Emscripten's OpenAL, HTML5, socket and GL libraries), WasmFS, the C start-up files and
Emscripten's parts of the C library. Its licence text below includes the MIT notice of the Node.js code in its path
library. Two files of this project contain code from Emscripten, as their headers say:
`Sources/Plasma/Apps/plClient/emscripten/plEmscriptenFetchBackend.js` (from `src/lib/libwasmfs_fetch.js`) and
`Sources/Plasma/Apps/plClient/emscripten/plEmscriptenOpenALLifecycle.js` (from `src/lib/libopenal.js`).

@@TEXT emscripten@@

### musl libc

The C library, as Emscripten 6.0.12 bundles it in `system/lib/libc/musl`.

@@TEXT musl@@

### LLVM C++ runtime libraries

libc++, libc++abi, libunwind and the compiler-rt builtins, from LLVM 22.1.8 as Emscripten 6.0.12 bundles them in
`system/lib`. The four licence files are the same text, apart from the name of the library in the "Legacy LLVM
License" section and compiler-rt's copyright years there (2009-2015 instead of 2009-2014). libc++'s follows; the
others are `system/lib/libcxxabi/LICENSE.TXT` (SHA-256 `@@SHA llvm-libcxxabi@@`),
`system/lib/libunwind/LICENSE.TXT` (SHA-256 `@@SHA llvm-libunwind@@`) and `system/lib/compiler-rt/LICENSE.TXT`
(SHA-256 `@@SHA llvm-compiler-rt@@`).

@@TEXT llvm-libcxx@@

### dlmalloc

The memory allocator, Doug Lea's malloc 2.8.6 as Emscripten bundles it in `system/lib/dlmalloc.c`, which is in the
public domain:

@@TEXT dlmalloc@@

### Emdawnwebgpu

Dawn's implementation of the `webgpu.h` C API on top of the browser's WebGPU, which the client's renderer uses.
Emscripten 6.0.12 pins the release archive by its SHA-512 (`tools/ports/emdawnwebgpu.py`) and downloads it on first
use. The JavaScript library and `webgpu.cpp` are under Emscripten's licence, as the package's `webgpu/src/LICENSE`
says; the `webgpu.h` header is under the BSD 3-Clause licence of the WebGPU native headers. The client does not use
the package's C++ bindings.

@@TEXT emdawnwebgpu@@

@@TEXT webgpu-headers@@

## Page kit

The client draws its debug and loading text with two faces, "Courier New" and "Trebuchet MS Bold", which it opens by
name. Both are Microsoft fonts that may not be redistributed, so the page kit serves these two free fonts under those
names instead. The files and their licence texts are in `web/deploy/fonts/`, copied unmodified from the projects'
release archives; a kit copies the licence texts next to the fonts. An operator who holds a licence for the original
faces can serve them instead, under that licence.

### Liberation Mono

Liberation Mono Regular, Version 2.1.5, from the Liberation Fonts 2.1.5 release
(https://github.com/liberationfonts/liberation-fonts/releases/tag/2.1.5), served as `Courier New.ttf`. It is not
Courier New. Digitized data copyright (c) 2010 Google Corporation; copyright (c) 2012 Red Hat, Inc. SIL Open Font
License, Version 1.1, with the Reserved Font Name "Liberation".

@@TEXT font-liberation@@

### DejaVu Sans Bold

DejaVu Sans Bold, Version 2.37, from the DejaVu Fonts 2.37 release
(https://github.com/dejavu-fonts/dejavu-fonts/releases/tag/version_2_37), served as `Trebuchet MS Bold.ttf`. It is not
Trebuchet MS Bold. Copyright (c) 2003 by Bitstream, Inc.; copyright (c) 2006 by Tavmjong Bah. The Bitstream Vera
Fonts licence and the Arev Fonts licence; the DejaVu changes are in the public domain.

@@TEXT font-dejavu@@

## Native builds

Native clients and tools build with vcpkg in manifest mode from the root `vcpkg.json`, at the same registry commit
as the browser build, so the libraries they share with it (above) have the same versions, except libvpx. These
libraries only native builds use:

- **curl** 8.21.0 (all platforms): curl AND ISC AND BSD-3-Clause;
- **Fontconfig** 2.17.1 (Linux): MIT;
- **libarchive** 3.8.7 (macOS): BSD-2-Clause; its `COPYING` names a few files under other permissive terms;
- **libepoxy** 1.5.10 (all platforms; this repository's overlay port): MIT;
- **OpenAL Soft** 1.24.3 (all platforms; held at that version by an override in `vcpkg.json`): LGPL-2.0-or-later.
  The browser client uses Emscripten's OpenAL implementation instead;
- **{fmt}** 11.0.2 (held by an override in `vcpkg.json` for OpenAL Soft): MIT;
- **libuuid** 1.0.3 (Linux): BSD-3-Clause;
- **CPython 3.12.9** (this repository's overlay port `python3`) and **libvpx** 1.16.0 instead of the browser's 1.15.2.

The licences are the ones the vcpkg ports declare; libarchive's port declares none, and the one given is that of
its `COPYING`. vcpkg installs each package's licence text as `share/<port>/copyright` in the build's install tree,
for these libraries and for the packages they depend on in turn.

## Build tools

These run only while building and are not part of any client or kit:

- **Emscripten SDK** 6.0.12: the compiler toolchain (Clang and LLVM, Binaryen, Node.js 24.19.0). Emscripten is
  under MIT OR NCSA; LLVM under Apache-2.0 WITH LLVM-exception; Binaryen under Apache-2.0; Node.js under the MIT
  licence.
- **CMake** 4.4.3 (BSD-3-Clause) and **Ninja** 1.13.2 (Apache-2.0), the versions the CI runner installs.
- **vcpkg**: the tool release 2026-07-27 and the registry at commit 9e593bb1, with its helper ports vcpkg-cmake
  2024-04-23, vcpkg-cmake-config 2026-07-21, vcpkg-cmake-get-vars 2025-05-29 and vcpkg-make 2026-07-09; MIT.
- **GNU M4** 1.4.21, **Autoconf** 2.72, **Automake** 1.17, **Libtool** 2.6.2 and the **Autoconf Archive**
  2024.10.16, which the Speex port needs to regenerate its build scripts. They are under the GNU GPL, with the
  exceptions their projects grant for generated scripts and macros.
- **CairoSVG** 2.9.0 and **Pillow** 12.2.0, installed by the optional `cairosvg` feature of `vcpkg.json` into the
  build's Python to render the native client's `resource.dat`.

## Files from other projects

These files of this repository contain code from the projects above, under those projects' licences:

- `Scripts/Triplets/wasm32-emscripten-plasma.cmake`, the vcpkg triplet of the browser build, is vcpkg's
  `triplets/community/wasm32-emscripten.cmake` with this build's compiler flags added. vcpkg's licence follows.
- `Sources/Plasma/Apps/plClient/emscripten/plEmscriptenFetchBackend.js` and
  `Sources/Plasma/Apps/plClient/emscripten/plEmscriptenOpenALLifecycle.js` contain code from Emscripten (see
  [Emscripten](#emscripten)).
- `web/build/python2/` holds CPython 2.3.3's licence, its build configuration, OpenUru's delta and this build's
  patches (see [CPython 2.3.3](#cpython-233)).
- `Scripts/Ports/physx/` holds patches to PhysX: three commits of its pull request 569, upstream Plasma's build
  fixes, and this project's `emscripten.patch` (see [NVIDIA PhysX SDK](#nvidia-physx-sdk)).
- `web/deploy/fonts/` holds the two fonts and their licences (see [Page kit](#page-kit)).

@@TEXT vcpkg@@

## Not included

Neither this repository nor a page kit contains Myst Online: Uru Live game data, which a shard or its file server
provides under Cyan Worlds' terms; nor the high-resolution texture packs that some deployments serve separately,
under their own licence; nor fonts an operator supplies.
