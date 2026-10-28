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

// The admission state that must be consistent across all of the Worker's isolates, each kept in a Durable Object.
// An object handles one call at a time between its awaits, and its storage calls hold back other calls until they
// finish, so the check-then-set steps below cannot interleave. Each object keeps its entries in memory as well,
// loaded from storage when the object starts, so a restart or an eviction of the object loses nothing.
import { DurableObject } from 'cloudflare:workers';

// A relay renews its leases every LEASE_RENEW_MS while it runs. A lease not renewed for LEASE_STALE_MS belongs to a
// relay that ended without releasing it (its isolate was evicted or redeployed), and is dropped.
export const LEASE_RENEW_MS = 30_000;
export const LEASE_STALE_MS = 90_000;
// Spent join nonces are kept until their token's expiry has passed by this margin, which covers the clock
// difference between the Worker that checked the expiry and this object.
const NONCE_MARGIN_S = 5;
const NONCE_PRUNE_MS = 60_000;
// At most this many unexpired nonces per JoinNonces shard (64 shards). A token lives at most 305 s, so the bound is
// about 850 accepted join tokens a second for the whole gateway. Above it a handshake is refused (503); a spent
// nonce is never dropped before its token has expired.
export const NONCES_PER_SHARD = 4096;
const DELETE_BATCH = 128; // the most keys one storage.delete call takes

async function deleteKeys(storage, keys) {
  for (let i = 0; i < keys.length; i += DELETE_BATCH) await storage.delete(keys.slice(i, i + DELETE_BATCH));
}

class Leases extends DurableObject {
  // A counting semaphore of leases: lease id -> time of its last renewal (ms). Storage keys are "l:<id>".
  constructor(ctx, env) {
    super(ctx, env);
    this.leases = new Map();
    ctx.blockConcurrencyWhile(async () => {
      for (const [key, renewed] of await ctx.storage.list({ prefix: 'l:' })) this.leases.set(key.slice(2), renewed);
    });
  }

  async dropStale(now) {
    const stale = [];
    for (const [id, renewed] of this.leases) {
      if (now - renewed > LEASE_STALE_MS) stale.push(id);
    }
    for (const id of stale) this.leases.delete(id);
    await deleteKeys(
      this.ctx.storage,
      stale.map((id) => 'l:' + id),
    );
  }

  async acquire(limit) {
    // A new lease id, or null when `limit` leases are live.
    const now = Date.now();
    await this.dropStale(now);
    if (this.leases.size >= limit) return null;
    const lease = crypto.randomUUID();
    this.leases.set(lease, now);
    await this.ctx.storage.put('l:' + lease, now);
    if ((await this.ctx.storage.getAlarm()) === null) await this.ctx.storage.setAlarm(now + LEASE_STALE_MS);
    return lease;
  }

  async renew(lease) {
    // False when the lease is gone: it went stale and its slot may already belong to another relay.
    if (!this.leases.has(lease)) return false;
    const now = Date.now();
    this.leases.set(lease, now);
    await this.ctx.storage.put('l:' + lease, now);
    return true;
  }

  async release(lease) {
    this.leases.delete(lease);
    await this.ctx.storage.delete('l:' + lease);
  }

  async alarm() {
    const now = Date.now();
    await this.dropStale(now);
    if (this.leases.size) await this.ctx.storage.setAlarm(now + LEASE_STALE_MS);
  }
}

export class IpSlots extends Leases {
  // limits.connectionsPerIp: one object per client address key (address.js ipKey). It holds at most
  // connectionsPerIp leases, and none once its relays have ended.
}

export class Capacity extends Leases {
  // limits.connections: the relays open at once from all addresses, one object for the whole gateway.
}

export class JoinNonces extends DurableObject {
  // The spent nonces of join tokens (join_token.py Verifier), nonce -> token expiry (unix s), so a token admits one
  // connection. Sharded by the nonce's first character (gateway.js nonceShard). Storage keys are "n:<nonce>".
  constructor(ctx, env) {
    super(ctx, env);
    this.spent = new Map();
    ctx.blockConcurrencyWhile(async () => {
      for (const [key, expiry] of await ctx.storage.list({ prefix: 'n:' })) this.spent.set(key.slice(2), expiry);
    });
  }

  async dropExpired() {
    // A token past its expiry is refused before the replay check, so its nonce is no longer needed.
    const cutoff = Date.now() / 1000 - NONCE_MARGIN_S;
    const expired = [];
    for (const [nonce, expiry] of this.spent) {
      if (expiry < cutoff) expired.push(nonce);
    }
    for (const nonce of expired) this.spent.delete(nonce);
    await deleteKeys(
      this.ctx.storage,
      expired.map((nonce) => 'n:' + nonce),
    );
  }

  async spend(nonce, expiry) {
    // 'spent' when the nonce was fresh and is now recorded, 'replay' when it was already spent, 'full' when the shard
    // holds NONCES_PER_SHARD unexpired nonces.
    if (this.spent.size >= NONCES_PER_SHARD) await this.dropExpired();
    if (this.spent.has(nonce)) return 'replay';
    if (this.spent.size >= NONCES_PER_SHARD) return 'full';
    this.spent.set(nonce, expiry);
    await this.ctx.storage.put('n:' + nonce, expiry);
    if ((await this.ctx.storage.getAlarm()) === null) await this.ctx.storage.setAlarm(Date.now() + NONCE_PRUNE_MS);
    return 'spent';
  }

  async alarm() {
    await this.dropExpired();
    if (this.spent.size) await this.ctx.storage.setAlarm(Date.now() + NONCE_PRUNE_MS);
  }
}
