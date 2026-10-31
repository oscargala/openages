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

#include <algorithm>
#include <deque>
#include <mutex>

#include "pfPatcher.h"

#include "HeadSpin.h"
#include "plFileSystem.h"
#include "hsStream.h"
#include "hsThread.h"
#include "hsTimer.h"

#include "pnEncryption/plChecksum.h"
#include "pnNetBase/pnNbError.h"

#include "plAudioCore/plAudioFileReader.h"
#include "plCompression/plZlibStream.h"
#include "plNetGameLib/plNetGameLib.h"
#include "plStatusLog/plStatusLog.h"

#ifdef __EMSCRIPTEN__
#include <sys/stat.h>
#include <zlib.h>

#include <atomic>
#include <cerrno>
#include <cstdio>

// The browser storage budget the application sets (pfPatcher::SetStorageBudget), shared by every patcher. Without one,
// no manifest is checked against the storage before its files are fetched.
static pfPatcher::StorageBudgetFunc s_storageBudget;
// The browser's count of failed writes per install-directory file (pfPatcher::SetWriteFailures), shared by every
// patcher. Without one, no download's size mismatch is a storage failure.
static pfPatcher::WriteFailuresFunc s_writeFailures;
// How many times a download that is not at its manifest size after Close, with no write of its file failed and not
// arrived complete, is fetched again before its patch fails (pfPatcherWorker::IFileThingDownloadCB). A download that
// arrived complete (every delivered byte, or every byte of a verified gzip stream, on disk) is kept, as native.
static constexpr uint32_t           kDownloadRetries = 2;
// One chunk of a download as the file server delivered it, a Write of pfPatcherStream
struct pfPatcherDownloadChunk
{
    uint32_t index;
    uint64_t offset; // its first byte's offset in the delivered (compressed) stream
    uint32_t size;
};
#ifdef PLASMA_TEST_HOOKS
// The harness's test faults (pfPatcher::SetTestDownloadFaults), set before any patcher starts.
static plFileName        s_testSizeFile;
static int32_t           s_testSizeDelta = 0;
static plFileName        s_testDropFile;
static std::atomic<bool> s_testDropArmed{false};
static bool              s_testDropEvery = false;
static bool              s_testDropCorrupt = false; // invert a byte of that payload, not drop it
#endif
#endif

template<typename... _Args>
static inline void PatcherLogGreen(const char* format, _Args&&... args)
{
    pfPatcher::GetLog()->AddLineF(plStatusLog::kGreen, format, std::forward<_Args>(args)...);
}

template<typename... _Args>
static inline void PatcherLogRed(const char* format, _Args&&... args)
{
    pfPatcher::GetLog()->AddLineF(plStatusLog::kRed, format, std::forward<_Args>(args)...);
}

template<typename... _Args>
static inline void PatcherLogWhite(const char* format, _Args&&... args)
{
    pfPatcher::GetLog()->AddLineF(plStatusLog::kWhite, format, std::forward<_Args>(args)...);
}

template<typename... _Args>
static inline void PatcherLogYellow(const char* format, _Args&&... args)
{
    pfPatcher::GetLog()->AddLineF(plStatusLog::kYellow, format, std::forward<_Args>(args)...);
}

// ===================================================

/** Human readable file flags */
enum FileFlags
{
    // Sound files only
    kSndFlagCacheSplit          = 1<<0,
    kSndFlagStreamCompressed    = 1<<1,
    kSndFlagCacheStereo         = 1<<2,

    // Any file
    kFlagZipped                 = 1<<3,

    // Executable flags
    kRedistUpdate               = 1<<4,
    kBundle                     = 1<<5,

    // Begin internal flags
    kLastManifestFlag           = 1<<6,
    kSelfPatch                  = 1<<7,
};

// ===================================================

struct pfPatcherQueuedFile
{
    enum class Type
    {
        kManifestHash,
        kSoundDecompress,
    };

    Type fType;
    plFileName fClientPath;
    plFileName fServerPath;
    plChecksum fChecksum;
    uint32_t fFileSize;
    uint32_t fZipSize;
    uint32_t fFlags;

    pfPatcherQueuedFile(Type t, const NetCliFileManifestEntry& file)
        : fType(t), fClientPath(plFileName(ST::string::from_utf16(file.clientName)).Normalize()),
          fServerPath(ST::string::from_utf16(file.downloadName)), fChecksum(plChecksum::Type::kMD5),
          fFileSize(file.fileSize), fZipSize(file.zipSize), fFlags(file.flags)
    {
        ST::string temp(file.md5, std::size(file.md5));
        fChecksum.SetFromHexString(temp.c_str());
    }

    pfPatcherQueuedFile(Type t, plFileName path, uint32_t flags=0)
        : fType(t), fClientPath(std::move(path)), fChecksum(plChecksum::Type::kMD5),
          fFileSize(), fZipSize(), fFlags(flags)
    { }

    pfPatcherQueuedFile(const pfPatcherQueuedFile& copy) = delete;

    pfPatcherQueuedFile& operator =(const pfPatcherQueuedFile& copy) = delete;
};

// ===================================================

/** State for a patcher worker thread started by pfPatcher::Start */
struct pfPatcherWorker final
{
    /** Represents a File/Auth download request */
    struct Request
    {
        enum { kFile, kManifest, kSecurePreloader, kAuthFile, kPythonList, kSdlList };

        ST::string fName;
        uint8_t fType;
        class pfPatcherStream* fStream;

        Request(const ST::string& name, uint8_t type, class pfPatcherStream* s=nullptr) :
            fName(name), fType(type), fStream(s)
        { }
    };

