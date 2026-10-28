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

// Dependency link test of a wasm32-emscripten deps prefix (build-deps.sh's check step): touches
// every library of the prefix, built and linked with the client's global flags (-pthread
// -fwasm-exceptions -sSUPPORT_LONGJMP=wasm) and -sPROXY_TO_PTHREAD, and runs under node.
#include <cstdio>
#include <cstring>
#include <csetjmp>
#include <stdexcept>
#include <thread>
#include <atomic>

#include <asio.hpp>
#include <expat.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <jpeglib.h>
#include <ogg/ogg.h>
#include <png.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <vorbis/vorbisenc.h>
#include <opus.h>
#include <speex/speex.h>
#include <string_theory/string>
#include <string_theory/format>
#include <tl/expected.hpp>
#include <zlib.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <PxPhysicsAPI.h>
#include <Python.h>

static int failures = 0;
#define CHECK(cond, what) do { bool ok_ = (cond); std::printf("%s %s\n", ok_ ? "ok  " : "FAIL", what); if (!ok_) ++failures; } while (0)

static jmp_buf jb;
static void do_longjmp() { std::longjmp(jb, 42); }

static tl::expected<int, ST::string> half(int x) {
    if (x % 2) return tl::make_unexpected(ST_LITERAL("odd"));
    return x / 2;
}

