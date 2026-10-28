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

// Parts of this file come from Emscripten 6.0.12 src/lib/libwasmfs_fetch.js, which carries this notice:
/**
 * @license
 * Copyright 2022 The Emscripten Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The JS half of the WasmFS fetch backend, replacing
 * Emscripten 6.0.12's src/lib/libwasmfs_fetch.js (same symbol, so this --js-library
 * definition wins; the C++ half, system/lib/wasmfs/backends/fetch_backend.cpp, is
 * unchanged). The game data is 4 GB of files that the client stats and reads in
 * pieces, which the stock implementation handles badly:
 *  - stat() (getSize) downloads data: the first 64 KiB chunk of a large file, or the
 *    whole file when it is smaller than two chunks. Plasma's ListDir stats every file
 *    it lists (dat/ at start-up: 1,355 files), so listing = downloading.
 *    Here getSize is one HEAD request, cached.
 *  - a read that runs past end of file (every stdio buffer refill at the tail) refetches
 *    the whole of a small file, because the request length is not clamped to the size.
 *  - concurrent reads of one file race on the shared cache entry.
 *  - freeFile leaves `file in cache` true with an undefined entry.
 *  - every fetched byte stays in the JS heap of the proxy worker for good.
 * This version: HEAD for the size; reads clamped to the size and served from aligned
 * chunks of chunkSize bytes; one Range request per run of missing chunks, shared by
 * concurrent readers; an LRU byte cap on the cached chunks (Module.plasma.fetch.cacheBytes,
 * default 64 MiB), with temporary read-local references that survive eviction;
 * a strong HEAD ETag pins each engine file; If-Match and returned ETags prevent
 * mixed-version range reads; changed entries fail until freed/recreated;
 * range headers and payload lengths validated before caching; errors returned as -errno, never thrown (a rejected promise would
 * leave the calling thread blocked forever).
 * The persistent chunk store is off the awaited read path: a validated run resolves its readers when its
 * bytes arrive and queues them for the store (write-behind, bounded by Module.plasma.fetch.writeBehindBytes, default 64 MiB,
 * beyond which a read waits for room); one run's stores take one lock hold, and its lookups read the records without
 * the lock (PlasmaAssetCache getRun); a chunk the store did not hold at session start, and has not stored since, skips
 * the lookup. A read that needs a chunk memory does not hold reads ahead over the file's next chunks
 * (Module.plasma.fetch.readAheadChunks, default 15: 1 MiB windows of 64 KiB chunks), in the same store lookup or Range
 * request.
 */