    std::deque<Request> fRequests;
    std::deque<pfPatcherQueuedFile> fQueuedFiles;

    std::recursive_mutex fRequestMut;
    std::mutex fFileMut;
    hsSemaphore fFileSignal;

    pfPatcher::CompletionFunc fOnComplete;
    pfPatcher::FindBundleExeFunc fFindBundleExe;
    pfPatcher::FileDownloadFunc fFileBeginDownload;
    pfPatcher::FileDesiredFunc fFileDownloadDesired;
    pfPatcher::FileDownloadFunc fFileDownloaded;
    pfPatcher::GameCodeDiscoverFunc fGameCodeDiscovered;
    pfPatcher::ProgressTickFunc fProgressTick;
    pfPatcher::FileDownloadFunc fRedistUpdateDownloaded;
    pfPatcher::FileDownloadFunc fSelfPatch;

    volatile bool fStarted;
    volatile bool fRequestActive;
    volatile bool fWantPython;
    volatile bool fWantSDL;

    uint64_t fCurrBytes;
    uint64_t fTotalBytes;

#ifdef __EMSCRIPTEN__
    // the storage this patch may still use (taken at its first manifest) and the bytes its manifests so far will write
    int64_t  fBrowserAvailable = -2;
    uint64_t fBrowserNeeded = 0;
#endif

    pfPatcherWorker();
    ~pfPatcherWorker();

    void IAuthThingDownloadCB(ENetError result, const plFileName& filename, hsStream* writer);
    void IGotAuthFileList(ENetError result, const std::vector<NetCliAuthFileInfo>& infos);
    void IHandleManifestDownload(const ST::string& group, const std::vector<NetCliFileManifestEntry>& manifest);
    void IPreloaderManifestDownloadCB(ENetError result, const ST::string& group, const std::vector<NetCliFileManifestEntry>& manifest);
    void IFileManifestDownloadCB(ENetError result, const ST::string& group, const std::vector<NetCliFileManifestEntry>& manifest);
    void IFileThingDownloadCB(ENetError result, const plFileName& filename, pfPatcherStream* stream);

    void EndPatch(ENetError result, const ST::string& msg={});
    bool IssueRequest();
    void Run();
    void IHashFile(pfPatcherQueuedFile& file);
    void IDecompressSound(const pfPatcherQueuedFile& sound) const;
    void ProcessFile();
    void WhitelistFile(const plFileName& file, bool justDownloaded, hsStream* s=nullptr);
    void EnqueuePreloaderLists();
};

// ===================================================

class pfPatcherStream : public plZlibStream
{
    pfPatcherWorker* fParent;
    plFileName fFilename;
    uint32_t fFlags;

    uint64_t fBytesWritten;
    float fDLStartTime;

    ST::string IMakeStatusMsg() const
    {
        float secs = hsTimer::GetSeconds<float>() - fDLStartTime;
        auto bytesPerSec = uint64_t(fBytesWritten / secs);
        return plFileSystem::ConvertFileSize(bytesPerSec) + "/s";
    }

    void IUpdateProgress(uint32_t count)
    {
        fBytesWritten += count; // just this file
        fParent->fCurrBytes += count; // the entire everything

        // tick-tick-tick, tick-tick-tock
        if (fParent->fProgressTick)
            fParent->fProgressTick(fParent->fCurrBytes, fParent->fTotalBytes, IMakeStatusMsg());
    }

public:
    pfPatcherStream(pfPatcherWorker* parent, const plFileName& filename, uint64_t size)
        : fParent(parent), fFilename(filename), fFlags(), fBytesWritten(), fDLStartTime(), plZlibStream()
    {
        fParent->fTotalBytes += size;
        fOutput = std::make_unique<hsRAMStream>();
    }

    pfPatcherStream(pfPatcherWorker* parent, const pfPatcherQueuedFile& file)
        : fParent(parent), fFilename(file.fClientPath.Normalize()), fFlags(file.fFlags), fBytesWritten(), fDLStartTime(), plZlibStream()
    {
#ifdef __EMSCRIPTEN__
        fExpectedSize = file.fFileSize;
#ifdef PLASMA_TEST_HOOKS
        if (s_testSizeFile.IsValid() && fFilename == s_testSizeFile)
            fExpectedSize += s_testSizeDelta;
#endif
#endif
        // ugh. eap removed the compressed flag in his fail manifests
        if (file.fServerPath.GetFileExt().compare_i("gz") == 0) {
            fFlags |= kFlagZipped;
            parent->fTotalBytes += file.fZipSize;
#ifdef __EMSCRIPTEN__
            fDeliverSize = file.fZipSize;
#endif
        } else {
            parent->fTotalBytes += file.fFileSize;
#ifdef __EMSCRIPTEN__
            fDeliverSize = file.fFileSize;
#endif
        }
    }

#ifdef __EMSCRIPTEN__
    // The next attempt of a download whose file is not at the manifest size (pfPatcherWorker::IFileThingDownloadCB): a
    // new stream, because plZlibStream resets its inflate state only in Rewind (plZlibStream.cpp:168-174). It keeps
    // the file, its flags (kFlagZipped already among them) and its sizes. It adds nothing to the patch's total bytes,
    // which still hold the first attempt's.
    pfPatcherStream(pfPatcherWorker* parent, const pfPatcherStream& failed)
        : plZlibStream(), fParent(parent), fFilename(failed.fFilename), fFlags(failed.fFlags), fBytesWritten(), fDLStartTime(), fExpectedSize(failed.fExpectedSize), fDeliverSize(failed.fDeliverSize), fAttempt(failed.fAttempt + 1)
    {
    }
#endif

