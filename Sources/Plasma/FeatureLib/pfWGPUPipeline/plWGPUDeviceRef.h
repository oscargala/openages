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

#ifndef _plWGPUDeviceRef_inc_
#define _plWGPUDeviceRef_inc_

// The device refs of the WebGPU pipeline, a port of plMetalDeviceRef.h.
// Same classes and members; MTL::Buffer/MTL::Texture become WGPUBuffer/WGPUTexture.

#include <webgpu/webgpu.h>

#include <vector>

#include "HeadSpin.h"
#include "hsGDeviceRef.h"

class plGBufferGroup;
class plBitmap;
class plRenderTarget;

class plWGPUDeviceRef : public hsGDeviceRef
{
protected:
    plWGPUDeviceRef*  fNext;
    plWGPUDeviceRef** fBack;

public:
    void             Unlink();
    void             Link(plWGPUDeviceRef** back);
    plWGPUDeviceRef* GetNext() const { return fNext; }
    bool             IsLinked() const { return fBack != nullptr; }

    bool HasFlag(uint32_t f) const { return 0 != (fFlags & f); }
    void SetFlag(uint32_t f, bool on)
    {
        if (on)
            fFlags |= f;
        else
            fFlags &= ~f;
    }

    virtual void Release() = 0;

    plWGPUDeviceRef();
    virtual ~plWGPUDeviceRef();
};

/*
 The buffer pool of plMetalDeviceRef.h (plMetalBufferPoolRef). On WebGPU every queue.writeBuffer is ordered
 before the frame's single submit, so two writes to one buffer within a frame (a pass that
 rewrites a sorted index buffer, say) would both land before the draws that read the first.
 The pool keeps Metal's answer: one buffer per pass and frame, three frames deep. Buffers are
 created by the caller and handed to the pool, which releases them when replaced or released.
 */
class plWGPUBufferPoolRef : public plWGPUDeviceRef
{
public:
    uint32_t fCurrentFrame;
    uint32_t fCurrentPass;
    uint32_t fLastWriteFrameTime;

    plWGPUBufferPoolRef() : plWGPUDeviceRef(),
                            fCurrentFrame(),
                            fCurrentPass(),
                            fLastWriteFrameTime(),
                            fBuffer(),
                            fBufferSize()
    {
    }

    ~plWGPUBufferPoolRef() override { ReleaseBuffers(); }

    // plMetalBufferPoolRef::PrepareForWrite
    void PrepareForWrite()
    {
        if (fLastWriteFrameTime != fFrameTime) {
            fCurrentPass = 0;
            fLastWriteFrameTime = fFrameTime;
            fCurrentFrame = (fCurrentFrame + 1) % 3;
        } else {
            fCurrentPass++;
        }

        if (fCurrentPass < fBuffers[fCurrentFrame].size()) {
            fBuffer = fBuffers[fCurrentFrame][fCurrentPass].buffer;
            fBufferSize = fBuffers[fCurrentFrame][fCurrentPass].size;
        } else {
            fBuffer = nullptr;
            fBufferSize = 0;
        }
    }

    static void SetFrameTime(uint32_t frameTime) { fFrameTime = frameTime; }

    WGPUBuffer GetBuffer() const { return fBuffer; }
    uint64_t   GetBufferSize() const { return fBufferSize; }

    // plMetalBufferPoolRef::SetBuffer: the pool takes over the caller's reference.
    void SetBuffer(WGPUBuffer buffer, uint64_t size)
    {
        fBuffer = buffer;
        fBufferSize = size;
        if (fCurrentPass >= fBuffers[fCurrentFrame].size())
            fBuffers[fCurrentFrame].resize(fCurrentPass + 1);
        else if (fBuffers[fCurrentFrame][fCurrentPass].buffer)
            wgpuBufferRelease(fBuffers[fCurrentFrame][fCurrentPass].buffer);
        fBuffers[fCurrentFrame][fCurrentPass] = {buffer, size};
    }

    // plMetalBufferPoolRef::Release. Metal's vertex and index refs override Release() with a
    // dirty flag only and never free their pool (no destructor); here the buffers are freed
    // as plDXVertexBufferRef::Release/plDXIndexBufferRef::Release free theirs.
    void ReleaseBuffers()
    {
        for (auto& frame : fBuffers) {
            for (auto& entry : frame) {
                if (entry.buffer)
                    wgpuBufferRelease(entry.buffer);
            }
            frame.clear();
        }
        fBuffer = nullptr;
        fBufferSize = 0;
    }

    void Release() override { ReleaseBuffers(); }

private:
    struct Entry
    {
        WGPUBuffer buffer;
        uint64_t   size;
    };
    static uint32_t    fFrameTime;
    WGPUBuffer         fBuffer;
    uint64_t           fBufferSize;
    std::vector<Entry> fBuffers[3];
};

class plWGPUVertexBufferRef : public plWGPUBufferPoolRef
{
public:
    plGBufferGroup* fOwner;
    uint32_t        fCount;
    uint32_t        fIndex;
    uint32_t        fVertexSize;
    int32_t         fOffset;
    uint8_t         fFormat;
    uint8_t*        fData;

    uint32_t fRefTime;

    enum
    {
        kRebuiltSinceUsed = 0x10, // kDirty = 0x1 is in hsGDeviceRef
        kVolatile = 0x20,
        kSkinned = 0x40
    };

    bool RebuiltSinceUsed() const { return HasFlag(kRebuiltSinceUsed); }
    void SetRebuiltSinceUsed(bool b) { SetFlag(kRebuiltSinceUsed, b); }