int main(int argc, char** argv) {
    // C++ exceptions (wasm EH)
    bool caught = false;
    try { throw std::runtime_error("boom"); } catch (const std::exception& e) { caught = std::strcmp(e.what(), "boom") == 0; }
    CHECK(caught, "wasm exceptions: throw/catch");

    // setjmp/longjmp (wasm SjLj)
    volatile int jv = setjmp(jb);
    if (jv == 0) do_longjmp();
    CHECK(jv == 42, "wasm longjmp: setjmp/longjmp");

    // pthreads
    std::atomic<int> counter{0};
    std::thread t([&] { counter += 7; });
    t.join();
    CHECK(counter == 7, "pthreads: std::thread");

    // asio (header-only): post + run
    asio::io_context io;
    int posted = 0;
    asio::post(io, [&] { posted = 1; });
    io.run();
    CHECK(posted == 1, "asio io_context");

    XML_Parser xp = XML_ParserCreate(nullptr);
    CHECK(xp && XML_Parse(xp, "<a b='1'/>", 10, 1) == XML_STATUS_OK, "expat parse");
    XML_ParserFree(xp);

    FT_Library ft;
    CHECK(FT_Init_FreeType(&ft) == 0, "freetype init");
    FT_Done_FreeType(ft);

    jpeg_decompress_struct cinfo; jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    CHECK(true, "libjpeg-turbo create/destroy");

    ogg_sync_state oy;
    CHECK(ogg_sync_init(&oy) == 0, "libogg sync init");
    ogg_sync_clear(&oy);

    CHECK(png_access_version_number() >= 10600, "libpng version");

    vorbis_info vi; vorbis_info_init(&vi);
    CHECK(vorbis_encode_init_vbr(&vi, 2, 44100, 0.4f) == 0, "libvorbis/vorbisenc init");
    vorbis_info_clear(&vi);
    OggVorbis_File vf; std::memset(&vf, 0, sizeof(vf)); ov_clear(&vf);
    CHECK(true, "libvorbisfile ov_clear");

    int oerr = 0;
    OpusEncoder* oe = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &oerr);
    CHECK(oe && oerr == OPUS_OK, "opus encoder create");
    opus_encoder_destroy(oe);

    void* sp = speex_encoder_init(&speex_nb_mode);
    CHECK(sp != nullptr, "speex encoder init");
    speex_encoder_destroy(sp);

    ST::string s = ST::format("{}-{}", "plasma", 42);
    CHECK(s == "plasma-42", "string-theory format");
    CHECK(half(4).value() == 2 && !half(3), "tl-expected");

    const char* zin = "zlib zlib zlib zlib";
    unsigned char zout[128]; uLongf zlen = sizeof(zout);
    CHECK(compress(zout, &zlen, (const Bytef*)zin, std::strlen(zin)) == Z_OK, "zlib compress");

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    unsigned char md[EVP_MAX_MD_SIZE]; unsigned int mdlen = 0;
    EVP_Digest("abc", 3, md, &mdlen, EVP_sha256(), nullptr);
    CHECK(ctx && mdlen == 32 && md[0] == 0xba, "openssl SSL_CTX + sha256");
    SSL_CTX_free(ctx);

    // PhysX: foundation, physics, cooking, extensions, character controller, PVD
    static physx::PxDefaultAllocator alloc;
    static physx::PxDefaultErrorCallback err;
    physx::PxFoundation* fnd = PxCreateFoundation(PX_PHYSICS_VERSION, alloc, err);
    physx::PxPvd* pvd = physx::PxCreatePvd(*fnd);
    physx::PxPhysics* phys = PxCreatePhysics(PX_PHYSICS_VERSION, *fnd, physx::PxTolerancesScale(), true, pvd);
    physx::PxCooking* cook = PxCreateCooking(PX_PHYSICS_VERSION, *fnd, physx::PxCookingParams(physx::PxTolerancesScale()));
    bool ext = PxInitExtensions(*phys, pvd);
    physx::PxSceneDesc sd(phys->getTolerancesScale());
    sd.gravity = physx::PxVec3(0, 0, -9.8f);
    sd.cpuDispatcher = physx::PxDefaultCpuDispatcherCreate(1);
    sd.filterShader = physx::PxDefaultSimulationFilterShader;
    physx::PxScene* scene = phys->createScene(sd);
    physx::PxControllerManager* cm = PxCreateControllerManager(*scene);
    physx::PxMaterial* mat = phys->createMaterial(0.5f, 0.5f, 0.1f);
    physx::PxRigidDynamic* box = physx::PxCreateDynamic(*phys, physx::PxTransform(physx::PxVec3(0, 0, 10)),
                                                         physx::PxBoxGeometry(1, 1, 1), *mat, 1.0f);
    scene->addActor(*box);
    for (int i = 0; i < 60; ++i) { scene->simulate(1.0f / 60.0f); scene->fetchResults(true); }
    float z = box->getGlobalPose().p.z;
    std::printf("     physx box z after 1 s: %.3f\n", z);
    CHECK(fnd && phys && cook && ext && scene && cm && z < 6.0f && z > 4.0f, "physx simulate (falls ~4.9 m in 1 s)");
    cm->release(); scene->release(); PxCloseExtensions(); cook->release(); phys->release(); pvd->release(); fnd->release();

    // CPython: embedded interpreter with the builtin modules the client scripts use
    const char* home = argc > 1 ? argv[1] : nullptr;
    PyConfig cfg; PyConfig_InitIsolatedConfig(&cfg);
    if (home) PyConfig_SetBytesString(&cfg, &cfg.home, home);
    PyStatus st = Py_InitializeFromConfig(&cfg);
    PyConfig_Clear(&cfg);
    CHECK(!PyStatus_Exception(st), "python initialize");
    if (!PyStatus_Exception(st)) {
        int rc = PyRun_SimpleString(
            "import sys, _struct, math, time, _random, _json, zlib, _datetime, _sha1, _sha2, _sha3, _md5, "
            "binascii, itertools, _collections, _functools, _io, _weakref, marshal, _bz2, pyexpat, json, re, "
            "datetime, random, hashlib, struct\n"
            "assert zlib.decompress(zlib.compress(b'x'*100)) == b'x'*100\n"
            "assert hashlib.sha256(b'abc').hexdigest().startswith('ba7816bf')\n"
            "print('     python', sys.version.split()[0], sys.platform, 'modules ok')\n");
        CHECK(rc == 0, "python stdlib + builtin modules");
        Py_FinalizeEx();
    }

    std::printf("%s (%d failures)\n", failures ? "LINK TEST FAILED" : "LINK TEST PASSED", failures);
    return failures ? 1 : 0;
}