    void Begin()
    {
        fDLStartTime = hsTimer::GetSeconds<float>();
#ifdef __EMSCRIPTEN__
        // what this download starts from, for the size line of a mismatch
        fSizeAtBegin = plFileInfo(fFilename).FileSize();
        fWriteFailuresAtBegin = s_writeFailures ? s_writeFailures(fFilename) : 0;
#endif
        if (!fOutput)
            Open(fFilename, "wb");
    }

    bool Open(const plFileName& filename, const char* mode) override
    {
        hsAssert(filename == fFilename, "trying to save to a different file, eh?");
        bool retVal = plZlibStream::Open(filename, mode);
        if (!retVal)
            PatcherLogRed("\tPhailed to open %s: '%s'", filename.AsString().c_str(), strerror(errno));
        return retVal;
    }

    void Close()
    {
#ifdef __EMSCRIPTEN__
        // what the inflater produced and consumed, for the size check after Close: plZlibStream::Close deletes the
        // z_stream (plZlibStream.cpp:77-82). uLong is 32 bits on wasm32, so the counts are kept widened.
        if (hsCheckBits(fFlags, kFlagZipped) && fZStream) {
            const z_streamp zstream = static_cast<z_streamp>(fZStream);
            fInflatedOut = int64_t(uint64_t(zstream->total_out));
            fInflatedIn = int64_t(uint64_t(zstream->total_in));
        } else {
            fInflatedOut = -1;
            fInflatedIn = -1;
        }
#endif
        if (hsCheckBits(fFlags, kFlagZipped))
            plZlibStream::Close();
        fOutput.reset();
    }

    uint32_t Write(uint32_t count, const void* buf) override
    {
        // tick whatever progress bar we have
        IUpdateProgress(count);

#ifdef __EMSCRIPTEN__
        // Each Write is one chunk of the file server's (RcvdFileDownloadChunkTrans::Post,
        // plNglFile.cpp), at this compressed offset
        const pfPatcherDownloadChunk chunk{fChunks++, fBytesWritten - count, count};
        fLastChunk = chunk;
#endif

#if defined(__EMSCRIPTEN__) && defined(PLASMA_TEST_HOOKS)
        // the harness's dropped tail: the first attempt's last payload never reaches the file, or every attempt's
        // or, with s_testDropCorrupt, reaches it with its middle byte inverted
        std::vector<uint8_t> corrupted;
        if (s_testDropFile.IsValid() && fFilename == s_testDropFile && fBytesWritten >= fDeliverSize &&
            (s_testDropEvery || (fAttempt == 0 && s_testDropArmed.exchange(false)))) {
            if (!s_testDropCorrupt || !count)
                return count;
            corrupted.assign((const uint8_t*)buf, (const uint8_t*)buf + count);
            corrupted[count / 2] ^= 0xFF;
            buf = corrupted.data();
        }
#endif

        // write the appropriate blargs
        if (hsCheckBits(fFlags, kFlagZipped))
#ifdef __EMSCRIPTEN__
            return IWriteZipped(chunk, count, buf);
#else
            return plZlibStream::Write(count, buf);
#endif
        else
            return fOutput->Write(count, buf);
    }

    bool AtEnd() override { return fOutput->AtEnd(); }
    uint32_t GetEOF() override { return fOutput->GetEOF(); }
    uint32_t GetPosition() const override { return fOutput->GetPosition(); }
    uint32_t Read(uint32_t count, void* buf) override { return fOutput->Read(count, buf); }
    void Rewind() override { fOutput->Rewind(); }
    void FastFwd() override { fOutput->FastFwd(); }
    void SetPosition(uint32_t pos) override { fOutput->SetPosition(pos); }
    void Skip(uint32_t deltaByteCount) override { fOutput->Skip(deltaByteCount); }

    uint32_t GetFlags() const { return fFlags; }
    plFileName GetFileName() const { return fFilename; }
    bool IsRedistUpdate() const { return hsCheckBits(fFlags, kRedistUpdate); }
    bool IsSelfPatch() const { return hsCheckBits(fFlags, kSelfPatch); }
    bool RequiresSfxCache() const { return hsCheckBits(fFlags, kSndFlagCacheSplit) || hsCheckBits(fFlags, kSndFlagCacheStereo); }
    void Unlink() const { plFileSystem::Unlink(fFilename); }

#ifdef __EMSCRIPTEN__
    // A file download whose file is not on disk at the manifest size after Close. Native has no size check: it
    // ignores the write result (RcvdFileDownloadChunkTrans::Post in plNglFile.cpp), keeps the file, and the next patch
    // of its manifest (the next link to the Age: plAgeLoader::UpdateAge, plResPatcher::Update) hashes it in IHashFile
    // and downloads it again. In the browser a failed patch ends the game, so pfPatcherWorker::IFileThingDownloadCB
    // fails the download for storage when a write of its file failed (plEmscriptenStdio.cpp removes such a file at
    // fclose); keeps it, as native, when it arrived complete (ArrivedComplete: the manifest size is what differs, and
    // since IHashFile compares that size first, the file is downloaded again, and kept again, at every later link to
    // its Age, as natively); and otherwise fetches it again, as that next patch would.
    int64_t  fExpectedSize = -1;
    uint32_t fDeliverSize = 0;          // the bytes the file server sends: the zip size, or the file size
    uint32_t fAttempt = 0;              // 0 for the first download, then 1 per retry
    int64_t  fSizeAtBegin = -1;         // the file's size before Begin's fopen, -1 when absent
    uint32_t fWriteFailuresAtBegin = 0; // the failed writes of this file before Begin (s_writeFailures)
    int64_t  fInflatedOut = -1;         // a zipped file's inflated bytes (z_stream total_out) at Close, -1 for none
    int64_t  fInflatedIn = -1;          // the compressed bytes the inflater consumed (total_in) at Close, -1 for none

