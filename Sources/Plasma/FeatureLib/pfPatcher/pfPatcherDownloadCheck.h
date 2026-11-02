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

// The browser's download check (pfPatcherStream, pfPatcher.cpp). It only logs: a download goes on exactly as natively,
// whatever it finds.
//
// A download can arrive with its full byte count and still be damaged. Three places can do that: the server's file,
// the way (the file channel carries plaintext with its own framing and no checksum: plNglFile.cpp), or the client after
// receipt. Two checks tell them apart:
//   - each chunk's CRC-32 (zlib's crc32) where it arrives, in the file connection's receive buffer
//     (DownloadRequestTrans::Recv, plNglFile.cpp, the first place where a chunk exists as a unit), and again just
//     before it is written (pfPatcherStream::Write, after the harness's test faults): a chunk whose two values differ
//     changed after receipt;
//   - the MD5 of the bytes written, against the manifest's md5compressed (NetCliFileManifestEntry, plNglFile.h, which
//     native parses and never reads). With every chunk consistent, a mismatch means that the bytes were already wrong
//     when they reached the client. A second download of the file then tells the server's file (the same wrong MD5
//     each time) from damage on the way (a different one).
// Chunks are written in the order they arrive (one FIFO: RcvdFileDownloadChunkTrans, NetTransSend in plNglTrans.cpp),
// so the n-th arrival is the n-th write. This class needs zlib only, so that a test can build it as it is.
#ifndef pfPatcherDownloadCheck_inc
#define pfPatcherDownloadCheck_inc

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

class pfPatcherDownloadCheck
{
public:
    // A manifest MD5 field (32 UTF-16 hex digits, no terminator) as lower-case hex, or empty when it is not 32 hex
    // digits, or is all zeros: no MD5 to check against
    static std::string ManifestMD5(const char16_t (&field)[32])
    {
        std::string hex;
        bool        zeros = true;
        for (char16_t c : field) {
            if (c >= u'A' && c <= u'F')
                c = char16_t(c - u'A' + u'a');
            if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
                return {};
            zeros = zeros && c == u'0';
            hex += char(c);
        }
        return zeros ? std::string() : hex;
    }

    // The CRC-32 of a chunk's bytes, as the arrival record and the write take it
    static uint32_t Crc(const void* data, uint32_t bytes)
    {
        return uint32_t(crc32(0L, static_cast<const Bytef*>(data), uInt(bytes)));
    }

    explicit pfPatcherDownloadCheck(std::string manifestMD5 = {}) : fManifestMD5(std::move(manifestMD5)) {}

    // The manifest's MD5 this check compares with (lower-case hex), or empty
    const std::string& ManifestMD5Hex() const { return fManifestMD5; }

    // A chunk reached the writer with its arrival CRC-32 (NetCliFileChunkArrivalCrc), before any test fault
    void Arrived(uint32_t crc, uint32_t bytes)
    {
        if (fHasPending)
            IMark(fPending, 0, false); // the previous chunk arrived and was never written
        fPending = {++fArrived, fOffset, bytes, crc};
        fHasPending = true;
        fOffset += bytes;
    }

    // The bytes about to be written: the chunk Arrived announced, unless a test fault dropped it. A write without an
    // arrival record (not a file-channel chunk) is only counted.
    void Written(const void* buf, uint32_t bytes)
    {
        if (!fHasPending) {
            ++fUnrecorded;
            return;
        }
        fHasPending = false;
        ++fWritten;
        const uint32_t crc = Crc(buf, bytes);
        if (crc != fPending.arrival || bytes != fPending.bytes)
            IMark(fPending, crc, true, bytes);
    }

    // Every chunk that arrived was written, with its arrival CRC-32
    bool Consistent() const { return !fBad && !fHasPending; }

    // Whether the download's line is due: the written bytes' MD5 (hex) is not the manifest's, or a chunk changed
    bool Mismatch(const std::string& writtenMD5) const
    {
        return !Consistent() || (!fManifestMD5.empty() && ILower(writtenMD5) != fManifestMD5);
    }

    // The line of a mismatch (no payload byte in it)
    std::string Line(const std::string& file, uint32_t attempt, const std::string& writtenMD5, uint64_t hashed,
                     uint64_t delivered, uint64_t deliverSize, int64_t fileSize) const
    {
        char head[512];
        std::snprintf(head, sizeof(head),
                      "Download check: '%s' attempt %u: MD5 %s of the %llu B written (delivered %llu of %llu B, "
                      "file %lld B), manifest md5compressed %s; ",
                      file.c_str(), unsigned(attempt + 1), writtenMD5.c_str(), (unsigned long long)hashed,
                      (unsigned long long)delivered, (unsigned long long)deliverSize, (long long)fileSize,
                      fManifestMD5.empty() ? "none" : fManifestMD5.c_str());
        return head + ChunkClause();
    }

    // The chunks: "chunks consistent (N arrived, N written)", or the first that changed after arrival, or that arrived
    // and was not written
    std::string ChunkClause() const
    {
        char         clause[256];
        const Chunk* bad = fBad ? &fBadChunk : (fHasPending ? &fPending : nullptr);
        if (!bad)
            std::snprintf(clause, sizeof(clause), "chunks consistent (%u arrived, %u written)", unsigned(fArrived),
                          unsigned(fWritten));
        else if (fBad && fBadWrite)
            std::snprintf(clause, sizeof(clause),
                          "chunk %u of %u (offset %llu, %u B) changed after arrival: CRC-32 %08x on arrival, %08x "
                          "at the write%s",
                          unsigned(bad->index), unsigned(fArrived), (unsigned long long)bad->offset,
                          unsigned(bad->bytes), unsigned(bad->arrival), unsigned(fBadCrc),
                          fBadBytes != bad->bytes ? ", of another size" : "");
        else
            std::snprintf(clause, sizeof(clause), "chunk %u of %u (offset %llu, %u B) arrived and was not written",
                          unsigned(bad->index), unsigned(fArrived), (unsigned long long)bad->offset,
                          unsigned(bad->bytes));
        std::string out = clause;
        if (fUnrecorded)
            out += "; " + std::to_string(fUnrecorded) + " written without an arrival record";
        return out;
    }

private:
    struct Chunk
    {
        uint32_t index;  // 1 for the download's first chunk
        uint64_t offset; // its first byte's offset in the delivered stream
        uint32_t bytes;
        uint32_t arrival; // its CRC-32 on arrival
    };

    static std::string ILower(std::string s)
    {
        for (char& c : s)
            if (c >= 'A' && c <= 'F')
                c = char(c - 'A' + 'a');
        return s;
    }

    void IMark(const Chunk& chunk, uint32_t crc, bool write, uint32_t bytes = 0)
    {
        if (fBad)
            return;
        fBad = true;
        fBadChunk = chunk;
        fBadWrite = write;
        fBadCrc = crc;
        fBadBytes = bytes;
    }

    std::string fManifestMD5; // lower-case hex, or empty
    uint32_t    fArrived = 0;
    uint32_t    fWritten = 0;
    uint32_t    fUnrecorded = 0;
    uint64_t    fOffset = 0;
    Chunk       fPending{};
    bool        fHasPending = false;
    // the first chunk that changed after arrival (fBadWrite, with the CRC-32 and size at its write), or that was not
    // written
    Chunk       fBadChunk{};
    bool        fBad = false;
    bool        fBadWrite = false;
    uint32_t    fBadCrc = 0;
    uint32_t    fBadBytes = 0;
};

#endif // pfPatcherDownloadCheck_inc
