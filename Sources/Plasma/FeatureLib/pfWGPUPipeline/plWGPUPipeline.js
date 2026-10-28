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

// JS helpers of pfWGPUPipeline: an Emscripten library, linked into the client (CMakeLists.txt --js-library).
addToLibrary({
  // The WebGPU boot failure, handed to the page: the message is decoded while the caller's string
  // is alive. A sync proxy keeps that lifetime across the client-worker -> browser-main boundary.
  plWGPU_reportBootFailure__proxy: 'sync',
  plWGPU_reportBootFailure__sig: 'vi',
  plWGPU_reportBootFailure__deps: ['$UTF8ToString'],
  plWGPU_reportBootFailure: (message) => {
    message >>>= 0; // wasm32 pointers may cross the JS boundary as signed i32.
    if (typeof Module.plasma?.bootFailure === 'function')
      Module.plasma.bootFailure(['worker-gpu'], UTF8ToString(message));
  },
  // The canvas has presented the client's first frame (plWGPUEnumerate::NoteCanvasTexture): the page's loading line
  // steps aside. Async: the client thread posts it once and goes on.
  plWGPU_reportFirstPresent__proxy: 'async',
  plWGPU_reportFirstPresent__sig: 'v',
  plWGPU_reportFirstPresent: () => {
    if (typeof Module.plasma?.firstFrame === 'function')
      Module.plasma.firstFrame();
  },
  // A serial per distinct GPUTexture returned by GPUCanvasContext.getCurrentTexture(): the
  // context hands out a new texture only after the previous one was presented. The last serial
  // handed out is this library's own state (one per thread, as WebGPU's object table is).
  $plWGPUTextureSerials: { last: 0 },
  plWGPU_textureSerial__deps: ['$WebGPU', '$plWGPUTextureSerials'],
  plWGPU_textureSerial: (ptr) => {
    var t = WebGPU.Internals.jsObjects[ptr];
    if (!t) return -1;
    if (t.plWGPUSerial === undefined)
      t.plWGPUSerial = ++plWGPUTextureSerials.last;
    return t.plWGPUSerial;
  },
  // The optional HD texture pack (plWGPUTexturePack.cpp): one streaming fetch on the browser main thread into one wasm
  // allocation of the response's length. Not through WasmFS: the fetch backend's chunk cache, read-ahead and persistent
  // store (plEmscriptenFetchBackend.js) are for the game files; a pack is read once per Age visit and held by the
  // client.
  // Not in the HTTP cache (no-store); the client checks every byte with SHA-256. A verified pack is kept in Cache
  // Storage instead (below).
  // block: four 32-bit words shared with the client thread: [0] state (0 in flight, 1 complete, 2 abandoned by the
  // client, < 0 failed: -1 missing, -2 unreadable, -3 origin refused, -4 too large, -5 out of memory), [1] bytes
  // written, [2] the buffer (0 until the length is known), [3] the length. The client owns the block and the buffer
  // unless it abandoned the fetch first (compare-exchange 0 -> 2); then this side frees both when it ends.
  // Origins: the page's own, or one of the deployment's pinned asset origins (PLASMA_RUNTIME_ASSET_CACHE_ORIGINS, the
  // rule of plEmscriptenAssetCache.js PlasmaAssetCache.create and identity()); credentials, query and fragment are
  // refused.
  // The pin list is checked by plEmscriptenAssetCache.js's own rule (PlasmaAssetCache.pinnedOrigins: at most
  // config.mjs's ASSET_ORIGIN_PINS, a duplicate rejects the whole list), so both admit the same origins.
  // Packs are kept between sessions in Cache Storage ($plWGPUTexturePackCache). The block
  // gains [4] the source (0 the network, 1 the Cache Storage entry) and, at byte 20, the entry's key SHA-256 (32
  // bytes); both are written before [2]. mode 0: after the origin checks, the URL's entry is used when the server
  // answers 304 to a request conditional on the entry's ETag; any other answer evicts it, and a 200 is read as the
  // network response. mode 1 (the client found the cached pack defective): the entry is evicted first, then the
  // network. A Cache Storage failure is a cache miss: the network is used.
  // The kept packs share one byte cap (capBytes): the packs used least recently are evicted first to stay within it.
  // A pack's use is the time the client took it into use: its network fetch kept (store), or its entry read after a
  // 304 (fetch). The use times live in one small JSON record in the same cache (recencyUrl), never in the packs'
  // entries, so no pack is copied to record a use. Every change to the kept set and the record runs under one lock
  // (exclusive: Web Locks across the origin's pages, else a promise chain in this page). A pack larger than the cap is
  // never kept.
  $plWGPUTexturePackCache: {
    name: 'plasma-hdpack-v1',
    capBytes: 256 * 1024 * 1024,
    // pack URL -> the strong ETag of the network response the client is verifying (plWGPUTexturePack_store's)
    etags: {},
    strong: function(tag) {
      // plEmscriptenAssetCache.js identity()'s strong-ETag rule
      return typeof tag === 'string' && /^"[\x21\x23-\x7e\x80-\xff]*"$/.test(tag);
    },
    recencyUrl: function() {
      return new URL('/.plasma-hdpack-recency.json', globalThis.location.href).href;
    },
    chain: null,      // (set at run time: the library object's values are serialised when the client is linked)
    exclusive: function(fn) {
      if (globalThis.navigator && navigator.locks && typeof navigator.locks.request === 'function')
        return navigator.locks.request(plWGPUTexturePackCache.name, fn);
      const run = (plWGPUTexturePackCache.chain || Promise.resolve()).then(fn);
      // The chain only orders the next holder after this one, failed or not; the failure itself reaches the caller
      // through run, so the chain's copy is dropped.
      plWGPUTexturePackCache.chain = run.catch(() => {});
      return run;
    },
    // {pack URL: last use, ms since the epoch}; a missing or malformed record is empty
    readRecency: async function(cache) {
      try {
        const r = await cache.match(plWGPUTexturePackCache.recencyUrl());
        const v = r ? await r.json() : {};
        return v && typeof v === 'object' && !Array.isArray(v) ? v : {};
      } catch (e) {
        console.warn('WebGPU: texture pack: the kept packs\' use record is unreadable; it is read as empty: ' + e);
        return {};
      }
    },
    writeRecency: function(cache, rec) {
      return cache.put(plWGPUTexturePackCache.recencyUrl(), new Response(JSON.stringify(rec), {
        headers: { 'Content-Type': 'application/json' } }));
    },
    // Called under exclusive(). Evicts the least recently used kept packs until they and `incoming` more bytes fit the
    // cap. The entries of `replacing` (a pack URL whose new entry is being written) are left to that write and not
    // counted. An entry this code cannot read the size of is evicted (lookup would evict it too). Returns the evicted
    // pack URLs and bytes; the record keeps only kept packs.
    enforce: async function(cache, replacing, incoming) {
      const C = plWGPUTexturePackCache, rec = await C.readRecency(cache), rows = [], gone = [];
      let total = incoming, bytes = 0;
      for (const req of await cache.keys()) {
        if (req.url === C.recencyUrl()) continue;
        const m = /^(.*)\?sha256=[0-9a-f]{64}$/.exec(req.url);
        const r = m ? await cache.match(req) : null;
        const size = r ? Number(r.headers.get('X-Plasma-HDPack-Bytes')) : NaN;
        if (!m || !Number.isSafeInteger(size) || size <= 0) { await cache.delete(req); continue; }
        if (m[1] === replacing) continue;
        rows.push({ req, url: m[1], size, used: Number(rec[m[1]]) || 0 });
        total += size;
      }
      rows.sort((a, b) => a.used - b.used);   // stable: equal times keep the cache's order
      const kept = new Set(rows.map(r => r.url));
      for (const r of rows) {
        if (total <= C.capBytes) break;
        await cache.delete(r.req);
        total -= r.size;
        bytes += r.size;
        gone.push(r.url);
        kept.delete(r.url);
      }
      const next = {};
      for (const url of Object.keys(rec))
        if (kept.has(url) || url === replacing) next[url] = rec[url];
      await C.writeRecency(cache, next);
      return { urls: gone, bytes };
    },
    // Records a use of a kept pack (its entry read after a 304); a failure is logged and otherwise ignored, like the
    // other Cache Storage failures
    touch: function(url) {
      const C = plWGPUTexturePackCache;
      if (typeof caches === 'undefined') return Promise.resolve();
      return C.exclusive(async () => {
        const cache = await caches.open(C.name);
        const rec = await C.readRecency(cache);
        rec[url] = Date.now();
        await C.writeRecency(cache, rec);
      }).catch((e) => {
        console.warn('WebGPU: texture pack: the use of ' + url + ' was not recorded: ' + e);
      });
    },
    // The URL's usable entry, {response, etag, size, sha}, or null; an entry that cannot be used is evicted.
    lookup: async function(url, maxBytes) {
      try {
        if (typeof caches === 'undefined') return null;
        const cache = await caches.open(plWGPUTexturePackCache.name);
        const keys = await cache.keys(url, { ignoreSearch: true });
        if (!keys.length) return null;
        let entry = null;
        const m = keys.length === 1 ? /^(.*)\?sha256=([0-9a-f]{64})$/.exec(keys[0].url) : null;
        if (m && m[1] === url) {
          const response = await cache.match(keys[0]);
          const etag = response && response.headers.get('X-Plasma-HDPack-ETag');
          const size = Number(response && response.headers.get('X-Plasma-HDPack-Bytes'));
          if (response && response.body && plWGPUTexturePackCache.strong(etag) && Number.isSafeInteger(size) &&
              size > 0 && size <= maxBytes)
            entry = { response, etag, size, sha: Uint8Array.from(m[2].match(/../g), h => parseInt(h, 16)) };
        }
        if (!entry)
          for (const k of keys) await cache.delete(k);
        return entry;
      } catch (e) {
        console.warn('WebGPU: texture pack: Cache Storage lookup failed for ' + url + '; the network is used: ' + e);
        return null;
      }
    },
    evict: async function(url) {
      try {
        if (typeof caches === 'undefined') return;
        const cache = await caches.open(plWGPUTexturePackCache.name);
        for (const k of await cache.keys(url, { ignoreSearch: true })) await cache.delete(k);
      } catch (e) {
        console.warn('WebGPU: texture pack: Cache Storage eviction failed for ' + url + ': ' + e);
      }
    },
  },
  plWGPUTexturePack_fetch__proxy: 'sync',
  plWGPUTexturePack_fetch__sig: 'ippii',
  plWGPUTexturePack_fetch__deps: ['$UTF8ToString', '$stringToNewUTF8', 'getenv', 'malloc', 'free', '$plWGPUTexturePackCache',
                                  '$PlasmaAssetCache'],
  plWGPUTexturePack_fetch: (url, block, maxBytes, mode) => {
    url = UTF8ToString(url >>> 0);
    block >>>= 0;
    maxBytes >>>= 0;
    const w = block >>> 2;
    let data = 0, reader = null;
    const abandoned = () => Atomics.load(HEAP32, w) === 2;
    const end = (code) => {
      // code 1 complete, < 0 failed; a fetch the client abandoned frees its own memory
      if (Atomics.compareExchange(HEAP32, w, 0, code) === 2) {
        if (data) _free(data);
        _free(block);
      }
      Atomics.notify(HEAP32, w + 1);
    };
    let u;
    try {
      u = new URL(url, globalThis.location.href);
      const page = new URL(globalThis.location.href);
      const local = ['localhost', '127.0.0.1', '[::1]'].includes(u.hostname);
      let pins = [];
      const name = stringToNewUTF8('PLASMA_RUNTIME_ASSET_CACHE_ORIGINS');
      try { pins = JSON.parse(UTF8ToString(_getenv(name), 8192) || '[]'); }
      catch (e) {
        pins = [];
        console.warn('WebGPU: texture pack: the pinned asset origins are not JSON; none is admitted: ' + e);
      }
      finally { _free(name); }
      const admitted = PlasmaAssetCache.pinnedOrigins(pins, page.origin);
      const pinned = admitted !== null && admitted.has(u.origin);
      if ((u.origin !== page.origin && !pinned) || u.username || u.password || u.search || u.hash ||
          (u.protocol !== 'https:' && !(u.protocol === 'http:' && local && page.protocol === 'http:')))
        return -3;
    } catch (e) {
      console.warn('WebGPU: texture pack: ' + url + ' refused: ' + e);
      return -3;
    }
    (async () => {
      try {
        const C = plWGPUTexturePackCache;
        let entry = null;
        if (mode === 1) await C.evict(u.href);
        else entry = await C.lookup(u.href, maxBytes);
        if (abandoned()) return end(-2);
        const init = { cache: 'no-store', redirect: 'error' };
        if (entry) init.headers = { 'If-None-Match': entry.etag };
        const r = await fetch(u.href, init);
        if (abandoned()) return end(-2);
        let body, size;
        delete C.etags[u.href];
        if (entry && r.status === 304) {
          body = entry.response.body;
          size = entry.size;
          Atomics.store(HEAPU32, w + 4, 1);
          HEAPU8.set(entry.sha, block + 20);
          C.touch(u.href);      // a use of the kept pack (the cap's recency), without waiting for it
        } else {
          if (entry) await C.evict(u.href);     // the server's pack changed, or is gone
          if (abandoned()) return end(-2);
          if (r.status === 404) return end(-1);
          const enc = r.headers.get('Content-Encoding');
          const len = r.headers.get('Content-Length');
          if (r.status !== 200 || !r.body || (enc !== null && enc.trim().toLowerCase() !== 'identity') ||
              len === null || !/^\d+$/.test(len) || Number(len) === 0)
            return end(-2);
          size = Number(len);
          if (!Number.isSafeInteger(size) || size > maxBytes) return end(-4);
          const tag = r.headers.get('ETag');
          if (C.strong(tag)) C.etags[u.href] = tag;
          body = r.body;
        }
        data = _malloc(size) >>> 0;
        if (!data) return end(-5);
        Atomics.store(HEAPU32, w + 3, size);
        Atomics.store(HEAPU32, w + 2, data);
        reader = body.getReader();
        let at = 0;
        for (;;) {
          const { done, value } = await reader.read();
          if (done) break;
          // The body is discarded either way: cancel() rejects only when the stream has already failed, which
          // changes nothing here.
          if (abandoned()) { reader.cancel().catch(() => {}); return end(-2); }
          if (at + value.length > size) { reader.cancel().catch(() => {}); return end(-2); }
          HEAPU8.set(value, data + at);
          at += value.length;
          Atomics.store(HEAPU32, w + 1, at);
          Atomics.notify(HEAP32, w + 1);
        }
        end(at === size ? 1 : -2);
      } catch (e) {
        console.warn('WebGPU: texture pack: fetch of ' + u.href + ' failed: ' + e);
        // A failed read leaves the stream errored, and cancel() then rejects with the error just logged.
        if (reader) reader.cancel().catch(() => {});
        end(-2);
      }
    })();
    return 0;
  },
  // Keeps a pack the client verified from the network in Cache Storage, under
  // <URL>?sha256=<the pack's data SHA-256> with the network response's strong ETag (the revalidation's validator) and
  // its length, replacing the URL's other entries. The body streams from the client's buffer in 1 MiB slices: no copy
  // of the pack is held here. block: [0] state (0 writing, 1 kept, 2 abandoned by the client, < 0 not kept), [1] the
  // buffer, [2] its length, bytes 12-43 the SHA-256. The client owns the block and the pack the buffer unless the
  // client abandons the write first (compare-exchange 0 -> 2); then this side frees both when the write ends.
  // Returns 0 when the write started, -1 without Cache Storage, -2 without a strong ETag, -3 larger than the cap.
  // The kept packs' cap (above): before the write, the least recently used other packs are evicted until
  // this one fits; after it, the pack's use is recorded and the cap enforced again over every kept pack, since another
  // write may have ended meanwhile. block [11] the packs evicted, [12] their bytes (written before [0]), [13] the cap
  // (written at the call). The state is then 1 kept, or -2 when this pack was itself the least recently used one.
  plWGPUTexturePack_store__proxy: 'sync',
  plWGPUTexturePack_store__sig: 'ipp',
  plWGPUTexturePack_store__deps: ['$UTF8ToString', 'free', '$plWGPUTexturePackCache'],
  plWGPUTexturePack_store: (url, block) => {
    url = UTF8ToString(url >>> 0);
    block >>>= 0;
    const w = block >>> 2, C = plWGPUTexturePackCache;
    const etag = C.etags[url];
    delete C.etags[url];
    Atomics.store(HEAPU32, w + 13, Math.min(C.capBytes, 0xffffffff));
    if (typeof caches === 'undefined') return -1;
    if (!etag) return -2;
    const data = Atomics.load(HEAPU32, w + 1) >>> 0, size = Atomics.load(HEAPU32, w + 2) >>> 0;
    if (size > C.capBytes) return -3;
    const used = Date.now();
    const key = url + '?sha256=' + Array.from(HEAPU8.subarray(block + 12, block + 44),
                                              b => b.toString(16).padStart(2, '0')).join('');
    const end = (code) => {
      if (Atomics.compareExchange(HEAP32, w, 0, code) === 2) {
        _free(data);
        _free(block);
      }
    };
    let evicted = 0, evictedBytes = 0;
    const note = (r) => {     // the other packs evicted (this one, evicted by the cap after its write, is the state)
      const self = r.urls.includes(url);
      evicted += r.urls.length - (self ? 1 : 0);
      evictedBytes += r.bytes - (self ? size : 0);
      Atomics.store(HEAPU32, w + 11, evicted);
      Atomics.store(HEAPU32, w + 12, Math.min(evictedBytes, 0xffffffff));
    };
    (async () => {
      try {
        const cache = await caches.open(C.name);
        note(await C.exclusive(() => C.enforce(cache, url, size)));
        let at = 0;
        const body = new ReadableStream({
          pull: (controller) => {
            if (Atomics.load(HEAP32, w) === 2) throw Error('abandoned');
            const n = Math.min(1 << 20, size - at);
            controller.enqueue(HEAPU8.slice(data + at, data + at + n));   // a copy of one slice, not shared memory
            at += n;
            if (at === size) controller.close();
          },
        }, { highWaterMark: 0 });
        await cache.put(new Request(key), new Response(body, { headers: {
          'Content-Type': 'application/octet-stream', 'X-Plasma-HDPack-Bytes': String(size),
          'X-Plasma-HDPack-ETag': etag } }));
        const after = await C.exclusive(async () => {
          for (const k of await cache.keys(url, { ignoreSearch: true }))
            if (k.url !== key) await cache.delete(k);
          const rec = await C.readRecency(cache);
          rec[url] = used;
          await C.writeRecency(cache, rec);
          return C.enforce(cache, null, 0);
        });
        note(after);
        end(after.urls.includes(url) ? -2 : 1);
      } catch (e) {
        console.warn('WebGPU: texture pack: Cache Storage write failed for ' + url + ': ' + e);
        end(-1);
      }
    })();
    return 0;
  },
});