    // The chunks Write was handed, and the one in which inflate failed
    uint32_t               fChunks = 0;
    pfPatcherDownloadChunk fLastChunk{0, 0, 0};
    pfPatcherDownloadChunk fFailChunk{0, 0, 0};
    int64_t                fFailIn = -1; // the compressed bytes the inflater had consumed when it failed, -1 for none

    // plZlibStream::Write, noting where inflate failed for the size line
    uint32_t IWriteZipped(const pfPatcherDownloadChunk& chunk, uint32_t count, const void* buf)
    {
        const bool     failedBefore = fErrorOccurred;
        const uint32_t written = plZlibStream::Write(count, buf);
        if (!failedBefore && fErrorOccurred) {
            fFailChunk = chunk;
            fFailIn = fZStream ? int64_t(uint64_t(static_cast<z_streamp>(fZStream)->total_in)) : -1;
        }
        return written;
    }

    // The file's size on disk now, or -1 with the stat errno
    int64_t SizeOnDisk(int& error) const
    {
        struct stat info;
        if (stat(fFilename.AsString().c_str(), &info) != 0) {
            error = errno;
            return -1;
        }
        error = 0;
        return info.st_size;
    }
    bool     Mismatched(int64_t onDisk) const { return fExpectedSize >= 0 && onDisk != fExpectedSize; }
    // a write of this file failed during this attempt (the stdio wrapper removed it)
    bool     WriteFailed() const { return s_writeFailures && s_writeFailures(fFilename) != fWriteFailuresAtBegin; }
    uint32_t Attempt() const { return fAttempt; }
    uint64_t BytesWritten() const { return fBytesWritten; }

    // The download arrived complete, so it is kept as native keeps it: zipped, the gzip stream ended with its CRC-32
    // and length checked (inflate's Z_STREAM_END with windowBits 31; an error sets fErrorOccurred,
    // plZlibStream.cpp:129-135) and every inflated byte is on disk, since plZlibStream::Write hands each to
    // fOutput (:138-139); unzipped, every delivered byte is on disk.
    bool ArrivedComplete(int64_t onDisk) const
    {
        if (onDisk < 0)
            return false;
        if (hsCheckBits(fFlags, kFlagZipped))
            return !fErrorOccurred && fDecompressedOk && onDisk == fInflatedOut;
        return uint64_t(onDisk) == fBytesWritten;
    }
    // The outcome of a download kept by ArrivedComplete
    ST::string KeptOutcome() const
    {
        if (hsCheckBits(fFlags, kFlagZipped))
            return ST::format(
                "kept, as native: the gzip stream ended and verified, and all {} inflated bytes are on "
                "disk; the manifest size differs",
                fInflatedOut);
        return ST::format("kept, as native: all {} delivered bytes are on disk; the manifest size differs",
                          fBytesWritten);
    }

    // The line of a mismatch: what was expected, delivered and found
    ST::string SizeLine(int64_t onDisk, int error, const ST::string& outcome) const
    {
        const ST::string disk = onDisk >= 0 ? ST::format("{} B", onDisk) : ST::format("absent (errno {})", error);
        const ST::string before = fSizeAtBegin >= 0 ? ST::format("{} B", fSizeAtBegin) : ST_LITERAL("absent");
        const ST::string inflated =
            fInflatedOut >= 0 ? ST::format("inflated {} B from {} of {} delivered B", fInflatedOut, fInflatedIn,
                                           fBytesWritten)
                              : ST_LITERAL("no inflate stream");
        // Where inflate failed (its compressed offset, and the chunk that held it), or the last chunk
        const ST::string chunks =
            fFailIn >= 0 ? ST::format("; inflate failed at compressed byte {}, in chunk {} of {} (offset {}, {} B)",
                                      fFailIn, fFailChunk.index + 1, fChunks, fFailChunk.offset, fFailChunk.size)
            : fChunks    ? ST::format("; {} chunks, the last at offset {}, {} B", fChunks, fLastChunk.offset,
                                      fLastChunk.size)
                         : ST_LITERAL("; no chunk");
        const ST::string inflate =
            hsCheckBits(fFlags, kFlagZipped)
                ? ST::format("error {}, stream end {}, {}{}", fErrorOccurred ? "yes" : "no",
                             fDecompressedOk ? "yes" : "no", inflated, chunks)
                : ST_LITERAL("n/a");
        const uint32_t failures = s_writeFailures ? s_writeFailures(fFilename) : 0;
        return ST::format(
            "Size mismatch: '{}' attempt {}: on disk {}, manifest {} B; before the download {}; "
            "delivered {} of {} B; inflate {}; write failures of the file {} -> {}: {}",
            fFilename, fAttempt + 1, disk, fExpectedSize, before, fBytesWritten, fDeliverSize, inflate,
            fWriteFailuresAtBegin, failures, outcome);
    }
#endif
};

// ===================================================

void pfPatcherWorker::IAuthThingDownloadCB(ENetError result, const plFileName& filename, hsStream* writer)
{
    if (IS_NET_SUCCESS(result)) {
        PatcherLogGreen("\tDownloaded Legacy File '{}'", filename);
        IssueRequest();

        // Now, we pass our RAM-backed file to the game code handlers. In the main client,
        // this will trickle down and add a new friend to plStreamSource. This should never
        // happen in any other app...
        writer->Rewind();
        WhitelistFile(filename, true, writer);
    } else {
        PatcherLogRed("\tDownloaded Failed: File '{}'", filename);
        EndPatch(result, filename.AsString());
    }
}

