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

// The bytes of browser frames that the relays of one isolate hold for their upstreams, together. gateway.py stops
// reading a browser's socket while 16 frames wait for the shard, and bounds its process's buffers by admitting no more
// connections than limits.memoryBytes covers (gateway.py Admission). A Worker can do neither: it cannot pause a
// WebSocket's incoming side, and one isolate runs relays that other isolates admitted. So each relay counts the frames
// it holds against its own cap (the 16 frames' bytes) and against the isolate's budget (limits.isolateQueueBytes),
// and a frame that fits neither closes a relay with the close a relay gets at its own cap.
//
// When the isolate's relays hold the whole budget, the frame that would exceed it closes the relay that holds the most
// queued bytes, the arriving frame counted: the sender itself when it holds the most (ties go to the sender), else
// the largest other relay, whose share is returned at once, and the frame is then queued. A relay that holds the most
// in its isolate is the one filling the budget, so a relay whose queue stays small is never closed by the budget, even
// while others hold it full and stop sending.
//
// This module has no Workers imports, so the contract test can run it in Node.

export class QueueBudget {
  constructor(limit = 0) {
    this.limit = limit; // limits.isolateQueueBytes, set from the deployment before each relay opens
    this.used = 0; // the bytes all open relays hold
    this.peak = 0; // the most bytes held at once since the isolate started
    this.queues = new Set(); // the open relays' queues
  }

  open(cap, evict) {
    // A relay's queue, holding at most cap bytes. evict() is called when the budget closes the relay to make room for
    // another relay's frame. The relay must then close itself (the queue is already closed and its share returned).
    const q = new RelayQueue(this, cap, evict);
    this.queues.add(q);
    return q;
  }
}

export class RelayQueue {
  constructor(budget, cap, evict) {
    this.budget = budget;
    this.cap = cap;
    this.evict = evict;
    this.queued = 0;
    this.closed = false;
  }

  add(n) {
    // A frame of n bytes arrives. Returns null when it is queued, else the refusal: 'relay' (this relay's own cap) or
    // 'isolate' (the isolate's budget, this relay holding the most; or the queue was already closed to make room).
    if (this.closed) return 'isolate';
    if (this.queued + n > this.cap) return 'relay';
    const b = this.budget;
    while (b.used + n > b.limit) {
      let largest = this,
        most = this.queued + n;
      for (const q of b.queues) {
        if (q.queued > most) {
          largest = q;
          most = q.queued;
        }
      }
      if (largest === this) return 'isolate';
      largest.close();
      largest.evict();
    }
    this.queued += n;
    b.used += n;
    if (b.used > b.peak) b.peak = b.used;
    return null;
  }

  drain(n) {
    // n bytes of a queued frame have left the queue: written to the upstream, or dropped. Nothing after close().
    if (this.closed) return;
    this.queued -= n;
    this.budget.used -= n;
  }

  close() {
    // The relay ends (or is evicted): every byte it still holds goes back to the budget, once.
    if (this.closed) return;
    this.closed = true;
    this.budget.used -= this.queued;
    this.queued = 0;
    this.budget.queues.delete(this);
  }
}

// The upstream half of a relay writes each browser frame to the shard's socket in pieces, as the downstream half sends
// the shard's bytes to the browser in pieces of at most gateway.js MAX_FRAME_OUT. TCP carries a byte stream with no
// message boundaries, so the shard reads the same bytes in the same order however they are cut, and gateway.py hands
// each frame to its asyncio transport in one write (make_relay upstream), which sends it as the socket takes it.

export async function writePieces(bytes, max, write) {
  // Calls write(piece) for consecutive views of bytes (a Uint8Array) of at most max bytes, in order, and awaits each
  // call before the next, so at most one piece is in flight. The views share the frame's memory: no piece is copied,
  // and the frame stays held until its last piece is written. write may return false to stop: the rest is not
  // written, and writePieces returns false. A rejected write rejects writePieces with its error, and the rest is not
  // written. An empty frame writes nothing. Returns true when every piece was written.
  for (let o = 0; o < bytes.byteLength; o += max) {
    if ((await write(bytes.subarray(o, o + max))) === false) return false;
  }
  return true;
}