addToLibrary({
  _wasmfs_create_fetch_backend_js__deps: [
    '$wasmFS$backends',
    '$PlasmaAssetCache',
    '$PlasmaAssetCatalog',
    '$stringToNewUTF8',
    'getenv',
    'free',
    '_wasmfs_fetch_get_file_url',
    '_wasmfs_fetch_get_chunk_size',
  ],
  _wasmfs_create_fetch_backend_js: async function(backend) {
    // Fetch runs on an SDK proxy worker, whose JS ENV object is independent.
    // Read only the public verified-config pins from the shared native environment.
    var origins = [], name = stringToNewUTF8('PLASMA_RUNTIME_ASSET_CACHE_ORIGINS');
    try { origins = JSON.parse(UTF8ToString(_getenv(name), 8192) || '[]'); }
    catch (_) { /* Malformed/missing pins retain uncached remote HTTP fallback. */ }
    finally { _free(name); }
    var persistent = PlasmaAssetCache.create({origins});
    var catalog = PlasmaAssetCatalog.create();
    var files = new Map();      // file pointer -> entry
    var lru = new Map();        // "file:chunk" -> bytes (insertion order = age)
    var cachedBytes = 0;
    // The backend's three bounds, Module.plasma.fetch {cacheBytes, writeBehindBytes, readAheadChunks}
    // (the page and module's one object, boot.mjs): each absent one takes its default. The backend runs on the SDK's
    // proxy worker, whose Module is its own, so only a pre-js (which runs on every thread: a test build's) or a test
    // fixture's worker sets them; the player page passes none.
    var tune = (typeof Module !== 'undefined' && Module.plasma && Module.plasma.fetch) || {};
    // 64 MiB (256 MiB before): the cached chunks are ArrayBuffers in the renderer process, and re-reads of evicted
    // chunks are served by the persistent store. On a route of three Age links (Relto to Gira, back to Relto, Gira
    // again) the 64 MiB cap held the renderer 169-186 MiB lower than 256 MiB, and a link's load part (its time after
    // the shard's part) rose by 0.08-0.23 s, less than two 256 MiB runs differ (0.94 / 1.40 s on the warm Relto link),
    // and the first load took 13.2 s against 12.1-13.3 s in four 256 MiB runs; 32 MiB saved 16 MiB more and cost more.
    var cacheCap = tune.cacheBytes || (64 * 1024 * 1024);
    // Write-behind for the persistent store. Only bytes that passed every check an awaited put would have (the run's
    // validated response, a cacheable entry, a response permitting storage) are queued; the store applies its own
    // identity checks when it writes them. Until its put finishes, a queued chunk also serves a later
    // reader of the same identity (URL, strong tag, size, chunk size, index), for example after the engine closed and
    // reopened the file, in place of a store lookup that could not find it yet. Bytes still queued when the client
    // stops are not stored; the next session fetches them again.
    var wbCap = tune.writeBehindBytes || (64 * 1024 * 1024);
    var wbQueue = [];           // [{e, items: [[chunk index, bytes, identity key], ...]}], in arrival order
    var wbQueued = new Map();   // identity key -> bytes whose put has not finished
    var wbBytes = 0, wbRunning = false, wbWaiters = [];
    var wbBatch = 16;           // chunks per store lock hold (64 KiB chunks: 1 MiB), across queued runs
    var readAhead = (Number.isSafeInteger(tune.readAheadChunks) && tune.readAheadChunks >= 0) ? tune.readAheadChunks : 15;


    function wbKey(e, c) { return JSON.stringify([e.url, e.etag, e.size, e.chunkSize, c]); }
    function wbWake() {
      if (wbBytes > wbCap && wbRunning) return;
      var waiters = wbWaiters; wbWaiters = [];
      for (var wake of waiters) wake();
    }
    async function wbDrain() {
      wbRunning = true;
      try {
        while (wbQueue.length) {
          const batch = [];
          while (wbQueue.length && batch.length < wbBatch) {
            const job = wbQueue[0];
            for (const item of job.items.splice(0, wbBatch - batch.length)) batch.push([job.e, ...item]);
            if (!job.items.length) wbQueue.shift();
          }
          try { await persistent.putMany(batch.map(([e, c, chunk]) => [e, c, chunk])); } catch (_) { /* best effort: the chunk is fetched again next session */ }
          for (const [, , chunk, key] of batch) { wbQueued.delete(key); wbBytes -= chunk.byteLength; }
          wbWake();
        }
      } finally { wbRunning = false; wbWake(); }
    }
    function wbEnqueue(e, result) {
      const items = [];
      for (const [c, chunk] of result) {
        const key = wbKey(e, c);
        if (wbQueued.has(key)) continue;
        wbQueued.set(key, chunk); wbBytes += chunk.byteLength; items.push([c, chunk, key]);
      }
      if (items.length) wbQueue.push({ e, items });
      if (!wbRunning && wbQueue.length) wbDrain();
      return wbBytes <= wbCap ? null : new Promise(resolve => wbWaiters.push(resolve));
    }

    function entryFor(file) {
      var e = files.get(file);
      if (!e) {
        var url = UTF8ToString(__wasmfs_fetch_get_file_url(file)).replace(/([^:])\/\/+/g, '$1/');
        if (url.indexOf('://') === -1) url = new URL(url, self.location.origin).toString();
        e = { url, size: undefined, sizePromise: null, chunkSize: __wasmfs_fetch_get_chunk_size(file),
              etag: null, invalid: false, chunks: new Map(), pending: new Map() };
        files.set(file, e);
      }
      return e;
    }

    function strongTag(value) {
      // RFC 9110 entity-tag, excluding weak validators. Empty opaque tags are valid.
      return value !== null && /^"[\x21\x23-\x7e\x80-\xff]*"$/.test(value);
    }

    function identityEncoding(response) {
      var value = response.headers.get('Content-Encoding');
      return value === null || value.trim().toLowerCase() === 'identity';
    }

    function clearChunks(file, e) {
      for (var i of e.chunks.keys()) {
        var key = file + ':' + i;
        var bytes = lru.get(key);
        if (bytes) { cachedBytes -= bytes.byteLength; lru.delete(key); }
      }
      e.chunks.clear();
    }

    function invalidate(file, e) {
      // Never repin an existing engine file: some old bytes may already have been
      // consumed by its reader. Only a new file entry (or client reload) can repin.
      e.invalid = true;
      if (files.get(file) === e) clearChunks(file, e);
      return { rc: -{{{ cDefs.EIO }}} };
    }

    function cachePolicy(response) {
      const policy = (response.headers.get('Cache-Control') || '').toLowerCase().split(',').map(v => v.trim());
      return response.headers.get('X-Plasma-Asset-Cache') === 'chunks-v1' &&
        policy.includes('private') && policy.includes('no-cache') && !policy.includes('no-store');
    }

    async function ensureSize(e) {
      if (e.invalid) return -{{{ cDefs.EIO }}};
      if (e.size !== undefined) return 0;
      if (!e.sizePromise) {
        e.sizePromise = (async () => {
          const metadata = await catalog.lookup(e.url);
          if (metadata) {
            e.size = metadata.size; e.etag = metadata.etag; e.cacheable = metadata.cacheable;
            return 0;
          }
          var r = await fetch(e.url, { method: 'HEAD', cache: 'no-store', redirect: 'error' });
          if (!r.ok) return r.status === 404 ? -{{{ cDefs.ENOENT }}} : -{{{ cDefs.EIO }}};
          var tag = r.headers.get('ETag');
          if (!strongTag(tag) || !identityEncoding(r)) return -{{{ cDefs.EIO }}};
          var len = r.headers.get('Content-Length');
          if (len === null) return -{{{ cDefs.EIO }}};
          if (!/^\d+$/.test(len)) return -{{{ cDefs.EIO }}};
          var size = Number(len);
          if (!Number.isSafeInteger(size) || size < 0) return -{{{ cDefs.EIO }}};
          e.cacheable = cachePolicy(r);
          e.etag = tag;
          e.size = size;
          return 0;
        })().catch(() => -{{{ cDefs.EIO }}});
      }
      var rc = await e.sizePromise;
      if (rc) e.sizePromise = null;  // allow a retry after a failure
      return rc;
    }

    function touch(file, e, i, bytes) {
      // A freed/reused file pointer must not publish an old in-flight response.
      if (files.get(file) !== e) return;
      var key = file + ':' + i;
      if (lru.has(key)) { lru.delete(key); lru.set(key, bytes); return; }
      lru.set(key, bytes);
      cachedBytes += bytes.byteLength;
      while (cachedBytes > cacheCap && lru.size > 1) {
        var oldKey = lru.keys().next().value;
        var old = lru.get(oldKey);
        lru.delete(oldKey);
        cachedBytes -= old.byteLength;
        var sep = oldKey.indexOf(':');
        var oe = files.get(+oldKey.slice(0, sep));
        if (oe) oe.chunks.delete(+oldKey.slice(sep + 1));
      }
    }

    // Keep the requested chunks alive for this read, independently of cache
    // eviction. Pending requests resolve to their bytes, not just a cache-side
    // success flag. Even a read larger than the LRU cap must finish correctly.
    async function ensureChunks(file, e, first, last) {
      const chunks = new Map();
      const waiting = new Map();
      // read-ahead: when the read needs a chunk that memory does not hold, the store lookup, and the
      // Range request of the missing run that ends at `last`, extend over the file's next chunks that memory, a pending
      // request and the write-behind queue do not hold (contiguous, at most readAhead, clamped to the file). The read
      // waits for them in that same lookup or response; they are cached, validated and stored like any other chunk.
      let ahead = last;
      if (readAhead > 0) {
        let needs = false;
        for (let c = first; c <= last && !needs; c++) needs = !e.chunks.has(c);
        if (needs) {
          const fileLast = Math.floor((e.size - 1) / e.chunkSize);
          while (ahead < fileLast && ahead - last < readAhead && !e.chunks.has(ahead + 1) && !e.pending.has(ahead + 1) &&
                 !(e.cacheable && wbQueued.has(wbKey(e, ahead + 1)))) ahead++;
        }
      }
      if (e.cacheable) {
        // one store lookup for the run's chunks (read-ahead included) that neither memory, a pending
        // request nor the write-behind queue holds
        const wanted = [];
        for (let c = first; c <= ahead; c++) {
          if (e.invalid) return { rc: -{{{ cDefs.EIO }}} };
          if (e.chunks.has(c) || e.pending.has(c)) continue;
          const queued = wbQueued.get(wbKey(e, c));
          if (queued) {
            if (files.get(file) === e) {
              chunks.set(c, queued); e.chunks.set(c, queued); touch(file, e, c, queued);
            }
            continue;
          }
          wanted.push(c);
        }
        if (wanted.length) {
          const hits = await persistent.getRun(e, wanted, true);
          if (!e.invalid && files.get(file) === e)
            for (const [c, bytes] of hits) { chunks.set(c, bytes); e.chunks.set(c, bytes); touch(file, e, c, bytes); }
        }
      }
      let i = first;
      while (i <= last) {
        if (chunks.has(i)) { i++; continue; }
        if (e.chunks.has(i)) {
          const bytes = e.chunks.get(i);
          chunks.set(i, bytes);
          touch(file, e, i, bytes);
          i++;
          continue;
        }
        if (e.pending.has(i)) {
          waiting.set(i, e.pending.get(i)); i++; continue;
        }
        const runStart = i;
        while (i <= last && !e.chunks.has(i) && !e.pending.has(i)) i++;
        let runEnd = i - 1;
        // the read-ahead chunks the store did not supply join the run that ends at `last`
        if (runEnd === last)
          while (runEnd < ahead && !chunks.has(runEnd + 1) && !e.chunks.has(runEnd + 1) && !e.pending.has(runEnd + 1)) runEnd++;
        i = Math.max(i, runEnd + 1);
        const start = runStart * e.chunkSize;
        const end = Math.min((runEnd + 1) * e.chunkSize, e.size) - 1;
        const pending = (async () => {
          const response = await fetch(e.url, { cache: 'no-store', redirect: 'error',
            headers: { 'Range': `bytes=${start}-${end}`, 'If-Match': e.etag } });
          if (e.invalid || response.status === 412 || response.status === 404)
            return invalidate(file, e);
          if (response.status !== 200 && response.status !== 206) return { rc: -{{{ cDefs.EIO }}} };
          if (response.headers.get('ETag') !== e.etag || !identityEncoding(response))
            return invalidate(file, e);
          const bytes = new Uint8Array(await response.arrayBuffer());
          if (e.invalid) return { rc: -{{{ cDefs.EIO }}} };
          let base = 0;
          if (response.status === 206) {
            const match = /^bytes (\d+)-(\d+)\/(\d+)$/.exec(response.headers.get('Content-Range') || '');
            if (!match || Number(match[1]) !== start || Number(match[2]) !== end ||
                Number(match[3]) !== e.size || bytes.length !== end - start + 1)
              return { rc: -{{{ cDefs.EIO }}} };
            base = start;
          } else if (bytes.length !== e.size) {
            // A server ignoring Range must return the complete representation.
            return { rc: -{{{ cDefs.EIO }}} };
          }
          const result = new Map();
          for (let c = runStart; c <= runEnd; c++) {
            const offset = c * e.chunkSize - base;
            // Copy so one retained cache chunk does not retain a whole response.
            const chunk = bytes.slice(offset, Math.min(offset + e.chunkSize, bytes.length));
            result.set(c, chunk);
            if (files.get(file) === e) {
              e.chunks.set(c, chunk);
              touch(file, e, c, chunk);
            }
          }
          if (e.cacheable && !e.invalid && cachePolicy(response)) {
            // queued for the store, not awaited; a read waits only while the queue is over its cap
            const room = wbEnqueue(e, result);
            if (room) await room;
          }
          return { rc: 0, chunks: result };
        })().catch(() => ({ rc: -{{{ cDefs.EIO }}} }));
        for (let c = runStart; c <= runEnd; c++) {
          e.pending.set(c, pending);
          waiting.set(c, pending);
        }
        // Block-scoped bounds and promise: multiple missing runs must each clear
        // their own entries, including failures and already-evicted responses.
        pending.then(() => {
          for (let c = runStart; c <= runEnd; c++)
            if (e.pending.get(c) === pending) e.pending.delete(c);
        });
      }
      for (const [c, pending] of waiting) {
        const result = await pending;
        if (result.rc) return result;
        const bytes = result.chunks.get(c);
        if (!bytes) return { rc: -{{{ cDefs.EIO }}} };
        chunks.set(c, bytes);
      }
      return e.invalid ? { rc: -{{{ cDefs.EIO }}} } : { rc: 0, chunks };
    }

    wasmFS$backends[backend] = {
      allocFile: async (file) => {},
      freeFile: async (file) => {
        var e = files.get(file);
        if (!e) return;
        clearChunks(file, e);
        files.delete(file);
      },
      write: async (file, buffer, length, offset) => -{{{ cDefs.EROFS }}},
      read: async (file, buffer, length, offset) => {
        try {
          if (offset < 0 || length <= 0) return 0;
          const e = entryFor(file);
          const rc = await ensureSize(e);
          if (rc) return rc;
          if (offset >= e.size) return 0;
          length = Math.min(length, e.size - offset);
          const first = Math.floor(offset / e.chunkSize);
          const last = Math.floor((offset + length - 1) / e.chunkSize);
          const result = await ensureChunks(file, e, first, last);
          if (result.rc) return result.rc;
          if (e.invalid) return -{{{ cDefs.EIO }}};
          for (let i = first; i <= last; i++) {
            const chunk = result.chunks.get(i);
            const chunkStart = i * e.chunkSize;
            const start = Math.max(chunkStart, offset);
            const end = Math.min(chunkStart + chunk.byteLength, offset + length);
            HEAPU8.set(chunk.subarray(start - chunkStart, end - chunkStart), buffer + (start - offset));
          }
          return length;
        } catch (_) {
          // The SDK finishes its proxy only after this promise resolves.
          return -{{{ cDefs.EIO }}};
        }
      },
      // test hook: resolves once the write-behind queue is empty (its puts finished)
      flushPersistent: () => new Promise(resolve => {
        const check = () => { if (!wbRunning && !wbQueue.length) resolve(); else wbWaiters.push(check); };
        check();
      }),
      getSize: async (file) => {
        try {
          var e = entryFor(file);
          var rc = await ensureSize(e);
          return rc ? 0 : e.size;
        } catch (_) { return 0; }
      },
    };
  },
});