void pfPatcherWorker::IGotAuthFileList(ENetError result, const std::vector<NetCliAuthFileInfo>& infos)
{
    if (IS_NET_SUCCESS(result)) {
        // so everything goes directly into the Requests deque because AuthSrv lists
        // don't have any hashes attached. WHY did eap think this was a good idea?!?!
        {
            hsLockGuard(fRequestMut);
            for (const auto& info : infos) {
                PatcherLogYellow("\tEnqueuing Legacy File '{}'", info.filename);

                plFileName fn = ST::string::from_utf16(info.filename);
                plFileSystem::CreateDir(fn.StripFileName());

                // We purposefully do NOT Open this stream! This uses a special auth-file constructor that
                // utilizes a backing hsRAMStream. This will be fed to plStreamSource later...
                pfPatcherStream* s = new pfPatcherStream(this, fn, info.filesize);
                fRequests.emplace_back(fn.AsString(), Request::kAuthFile, s);
            }
        }
        IssueRequest();
    } else {
        PatcherLogRed("\tSHIT! Some legacy manifest phailed");
        EndPatch(result, "SecurePreloader failed");
    }
}

void pfPatcherWorker::IHandleManifestDownload(const ST::string& group, const std::vector<NetCliFileManifestEntry>& manifest)
{
    PatcherLogGreen("\tDownloaded Manifest '{}'", group);
#ifdef __EMSCRIPTEN__
    // Before this manifest's files are hashed and fetched: do they fit the browser's storage? The
    // budget is asked once per patch; each manifest adds its full size, or, when that does not fit, the size of the
    // files not already present at the manifest size (what IHashFile would enqueue at most). Sound-cache expansion
    // (IDecompressSound) is not known from the manifest and is not counted. A shortfall ends the patch through
    // EndPatch, as a failed manifest download does (IFileManifestDownloadCB), before any file is written.
    if (s_storageBudget) {
        if (fBrowserAvailable == -2)
            fBrowserAvailable = s_storageBudget();
        uint64_t total = 0;
        for (const auto& entry : manifest)
            total += entry.fileSize;
        if (fBrowserAvailable >= 0 && fBrowserNeeded + total > uint64_t(fBrowserAvailable)) {
            total = 0;
            for (const auto& entry : manifest) {
                plFileName path = plFileName(ST::string::from_utf16(entry.clientName)).Normalize();
                if (plFileInfo(path).FileSize() != int64_t(entry.fileSize))
                    total += entry.fileSize;
            }
        }
        fBrowserNeeded += total;
        PatcherLogWhite("\tBrowser storage: '{}' needs {} (patch so far {}), available {}", group,
                        plFileSystem::ConvertFileSize(total), plFileSystem::ConvertFileSize(fBrowserNeeded),
                        fBrowserAvailable >= 0 ? plFileSystem::ConvertFileSize(fBrowserAvailable) : ST_LITERAL("unknown"));
        if (fBrowserAvailable >= 0 && fBrowserNeeded > uint64_t(fBrowserAvailable)) {
            PatcherLogRed("\tNot enough browser storage for Manifest '{}'", group);
            EndPatch(kNetErrInternalError, ST::format("Not enough browser storage for {}: {} needed, {} available", group,
                                                      plFileSystem::ConvertFileSize(fBrowserNeeded),
                                                      plFileSystem::ConvertFileSize(fBrowserAvailable)));
            return;
        }
    }
#endif
    {
        hsLockGuard(fFileMut);
        for (const auto& entry : manifest)
            fQueuedFiles.emplace_back(pfPatcherQueuedFile::Type::kManifestHash, entry);
        fFileSignal.Signal();
    }
    IssueRequest();
}

void pfPatcherWorker::IPreloaderManifestDownloadCB(ENetError result, const ST::string& group, const std::vector<NetCliFileManifestEntry>& manifest)
{
    if (IS_NET_SUCCESS(result)) {
        IHandleManifestDownload(group, manifest);
    } else {
        EnqueuePreloaderLists();
        IssueRequest();
    }
}

void pfPatcherWorker::IFileManifestDownloadCB(ENetError result, const ST::string& group, const std::vector<NetCliFileManifestEntry>& manifest)
{
    if (IS_NET_SUCCESS(result))
        IHandleManifestDownload(group, manifest);
    else {
        PatcherLogRed("\tDownload Failed: Manifest '{}'", group);
        EndPatch(result, group);
    }
}

