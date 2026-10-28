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

// Optional read-only asset chunks. Original WasmFS/POSIX readers remain authoritative.
addToLibrary({
  $PlasmaAssetCache: {
    // Runtime configuration may admit exact remote origins: the set of pins, or
    // null for the whole list on malformed/ambiguous input (duplicates included).
    // At most PlasmaAssetOriginPins, config.mjs's
    // ASSET_ORIGIN_PINS, which the client build defines (asset-origin-pins.js,
    // plClient CMakeLists.txt); where it is not defined no pin is admitted. The
    // pack fetch's origin check (plWGPUPipeline.js) applies this same rule.
    pinnedOrigins: function(values, pageOrigin) {
      const limit = typeof PlasmaAssetOriginPins === 'number' ? PlasmaAssetOriginPins : 0;
      const pinned = new Set();
      try {
        if (!Array.isArray(values) || values.length > limit) throw Error('origin pins');
        for (const value of values) {
          if (typeof value !== 'string' || value.length > 2048) throw Error('origin pin');
          const u = new URL(value);
          const local = ['localhost', '127.0.0.1', '[::1]'].includes(u.hostname);
          if (u.origin !== value || u.href !== value + '/' || /[%*]/.test(u.hostname) ||
              u.username || u.password || u.search || u.hash || pinned.has(value) ||
              (u.protocol !== 'https:' && !(u.protocol === 'http:' && local && new URL(pageOrigin).protocol === 'http:')))
            throw Error('origin pin');
          pinned.add(value);
        }
      } catch (_) { return null; }
      return pinned;
    },
    create: function(options = {}) {
      const namespace = '.plasma-asset-chunks-v1';
      const encoder = new TextEncoder();
      const decoder = new TextDecoder();
      const byteCap = options.byteCap === undefined ? 512 * 1024 * 1024 : options.byteCap;
      // The record cap bounds the per-record overhead (index rows, OPFS entries, the start-up reconcile); the byte cap
      // is the budget. Files end in a partial chunk, so the game data's records average less than a full 64 KiB chunk,
      // and a cap of 8192 records (512 MiB in full chunks) would bind before the byte cap does. 16384 (512 MiB in
      // 32 KiB records) leaves the byte cap the binding one.
      const entryCap = options.entryCap === undefined ? 16384 : options.entryCap;
      const maxChunk = 4 * 1024 * 1024;
      // Eviction never takes a record used in this boot: a committed record whose LRU stamp is at or after the boot's
      // start (`epoch`) is protected, and a new record that finds only protected ones is not stored. Every boot reads
      // the same records in the same order (the header of every .prp, plResManager::IInit, then StartUp's and the Age's
      // pages), so where a boot reads more than the budget, plain LRU would evict the record that boot reads next, and
      // every warm boot of such a set would fetch nearly all of it again. Instead the store keeps the first 512 MiB the
      // boot uses and fetches only the rest; records no boot since the epoch has used are evicted least recently used
      // first, so a boot that fits converges as LRU does. The
      // epoch is the first store instance this worker creates (the client mounts every fetch tree at start-up, main.cpp
      // MountFetchTree), so all the boot's instances share it. Under the budget nothing is evicted and nothing changes.
      if (!Number.isSafeInteger(this.epoch)) this.epoch = Date.now();
      const epoch = this.epoch;
      // Reject the whole pin list on malformed/ambiguous input; legacy and
      // same-origin mode stay valid.
      const pinnedOrigins = this.pinnedOrigins(options.origins === undefined ? [] : options.origins,
                                               self.location.origin) || new Set();
      const available = !options.disabled && typeof navigator !== 'undefined' &&
        navigator.storage && navigator.storage.getDirectory && navigator.locks &&
        typeof indexedDB !== 'undefined' && typeof crypto !== 'undefined' && crypto.subtle &&
        Number.isSafeInteger(byteCap) && byteCap > 0 && Number.isSafeInteger(entryCap) && entryCap > 0;
      // refused: new records not stored because every committed record was used in this boot.
      const stats = { hits: 0, misses: 0, writes: 0, evictions: 0, refused: 0, corrupt: 0, errors: 0, recovered: 0 };
      let db, directory, ready = false, disabled = !available;
      // A lower bound of the committed records' LRU stamps, as far as this instance knows:
      // the oldest stamp (or the time) at its reconcile, raised to the last victim's stamp by each of its evictions,
      // lowered by any stamp it writes. The eviction plan reads the 'used' index from here instead of from its start:
      // IndexedDB (LevelDB in Chromium) keeps a deleted index row as a tombstone until compaction, so every read from
      // the start passes all the rows evicted before it, a cost that grows with every eviction, across boots. A record
      // below the bound (another tab's stamp after the clock went back) is not
      // offered first, but when the read from the bound runs short the plan reads the whole index, so the caps hold.
      let floor = null;
      const request = r => new Promise((resolve, reject) => {
        r.onsuccess = () => resolve(r.result);
        r.onerror = () => reject(r.error);
      });
      async function transaction(mode, fn) {
        const t = db.transaction(['chunks', 'state'], mode);
        const finished = new Promise((resolve, reject) => {
          t.oncomplete = resolve;
          t.onerror = t.onabort = () => reject(t.error || Error('cache transaction aborted'));
        });
        // Queue IDB work synchronously: never keep a transaction alive across OPFS awaits.
        const result = fn(t.objectStore('chunks'), t.objectStore('state'));
        await finished;
        return result;
      }
      const query = (store, key) => request(db.transaction(store, 'readonly').objectStore(store).get(key));
      // Several chunk records in one transaction (undefined where absent). stamp: readwrite, and each
      // record found gets its LRU stamp in the same transaction (a read hit's stamp, written before the lock is released;
      // one that then fails verification is discarded like any corrupt record).
      const getMany = (keys, stamp) => new Promise((resolve, reject) => {
        const t = db.transaction('chunks', stamp ? 'readwrite' : 'readonly'), store = t.objectStore('chunks'), out = new Array(keys.length);
        keys.forEach((key, n) => { store.get(key).onsuccess = event => {
          const record = event.target.result; out[n] = record;
          if (record && stamp) {
            const used = Date.now();
            store.put({ ...record, used });
            if (floor !== null && used < floor) floor = used;
          }
        }; });
        t.oncomplete = () => resolve(out);
        t.onerror = t.onabort = () => reject(t.error || Error('cache transaction aborted'));
      });
      const all = () => request(db.transaction('chunks', 'readonly').objectStore('chunks').getAll());
      const digest = async bytes => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('');
      async function initialize() {
        if (db) return;
        directory = await (await navigator.storage.getDirectory()).getDirectoryHandle(namespace, { create: true });
        const opening = indexedDB.open(namespace, 1);
        opening.onupgradeneeded = () => {
          const entries = opening.result.createObjectStore('chunks', { keyPath: 'key' });
          entries.createIndex('used', 'used');
          opening.result.createObjectStore('state');
        };
        db = await new Promise((resolve, reject) => {
          opening.onsuccess = () => resolve(opening.result);
          opening.onerror = () => reject(opening.error);
          opening.onblocked = () => reject(Error('asset index blocked'));
        });
        db.onversionchange = () => { db.close(); disabled = true; };
      }
      async function remove(key) {
        try { await directory.removeEntry(key); }
        catch (error) { if (error.name !== 'NotFoundError') throw error; }
      }
      async function reconcile() {
        const now = Date.now();
        floor = null;
        const records = await all(), known = new Map(), present = new Set();
        for (const r of records) {
          if (/^[a-f0-9]{64}$/.test(r.key) && Number.isSafeInteger(r.bytes) &&
              r.bytes > 0 && r.bytes <= maxChunk + 8196 && Number.isFinite(r.used)) known.set(r.key, r);
        }
        for await (const [name, handle] of directory.entries()) {
          // This directory belongs solely to this schema, never engine user files.
          if (handle.kind === 'file' && known.has(name)) present.add(name);
          else await directory.removeEntry(name, { recursive: true });
        }
        let bytes = 0, count = 0;
        for (const key of present) { bytes += known.get(key).bytes; count++; }
        const evicted = [];
        for (const r of [...known.values()].filter(r => present.has(r.key)).sort((a, b) => a.used - b.used)) {
          if (bytes <= byteCap && count <= entryCap) break;
          await remove(r.key); present.delete(r.key); bytes -= r.bytes; count--; evicted.push(r.key);
        }
        // A stamp after now was written while the clock was ahead (every stamp is written under
        // the lock, so none is newer than this hold). Left alone it would read as used in this boot (at or after the
        // epoch) and stay protected until the clock passed it; it is re-stamped just before the epoch instead, the most
        // recent of the earlier boots' records.
        const future = [...present].map(key => known.get(key)).filter(r => r.used > now);
        for (const r of future) r.used = Math.min(epoch, now) - 1;
        await transaction('readwrite', (chunks, state) => {
          for (const r of records) if (!present.has(r.key)) chunks.delete(r.key);
          for (const r of future) chunks.put({ ...r });
          state.put({ bytes, count, dirty: false }, 'total');
        });
        // Under the lock: a record committed later is stamped after now.
        floor = now;
        for (const key of present) floor = Math.min(floor, known.get(key).used);
        stats.evictions += evicted.length;
        stats.recovered++;
        ready = true;
      }
      // The LRU stamps of the read path's hits, which getRun reads without the lock (key -> time of the hit, at most
      // entryCap keys). Each lock hold of this instance writes them first, in one transaction, so the eviction plan of
      // its next put sees them, and a record this boot read stays protected. A stamp never moves back. Stamps still
      // queued when the page exits, or not queued because the queue is full, are lost: that only makes the LRU
      // approximate (such a record may be evicted before one used less recently, and is fetched again).
      const stamps = new Map();
      function stamp(key, used) {
        if (stamps.has(key) || stamps.size < entryCap) stamps.set(key, used);
      }
      const flushStamps = () => new Promise((resolve, reject) => {
        const t = db.transaction('chunks', 'readwrite'), store = t.objectStore('chunks');
        for (const [key, used] of stamps) store.get(key).onsuccess = event => {
          const record = event.target.result;
          if (record && !(record.used >= used)) {
            store.put({ ...record, used });
            if (floor !== null && used < floor) floor = used;
          }
        };
        stamps.clear();
        t.oncomplete = resolve;
        t.onerror = t.onabort = () => reject(t.error || Error('cache transaction aborted'));
      });
      async function locked(fn) {
        if (disabled) return null;
        try {
          return await navigator.locks.request(namespace, async () => {
            await initialize();
            let state = await query('state', 'total');
            if (!ready || !state || state.dirty || !Number.isSafeInteger(state.bytes) || state.bytes < 0 || state.bytes > byteCap || !Number.isSafeInteger(state.count) || state.count < 0 || state.count > entryCap) { await reconcile(); state = await query('state', 'total'); }
            if (stamps.size) await flushStamps();
            return await fn(state);
          });
        } catch (_) {
          // Storage is an optimization; quota/privacy/API failures cannot block the reader.
          stats.errors++; disabled = true;
          if (db) db.close();
          return null;
        }
      }
      async function identity(file, index) {
        const url = new URL(file.url);
        if (!file.cacheable || file.invalid || (url.origin !== self.location.origin && !pinnedOrigins.has(url.origin)) ||
            url.username || url.password || url.search || url.hash ||
            !/^"[\x21\x23-\x7e\x80-\xff]*"$/.test(file.etag || '') ||
            !Number.isSafeInteger(file.size) || file.size < 0 ||
            !Number.isSafeInteger(file.chunkSize) || file.chunkSize <= 0 || file.chunkSize > maxChunk ||
            !Number.isSafeInteger(index) || index < 0 || index * file.chunkSize >= file.size) return null;
        const descriptor = JSON.stringify([1, url.href, file.etag, file.size, file.chunkSize, index]);
        return { descriptor, key: await digest(encoder.encode(descriptor)),
          length: Math.min(file.chunkSize, file.size - index * file.chunkSize) };
      }
      // Returns the accounting after the discard, so that one lock hold can discard more than one record.
      async function discard(key, state) {
        await transaction('readwrite', (_, totals) => totals.put({ ...state, dirty: true }, 'total'));
        await remove(key);
        const record = await query('chunks', key);
        const next = { bytes: state.bytes - (record ? record.bytes : 0), count: state.count - (record ? 1 : 0), dirty: false };
        await transaction('readwrite', (chunks, totals) => {
          chunks.delete(key);
          totals.put(next, 'total');
        });
        if (known) known.delete(key);
        return next;
      }
      // A record's payload, or a throw: the header's identity must be the chunk's (url, strong ETag, size, chunk size,
      // index: identity()) and the payload's digest the header's.
      async function verify(bytes, id) {
        const length = new DataView(bytes.buffer).getUint32(0, true);
        if (length > 8192 || bytes.length !== 4 + length + id.length) throw Error('header length');
        const header = JSON.parse(decoder.decode(bytes.subarray(4, 4 + length)));
        const payload = bytes.slice(4 + length);
        if (header.identity !== id.descriptor || header.digest !== await digest(payload)) throw Error('chunk identity/integrity');
        return payload;
      }
      // The read path's lookup of a known record, without the lock or the index: the record file alone, verified as
      // the locked lookup verifies it. The store is the port's own (the native client reads its data files straight
      // from disk), so it follows no engine code: a lookup must only not make the engine's synchronous loads wait for
      // the store's other work, which the lock serializes. The record is the chunk's current content because its key
      // and header carry the identity, whose strong ETag is the one this session validated for the file (the
      // catalog's content digest, or the HEAD response's ETag; the fetch backend's ensureSize), so a record of another
      // version of the file has another key. A file written to a key is a complete record (writeRecord:
      // createWritable's close replaces the file atomically). Returns null on any failure (the file absent, or removed
      // or replaced while it is read by another worker or tab, or failing the checks): the chunk then takes the locked
      // lookup, which reads it again and discards it only if it fails there too. Nothing here writes or removes.
      async function readUnlocked(id) {
        try {
          const snapshot = await api.openRecord(directory, id.key);
          if (snapshot.size > id.length + 8196) return null;
          return await verify(new Uint8Array(await snapshot.arrayBuffer()), id);
        } catch (_) { return null; }
      }
      // The committed record keys as of this instance's first run lookup (one readonly transaction
      // under the lock, after any recovery), plus the records this instance commits later and minus the ones it evicts
      // or discards. getRun(..., true) uses it: a chunk whose key is absent skips the store, and one whose key is
      // present is read without the lock first (readUnlocked). A record that another worker or tab commits meanwhile is
      // then fetched again, and the put finds it committed; one that another evicts is a miss.
      let known = null, knownLoading = null;
      function knownKeys() {
        if (!knownLoading) knownLoading = locked(async () => {
          known = new Set(await request(db.transaction('chunks', 'readonly').objectStore('chunks').getAllKeys()));
          return known;
        });
        return knownLoading;
      }
      const api = {
        stats,
        get: async function(file, index) {
          const hits = await api.getRun(file, [index], false);
          return hits.has(index) ? hits.get(index) : null;
        },
        // The chunks of one read run under one lock hold: one index transaction for their records,
        // each present record read and verified exactly as a single get does (OPFS length, header, identity, payload
        // digest), the records concurrently; failing ones are then discarded one by one. The LRU stamps are written in
        // the lookup's own transaction (getMany). Returns a Map index -> payload. skipKnownAbsent: the fetch backend's
        // read path, which consults knownKeys() first and reads the known records without the lock (readUnlocked;
        // their stamps are queued for the next lock hold); only the ones that read fails take the lock hold.
        getRun: async function(file, indices, skipKnownAbsent) {
          const hits = new Map();
          if (disabled || !indices.length) return hits;
          try {
            const ids = (await Promise.all(indices.map(index => identity(file, index))))
              .map((id, n) => id && [indices[n], id]).filter(Boolean);
            let wanted = ids;
            if (skipKnownAbsent && wanted.length) {
              const keys = known || await knownKeys();
              if (keys) {
                wanted = ids.filter(([, id]) => keys.has(id.key));
                stats.misses += ids.length - wanted.length;
                const read = await Promise.all(wanted.map(([, id]) => readUnlocked(id)));
                const used = Date.now();
                wanted = wanted.filter(([index, id], n) => {
                  if (!read[n]) return true;
                  hits.set(index, read[n]); stats.hits++; stamp(id.key, used);
                  return false;
                });
              }
            }
            if (!wanted.length) return hits;
            await locked(async state => {
              const records = await getMany(wanted.map(([, id]) => id.key), true);
              const verified = await Promise.all(wanted.map(async ([, id], n) => {
                const record = records[n];
                if (!record) return null;
                try {
                  const snapshot = await api.openRecord(directory, id.key);
                  if (snapshot.size !== record.bytes || snapshot.size > id.length + 8196) throw Error('record length');
                  return { payload: await verify(new Uint8Array(await snapshot.arrayBuffer()), id) };
                } catch (_) { return { corrupt: true }; }
              }));
              for (let n = 0; n < wanted.length; n++) {
                const v = verified[n];
                if (!v) { stats.misses++; continue; }
                if (v.corrupt) { stats.corrupt++; state = await discard(wanted[n][1].key, state); stats.misses++; continue; }
                hits.set(wanted[n][0], v.payload); stats.hits++;
              }
            });
          } catch (_) { stats.errors++; }
          return hits;
        },
        put: async function(file, index, payload) {
          return await api.putMany([[file, index, payload]]) === 1;
        },
        putRun: async function(file, entries) {
          return api.putMany(entries.map(([index, payload]) => [file, index, payload]));
        },
        // Several chunks ([[file, index, payload], ...]) under one lock hold: one index read for the
        // already committed ones and one dirty marker; then the same LRU eviction as single puts in entry order (the
        // batch's earlier records are the newest candidates, as they would be once committed: one evicted that way is
        // simply not written), the committed victims removed, the records written as atomic OPFS records concurrently
        // (the lock is held until every write settled), and one transaction publishing the records, the evictions and
        // the accounting. An interrupted or failed batch leaves the dirty marker, so the next holder reconciles, as after
        // an interrupted single put. Returns the number of entries committed (newly or before), 0 when none.
        putMany: async function(entries) {
          if (disabled || !entries.length) return 0;
          try {
            const prepared = (await Promise.all(entries.map(async ([file, index, payload]) => {
              const id = await identity(file, index);
              if (!id || payload.byteLength !== id.length) return null;
              const header = encoder.encode(JSON.stringify({ identity: id.descriptor, digest: await digest(payload) }));
              const size = 4 + header.length + payload.length;
              if (header.length > 8192 || size > byteCap) return null;
              const bytes = new Uint8Array(size);
              new DataView(bytes.buffer).setUint32(0, header.length, true);
              bytes.set(header, 4); bytes.set(payload, 4 + header.length);
              return { key: id.key, bytes, size };
            }))).filter(Boolean);
            if (!prepared.length) return 0;
            return await locked(async state => {
              const committed = await getMany(prepared.map(r => r.key));
              const seen = new Set();
              const fresh = prepared.filter((r, n) => !committed[n] && !seen.has(r.key) && seen.add(r.key));
              if (!fresh.length) return prepared.length;
              // The eviction plan over the committed records least recently used first
              // (the 'used' index's order, ties by key). plan(candidates, exhausted) returns null while it would need a
              // committed record beyond the candidates read so far, so the index is read only as far as the plan
              // needs: the first n records, n growing fourfold, instead of every record for each batch at the caps.
              // Under the lock the index does not change between reads. The reads start at this instance's floor; a
              // plan that ran out of them (short) is made again from the whole index, as without the floor.
              // A victim must have been last used before the epoch. In index order the first
              // candidate stamped at or after it ends the victims: every later one was used in this boot too. A
              // record that cannot be made room for from the earlier boots' records is refused (not stored), and the
              // victims taken for it are kept, so no record is evicted for a record that is then not stored; the
              // batch's later records are still stored where they fit. The batch's own records are this boot's, so
              // they are never victims (LRU took them once the committed records ran out). Refusing never exceeds a
              // cap, so only a plan that ran out of rows before reaching this boot's records is made again from the
              // whole index (a record below the floor is stamped there only after the clock went back).
              const plan = (candidates, exhausted) => {
                let bytes = state.bytes, count = state.count, next = 0, short = false;
                const planned = [], refused = [];
                for (const r of fresh) {
                  const mark = next, was = [bytes, count];
                  let fits = true;
                  while (bytes + r.size > byteCap || count + 1 > entryCap) {
                    if (next < candidates.length && candidates[next].used < epoch) {
                      const victim = candidates[next++]; bytes -= victim.bytes; count--;
                    } else if (next < candidates.length) { fits = false; break; }
                    else if (!exhausted) return null;
                    else { short = true; fits = false; break; }
                  }
                  if (fits) { planned.push(r); bytes += r.size; count++; }
                  else { refused.push(r); next = mark; [bytes, count] = was; }
                }
                const victims = candidates.slice(0, next);
                return { bytes, count, evicted: victims.map(v => v.key), planned, refused, short,
                         last: victims.length ? victims[victims.length - 1].used : null };
              };
              // lowest: the first stamp the last read returned, the lowest committed stamp at
              // or above where it started. The floor is raised to it, so the next read does not pass again the index
              // rows deleted below it: every read hit re-stamps its record and leaves its old row deleted (a LevelDB
              // tombstone), and a refused batch evicts nothing, so without this each refused batch of a warm boot read
              // past all the boot's re-stamped rows (about 20 ms a read, 1.4 s for ChisoPreniv's).
              let chosen = plan([], false), lowest = null;
              for (let n = 4 * fresh.length, from = floor; !chosen; n *= 4) {
                const range = from === null ? null : IDBKeyRange.lowerBound(from);
                const candidates = await request(db.transaction('chunks', 'readonly').objectStore('chunks').index('used').getAll(range, n));
                lowest = candidates.length ? candidates[0].used : null;
                chosen = plan(candidates, candidates.length < n);
                if (chosen && chosen.short && from !== null) { chosen = null; from = null; n = fresh.length; }
              }
              const { bytes, count, evicted, planned, refused } = chosen;
              if (refused.length) stats.refused += refused.length;
              // Nothing to store (every record refused, so nothing evicted): no dirty marker, no change.
              const low = floor === null || lowest === null ? floor : Math.max(floor, lowest);
              if (!planned.length) { floor = low; return prepared.length - fresh.length; }
              await transaction('readwrite', (_, totals) => totals.put({ ...state, dirty: true }, 'total'));
              stats.evictions += evicted.length;
              for (const key of evicted) await remove(key);
              // Atomic createWritable close publishes a complete record; dirty state
              // recovers orphan bytes or stale index entries after an interrupted write.
              const writes = await Promise.allSettled(planned.map(r => api.writeRecord(directory, r.key, r.bytes)));
              const failed = writes.find(w => w.status === 'rejected');
              if (failed) throw failed.reason;
              const used = Date.now();
              await transaction('readwrite', (chunks, totals) => {
                for (const key of evicted) chunks.delete(key);
                for (const r of planned) chunks.put({ key: r.key, bytes: r.size, used });
                totals.put({ bytes, count, dirty: false }, 'total');
              });
              // Every record stamped below the last victim was a victim (index order); the new records are stamped now.
              if (floor !== null) floor = Math.min(chosen.last === null ? low : Math.max(low, chosen.last), used);
              if (known) { for (const key of evicted) known.delete(key); for (const r of planned) known.add(r.key); }
              stats.writes += planned.length;
              return prepared.length - fresh.length + planned.length;
            }) || 0;
          } catch (_) { stats.errors++; return 0; }
        },
        // A record file's snapshot; reading it fails once the file is removed or replaced (getRun).
        openRecord: async function(dir, key) {
          return await (await dir.getFileHandle(key)).getFile();
        },
        writeRecord: async function(dir, key, bytes) {
          const writable = await (await dir.getFileHandle(key, { create: true })).createWritable();
          try { await writable.write(bytes); await writable.close(); }
          catch (error) { try { await writable.abort(); } catch (_) {} throw error; }
        },
        close: function() { if (db) db.close(); disabled = true; },
      };
      return api;
    },
  },
});