    bool Volatile() const { return HasFlag(kVolatile); }
    void SetVolatile(bool b) { SetFlag(kVolatile, b); }

    bool Skinned() const { return HasFlag(kSkinned); }
    void SetSkinned(bool b) { SetFlag(kSkinned, b); }

    bool Expired(uint32_t t) const { return Volatile() && (IsDirty() || (fRefTime != t)); }
    void SetRefTime(uint32_t t) { fRefTime = t; }

    plWGPUVertexBufferRef() : plWGPUBufferPoolRef(),
                              fOwner(),
                              fCount(),
                              fIndex(),
                              fVertexSize(),
                              fOffset(),
                              fFormat(),
                              fData(),
                              fRefTime()
    {
    }

    ~plWGPUVertexBufferRef() override;

    void                   Link(plWGPUVertexBufferRef** back) { plWGPUDeviceRef::Link((plWGPUDeviceRef**)back); }
    plWGPUVertexBufferRef* GetNext() const { return (plWGPUVertexBufferRef*)fNext; }

    void Release() override;
};

class plWGPUIndexBufferRef : public plWGPUBufferPoolRef
{
public:
    uint32_t        fCount;
    uint32_t        fIndex;
    int32_t         fOffset; // the base vertex of the draws (plDXIndexBufferRef::fOffset)
    plGBufferGroup* fOwner;
    uint32_t        fRefTime;

    enum
    {
        kRebuiltSinceUsed = 0x10, // kDirty = 0x1 is in hsGDeviceRef
        kVolatile = 0x20
    };

    bool RebuiltSinceUsed() const { return HasFlag(kRebuiltSinceUsed); }
    void SetRebuiltSinceUsed(bool b) { SetFlag(kRebuiltSinceUsed, b); }

    bool Volatile() const { return HasFlag(kVolatile); }
    void SetVolatile(bool b) { SetFlag(kVolatile, b); }

    bool Expired(uint32_t t) const { return Volatile() && (IsDirty() || (fRefTime != t)); }
    void SetRefTime(uint32_t t) { fRefTime = t; }

    void Release() override;

    void                  Link(plWGPUIndexBufferRef** back) { plWGPUDeviceRef::Link((plWGPUDeviceRef**)back); }
    plWGPUIndexBufferRef* GetNext() const { return (plWGPUIndexBufferRef*)fNext; }
    ~plWGPUIndexBufferRef() override;

    plWGPUIndexBufferRef() : plWGPUBufferPoolRef(),
                             fCount(),
                             fIndex(),
                             fOffset(),
                             fOwner(),
                             fRefTime()
    {
    }
};

class plWGPUTextureRef : public plWGPUDeviceRef
{
public:
    plBitmap* fOwner;

    int32_t           fLevels;    // the GPU texture's level count; 1 for render targets;
                                  // for a texture pack texture, the shipped D3D9 count
    int32_t           fGpuLevels; // the GPU texture's actual level count
    WGPUTexture       fTexture;
    WGPUTextureView   fTextureView; // the whole texture (2D, or cube for cubic maps)
    WGPUTextureFormat fFormat;
    bool              fCubic;

    // Which upload path ran: logged once per texture.
    enum UploadPath : uint8_t
    {
        kUploadNone,
        kUploadBC,           // DXT1/DXT5 as BC1/BC3 (texture-compression-bc)
        kUploadDecompressed, // DXT decompressed by hsCodecManager (no BC feature)
        kUploadUncompressed  // plMipmap data, formatted per D3D9 (IFormatTextureData)
    } fUploadPath;

    void              Link(plWGPUTextureRef** back) { plWGPUDeviceRef::Link((plWGPUDeviceRef**)back); }
    plWGPUTextureRef* GetNext() const { return (plWGPUTextureRef*)fNext; }

    /// Counts texture/view releases (the device's bind-group cache keys on views).
    static uint32_t fReleaseSerial;

    plWGPUTextureRef() : plWGPUDeviceRef(),
                         fOwner(),
                         fLevels(1),
                         fGpuLevels(1),
                         fTexture(),
                         fTextureView(),
                         fFormat(WGPUTextureFormat_Undefined),
                         fCubic(),
                         fUploadPath(kUploadNone)
    {
    }

    ~plWGPUTextureRef() override;

    void Release() override;
};

class plWGPURenderTargetRef : public plWGPUTextureRef
{
public:
    WGPUTexture       fDepthBuffer;
    WGPUTextureView   fDepthView;
    // The colour attachment: the texture's only level (2D targets), or one face of the
    // parent's cube texture (cubic render target faces, plMetalPipeline::MakeRenderTargetRef).
    WGPUTextureView   fRenderView;
    // The depth surface's format (plDXRenderTargetRef keeps its formats, plDXDeviceRefs.cpp:
    // 365-414); undefined without a depth surface.
    WGPUTextureFormat fDepthFormat;

    void                   Link(plWGPURenderTargetRef** back) { plWGPUDeviceRef::Link((plWGPUDeviceRef**)back); }
    plWGPURenderTargetRef* GetNext() const { return (plWGPURenderTargetRef*)fNext; }

    plWGPURenderTargetRef() : fDepthBuffer(), fDepthView(), fRenderView(), fDepthFormat(WGPUTextureFormat_Undefined)
    {
    }

    ~plWGPURenderTargetRef() override;

    void Release() override;

    virtual void SetOwner(plRenderTarget* targ) { fOwner = (plBitmap*)targ; }
};

#endif // _plWGPUDeviceRef_inc_