void pfPatcherWorker::IFileThingDownloadCB(ENetError result, const plFileName& filename, pfPatcherStream* stream)
{
    // We need to explicitly close any underlying streams NOW because we
    // might be about to signal the client that this file needs to be acted
    // on, eg installed, decompressed from ogg to wave, etc. We can't wait
    // for hsStream's RAII to close the stream at the end of this function or
    // the callback code may crash due to either a permissions error or the
    // zlib decompression not being complete.
    stream->Close();

    // the message that ends the patch if this download failed
    ST::string failure = filename.AsString();
#ifdef __EMSCRIPTEN__
    // A download the file server completed whose file is not at the manifest size (pfPatcherStream above): a failed
    // write fails it for storage, a download that arrived complete is kept as native, and any other is fetched again.
    // A failed download keeps native's branch below. Each mismatch writes one line to the patcher log and the console.
    int           error = 0;
    const int64_t onDisk = IS_NET_SUCCESS(result) ? stream->SizeOnDisk(error) : -1;
    if (IS_NET_SUCCESS(result) && stream->Mismatched(onDisk)) {
        const auto logMismatch = [&](const ST::string& outcome, bool kept = false) {
            const ST::string line = stream->SizeLine(onDisk, error, outcome);
            if (kept)
                PatcherLogYellow("\t{}", line);
            else
                PatcherLogRed("\t{}", line);
            std::printf("patcher: %s\n", line.c_str());
            std::fflush(stdout);
        };
        if (stream->WriteFailed()) {
            logMismatch(ST_LITERAL("a write failed"));
            result = kNetErrInternalError;
            failure = ST::format("Not enough browser storage to write {}", stream->GetFileName());
        } else if (stream->ArrivedComplete(onDisk)) {
            // Every byte the file server sent (a zipped file: every byte of its verified gzip stream) is on disk, so
            // only the manifest size differs. Native keeps the file: the result stays a success and the native block
            // below whitelists it. The next patch of the manifest downloads it again (IHashFile compares the size
            // first), as natively, and this line repeats.
            logMismatch(stream->KeptOutcome(), true);
        } else if (stream->Attempt() < kDownloadRetries && fStarted) {
            // Fetched again at once, as the next patch of this manifest would natively (IHashFile finds no file and
            // enqueues it), in a new stream at the front of the requests, so one request stays in flight. The
            // patch's progress takes back this attempt's bytes. The begin callback fires again for the retry (the
            // launcher step counts it among its downloaded files). Not when the patch has ended (fStarted false).
            logMismatch(ST::format("downloading it again ({} of {})", stream->Attempt() + 1, kDownloadRetries));
            stream->Unlink();
            pfPatcherStream* retry = new pfPatcherStream(this, *stream);
            fCurrBytes -= stream->BytesWritten();
            {
                hsLockGuard(fRequestMut);
                fRequests.emplace_front(filename.AsString(), Request::kFile, retry);
                IssueRequest();
            }
            delete stream;
            return;
        } else {
            logMismatch(ST_LITERAL("the download failed"));
            result = kNetErrInternalError;
            if (onDisk >= 0)
                failure = ST::format(
                    "Download of {} did not complete ({} bytes on disk, {} expected). Reload the page "
                    "to try again.",
                    stream->GetFileName(), onDisk, stream->fExpectedSize);
            else
                failure = ST::format(
                    "Download of {} did not complete (the file is missing after download). Reload "
                    "the page to try again.",
                    stream->GetFileName());
        }
    }
#endif
    if (IS_NET_SUCCESS(result)) {
        PatcherLogGreen("\tDownloaded File '{}'", stream->GetFileName());
        WhitelistFile(stream->GetFileName(), true);
        if (fSelfPatch && stream->IsSelfPatch())
            fSelfPatch(stream->GetFileName());
        if (fRedistUpdateDownloaded && stream->IsRedistUpdate())
            fRedistUpdateDownloaded(stream->GetFileName());

        // Punt the SFX decompression to the patcher thread (this is the main/draw thread)
        if (stream->RequiresSfxCache()) {
            hsLockGuard(fFileMut);
            fQueuedFiles.emplace_back(pfPatcherQueuedFile::Type::kSoundDecompress, stream->GetFileName(), stream->GetFlags());
            fFileSignal.Signal();
        }
        IssueRequest();
    } else {
        PatcherLogRed("\tDownloaded Failed: File '{}'", stream->GetFileName());
        stream->Unlink();
        EndPatch(result, failure);
    }

    delete stream;
}

// ===================================================

pfPatcherWorker::pfPatcherWorker() :
    fStarted(false), fCurrBytes(0), fTotalBytes(0), fRequestActive(true),
    fWantPython(), fWantSDL()
{ }

pfPatcherWorker::~pfPatcherWorker()
{
    {
        hsLockGuard(fRequestMut);
        std::for_each(fRequests.begin(), fRequests.end(),
            [] (const Request& req) {
                delete req.fStream;
            }
        );
        fRequests.clear();
    }

    {
        hsLockGuard(fFileMut);
        fQueuedFiles.clear();
    }
}

void pfPatcherWorker::EndPatch(ENetError result, const ST::string& msg)
{
    // Guard against multiple calls
    if (fStarted) {
        // Send end status
        if (fOnComplete)
            fOnComplete(result, msg);

        // yay log hax
        if (IS_NET_SUCCESS(result))
            PatcherLogWhite("--- Patch Complete ---");
        else {
            PatcherLogRed("\tNetwork Error: {}", NetErrorToString(result));
            PatcherLogWhite("--- Patch Killed by Error ---");
        }
    }

    fStarted = false;
    fFileSignal.Signal();
}

bool pfPatcherWorker::IssueRequest()
{
    hsLockGuard(fRequestMut);
    if (fRequests.empty()) {
        fRequestActive = false;
        fFileSignal.Signal(); // make sure the patch thread doesn't deadlock!
        return false;
    } else
        fRequestActive = true;

    const Request& req = fRequests.front();
    switch (req.fType) {
        case Request::kFile:
            req.fStream->Begin();
            if (fFileBeginDownload)
                fFileBeginDownload(req.fStream->GetFileName());

            NetCliFileDownloadRequest(req.fName, req.fStream, 0, [this, filename = req.fName, stream = req.fStream](auto result) {
                IFileThingDownloadCB(result, filename, stream);
            });
            break;
        case Request::kManifest:
            NetCliFileManifestRequest(req.fName.to_utf16().data(), 0, [this, group = req.fName](auto result, const auto& manifest) {
                IFileManifestDownloadCB(result, group, manifest);
            });
            break;
        case Request::kSecurePreloader:
            // so, yeah, this is usually the "SecurePreloader" manifest on the file server...
            // except on legacy servers, this may not exist, so we need to fall back without nuking everything!
            NetCliFileManifestRequest(req.fName.to_utf16().data(), 0, [this, group = req.fName](auto result, const auto& manifest) {
                IPreloaderManifestDownloadCB(result, group, manifest);
            });
            break;
        case Request::kAuthFile:
            // ffffffuuuuuu
            req.fStream->Begin();
            if (fFileBeginDownload)
                fFileBeginDownload(req.fStream->GetFileName());

            NetCliAuthFileRequest(req.fName, req.fStream, [this, filename = req.fName, writer = req.fStream](auto result) {
                IAuthThingDownloadCB(result, filename, writer);
            });
            break;
        case Request::kPythonList:
            NetCliAuthFileListRequest(u"Python", u"pak", [this](auto result, const auto& infos) {
                IGotAuthFileList(result, infos);
            });
            break;
        case Request::kSdlList:
            NetCliAuthFileListRequest(u"SDL", u"sdl", [this](auto result, const auto& infos) {
                IGotAuthFileList(result, infos);
            });
            break;
        DEFAULT_FATAL(req.fType);
    }

    fRequests.pop_front();
    return true;
}

void pfPatcherWorker::Run()
{
    // So here's the rub:
    // We have one or many manifests in the fRequests deque. We begin issuing those requests one-by one, starting here.
    // As we receive the answer, the NetCli thread populates fQueuedFiles and pings the fFileSignal semaphore, then issues the next request...
    // In this non-UI/non-Net thread, we do the stutter-prone/time-consuming IO/hashing operations. (Typically, the UI thread == Net thread)
    // As we find files that need updating, we add them to fRequests.
    // If there is no net request from ME when we find a file, we issue the request
    // Once a file is downloaded, the next request is issued.
    // When there are no files in my deque and no requests in my deque, we exit without errors.
    PatcherLogWhite("--- Patch Started ({} requests) ---", fRequests.size());
    fStarted = true;
    IssueRequest();

    // Now, work until we're done processing files
    do {
        fFileSignal.Wait();

        hsLockGuard(fFileMut);
        if (!fQueuedFiles.empty()) {
            ProcessFile();
            continue;
        }

        // This makes sure both queues are empty before exiting.
        if (!fRequestActive)
            if(!IssueRequest())
                break;
    } while (fStarted);

    EndPatch(kNetSuccess);
}

void pfPatcherWorker::IHashFile(pfPatcherQueuedFile& file)
{
    // Only accept game code if we want it
    if (!fWantPython && file.fClientPath.GetFileExt().compare_i("pak") == 0) {
        PatcherLogRed("\tDeclined unwanted Python code '{}'", file.fClientPath);
        return;
    }
    if (!fWantSDL && file.fClientPath.GetFileExt().compare_i("sdl") == 0) {
        PatcherLogRed("\tDeclined unwanted SDL '{}'", file.fClientPath);
        return;
    }

    // Check to see if ours matches
    plFileName clientPathForComparison = file.fClientPath;
    if ((file.fFlags & kBundle) && fFindBundleExe) {
        clientPathForComparison = fFindBundleExe(clientPathForComparison);
    }
    plFileInfo mine(clientPathForComparison);
    if (mine.FileSize() == file.fFileSize) {
        plChecksum cliMD5(plChecksum::Type::kMD5, clientPathForComparison);
        if (cliMD5 == file.fChecksum) {
            WhitelistFile(file.fClientPath, false);
            return;
        }
    }

    // It's different... but do we want it?
    if (fFileDownloadDesired) {
        if (!fFileDownloadDesired(file.fClientPath)) {
            PatcherLogRed("\tDeclined '{}'", file.fClientPath);
            return;
        }
    }

    // If you got here, they're different and we want it.
    PatcherLogYellow("\tEnqueuing '{}'", file.fServerPath);
    plFileSystem::CreateDir(file.fClientPath.StripFileName());

    // If someone registered for SelfPatch notifications, then we should probably
    // let them handle the gruntwork... Otherwise, go nuts!
    if (fSelfPatch) {
        if (file.fClientPath == plFileSystem::GetCurrentAppPath().GetFileName()) {
            file.fClientPath += ".tmp"; // don't overwrite myself!
            file.fFlags |= kSelfPatch;
        }
    }

    pfPatcherStream* s = new pfPatcherStream(this, file);
    {
        hsLockGuard(fRequestMut);
        fRequests.emplace_back(file.fServerPath.AsString(), Request::kFile, s);
    }
}

void pfPatcherWorker::IDecompressSound(const pfPatcherQueuedFile& file) const
{
    PatcherLogGreen("\tDecompressing SFX '{}'", file.fClientPath);
    if (hsCheckBits(file.fFlags, kSndFlagCacheSplit))
        plAudioFileReader::CacheFile(file.fClientPath, true);
    if (hsCheckBits(file.fFlags, kSndFlagCacheStereo))
        plAudioFileReader::CacheFile(file.fClientPath, false);
}

void pfPatcherWorker::ProcessFile()
{
    do {
        pfPatcherQueuedFile& file = fQueuedFiles.front();
        switch (file.fType) {
        case pfPatcherQueuedFile::Type::kManifestHash:
            IHashFile(file);
            break;
        case pfPatcherQueuedFile::Type::kSoundDecompress:
            IDecompressSound(file);
            break;
        }
        fQueuedFiles.pop_front();

        if (!fRequestActive)
            IssueRequest();
    } while (!fQueuedFiles.empty());
}

void pfPatcherWorker::WhitelistFile(const plFileName& file, bool justDownloaded, hsStream* stream)
{
    // if this is a newly downloaded file, fire off a completion callback
    if (justDownloaded && fFileDownloaded)
        fFileDownloaded(file);

    // we want to whitelist our game code, so here we go...
    if (fGameCodeDiscovered) {
        ST::string ext = file.GetFileExt();
        if (ext.compare_i("pak") == 0 || ext.compare_i("sdl") == 0) {
            if (!stream) {
                hsUNIXStream* newStream = new hsUNIXStream;
                newStream->Open(file, "rb");
                stream = newStream;
            }

            // if something terrible goes wrong (eg bad encryption), we can exit sanely
            // callback eats stream
            if (!fGameCodeDiscovered(file, stream))
                EndPatch(kNetErrInternalError, "SecurePreloader failed.");
        }
    } else {
        // no dad gum memory leaks, m'kay?
        delete stream;
    }
}

void pfPatcherWorker::EnqueuePreloaderLists()
{
    PatcherLogYellow("\tWARNING: *** Falling back to AuthSrv file lists to get game code ***");

    hsLockGuard(fRequestMut);
    if (fWantPython)
        fRequests.emplace_back(ST::string(), pfPatcherWorker::Request::kPythonList);
    if (fWantSDL)
        fRequests.emplace_back(ST::string(), pfPatcherWorker::Request::kSdlList);
}

// ===================================================

plStatusLog* pfPatcher::GetLog()
{
    static plStatusLog* log = nullptr;
    if (!log)
    {
        log = plStatusLogMgr::GetInstance().CreateStatusLog(
            20,
            "patcher.log",
            plStatusLog::kFilledBackground | plStatusLog::kAlignToTop | plStatusLog::kDeleteForMe);
    }
    return log;
}

pfPatcher::pfPatcher() : fWorker(new pfPatcherWorker) { }
pfPatcher::pfPatcher(pfPatcher&& other) noexcept = default;
pfPatcher::~pfPatcher() { }

pfPatcher& pfPatcher::operator=(pfPatcher&& other) noexcept = default;

// ===================================================

void pfPatcher::OnFindBundleExe(FindBundleExeFunc vb)
{
    fWorker->fFindBundleExe = std::move(vb);
}

void pfPatcher::OnCompletion(CompletionFunc cb)
{
    fWorker->fOnComplete = std::move(cb);
}

void pfPatcher::OnFileDownloadBegin(FileDownloadFunc cb)
{
    fWorker->fFileBeginDownload = std::move(cb);
}

void pfPatcher::OnFileDownloadDesired(FileDesiredFunc cb)
{
    fWorker->fFileDownloadDesired = std::move(cb);
}

void pfPatcher::OnFileDownloaded(FileDownloadFunc cb)
{
    fWorker->fFileDownloaded = std::move(cb);
}

void pfPatcher::OnGameCodeDiscovery(GameCodeDiscoverFunc cb)
{
    fWorker->fGameCodeDiscovered = std::move(cb);
}

void pfPatcher::OnProgressTick(ProgressTickFunc cb)
{
    fWorker->fProgressTick = std::move(cb);
}

void pfPatcher::OnRedistUpdate(FileDownloadFunc cb)
{
    fWorker->fRedistUpdateDownloaded = std::move(cb);
}

void pfPatcher::OnSelfPatch(FileDownloadFunc cb)
{
    fWorker->fSelfPatch = std::move(cb);
}

#ifdef __EMSCRIPTEN__
void pfPatcher::SetStorageBudget(StorageBudgetFunc cb)
{
    s_storageBudget = std::move(cb);
}

void pfPatcher::SetWriteFailures(WriteFailuresFunc cb)
{
    s_writeFailures = std::move(cb);
}

#ifdef PLASMA_TEST_HOOKS
void pfPatcher::SetTestDownloadFaults(const plFileName& sizeFile, int32_t sizeDelta, const plFileName& dropFile,
                                      bool dropEvery, bool dropCorrupt)
{
    s_testDropCorrupt = dropCorrupt;
    s_testSizeFile = sizeFile.Normalize();
    s_testSizeDelta = sizeDelta;
    s_testDropFile = dropFile.Normalize();
    s_testDropArmed = dropFile.IsValid();
    s_testDropEvery = dropEvery;
}
#endif
#endif

// ===================================================

void pfPatcher::RequestGameCode(bool python, bool sdl)
{
    fWorker->fWantPython = python;
    fWorker->fWantSDL = sdl;

    hsLockGuard(fWorker->fRequestMut);
    if (NetCliFileQueryConnected()) {
        fWorker->fRequests.emplace_back("SecurePreloader", pfPatcherWorker::Request::kSecurePreloader);
    } else {
        fWorker->EnqueuePreloaderLists();
    }
}

void pfPatcher::RequestManifest(const ST::string& mfs)
{
    hsLockGuard(fWorker->fRequestMut);
    fWorker->fRequests.emplace_back(mfs, pfPatcherWorker::Request::kManifest);
}

void pfPatcher::RequestManifest(const std::vector<ST::string>& mfs)
{
    hsLockGuard(fWorker->fRequestMut);
    std::for_each(mfs.begin(), mfs.end(),
        [&] (const ST::string& name) {
            fWorker->fRequests.emplace_back(name, pfPatcherWorker::Request::kManifest);
        }
    );
}

void pfPatcher::Start()
{
    hsAssert(fWorker, "pfPatcher is one-use only. kthx.");
    if (!fWorker) {
        return;
    }

    // Ownership of the pfPatcherWorker moves from pfPatcher to the newly started patcher thread.
    hsThread::StartSimpleThread([worker = std::move(fWorker)] {
        hsThread::SetThisThreadName(ST_LITERAL("pfPatcherWorker"));
        worker->Run();
    }).detach();
}
