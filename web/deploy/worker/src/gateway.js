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

// GET /connect?host=..&port=.. with a WebSocket upgrade: plasma_web/gateway.py (check_and_connect, make_relay)
// on Workers. A connection is admitted in gateway.py's order, every check before any TCP connection: the client
// address, the connect rate per address, the route and the operator's upstream allowlist, Host, Origin, the join
// token, the concurrent connections per address, the gateway's global cap. Then the TCP connection to the shard is
// opened BEFORE the 101 answer. The native client reports a connection only once its TCP connect has succeeded
// (pnAcSocket.cpp AsyncSocketConnect, SocketInitConnect) and the browser client reports it on the WebSocket's open
// (pnAcSocketEmscripten.cpp plws_on_open), so an unreachable shard answers 502 and the client sees a close before
// open, the native kNetErrConnectFailed, not a connection that drops.
import { connect } from 'cloudflare:sockets';
import { IPV4, clientAddress, ipKey, ipv4Number } from './address.js';
import * as joinToken from './join_token.js';
import { LEASE_RENEW_MS } from './objects.js';

// The Plasma connection types a client may open (pnNpCommon.h EConnType: kConnTypeCliToAuth 10, kConnTypeCliToGame
// 11, kConnTypeCliToFile 16, kConnTypeCliToGateKeeper 22). A connection's first byte is its type; any other first
// byte closes the socket before anything reaches the shard.
const CONNECTION_TYPES = new Set([10, 11, 16, 22]);
const CONNECT_TIMEOUT_MS = 4000; // gateway.py CONNECT_TIMEOUT
const MAX_MESSAGE = 1024 * 1024; // gateway.py MAX_SIZE: the largest browser frame
const MAX_FRAME_OUT = 65536; // gateway.py READ_CHUNK: the largest frame sent to the browser
// gateway.py stops reading the browser's socket while MAX_QUEUE (16) received frames wait for the shard. A Worker
// cannot pause a WebSocket's incoming side, so the same bound, 16 frames of at most 1 MiB, is a byte cap on the frames
// waiting for the upstream token bucket. A browser that sends more closes with 1008.
const MAX_QUEUED = 16 * MAX_MESSAGE;
const LIMIT_KEYS = ['connectionsPerIp', 'connectsPerMinutePerIp', 'bytesPerSecond', 'idleSeconds', 'connections'];

export function parseConfig(env) {
  // The deployment's gateway settings, which emit_worker.py writes into the Worker's vars from the deployment file,
  // and the JOIN_KEY secret. Anything missing or malformed gives null, and every handshake and join request is then
  // refused with 500.
  try {
    const up = env.UPSTREAMS,
      limits = env.LIMITS;
    const cfg = {
      deployment: env.DEPLOYMENT,
      ttl: Number(env.JOIN_TTL),
      key: joinToken.importKey(env.JOIN_KEY),
      pairs: (up.pairs || []).map(([h, p]) => [String(h), Number(p)]),
      networkPort: up.networks ? Number(up.networks.port) : null,
      networks: (up.networks ? up.networks.cidrs : []).map((cidr) => {
        const [base, bits] = cidr.split('/');
        const prefix = Number(bits);
        if (!IPV4.test(base) || !(prefix >= 16 && prefix <= 32)) throw new Error('network');
        const mask = (0xffffffff << (32 - prefix)) >>> 0;
        return { base: (ipv4Number(base) & mask) >>> 0, mask };
      }),
      origins: env.ORIGINS.map(String),
      hosts: env.PUBLIC_HOSTS.map(String),
      limits: {},
    };
    for (const k of LIMIT_KEYS) {
      const v = Number(limits[k]);
      const positive = k === 'bytesPerSecond' || k === 'idleSeconds';
      if (!Number.isInteger(v) || v < 0 || (positive && v < 1)) throw new Error(k);
      cfg.limits[k] = v;
    }
    if (
      typeof cfg.deployment !== 'string' ||
      !/^[A-Za-z0-9_.-]{1,64}$/.test(cfg.deployment) ||
      !Number.isInteger(cfg.ttl) ||
      cfg.ttl < 10 ||
      cfg.ttl > 300 ||
      cfg.key === null ||
      !cfg.pairs.length ||
      !cfg.origins.length ||
      !cfg.hosts.length
    )
      return null;
    return cfg;
  } catch {
    return null;
  }
}

function parseQuery(search) {
  // Python's parse_qs(query, strict_parsing=True, keep_blank_values=True): '&'-separated name=value fields, each with
  // an '=', '+' as a space, percent-decoding; an empty query is {}. A malformed escape cannot name an allowlisted
  // host or port, so it is refused here (Python keeps it literally and refuses it at the allowlist).
  const query = new Map();
  if (search === '') return query;
  for (const field of search.split('&')) {
    const eq = field.indexOf('=');
    if (eq < 0) return null;
    let name, value;
    try {
      name = decodeURIComponent(field.slice(0, eq).replace(/\+/g, ' '));
      value = decodeURIComponent(field.slice(eq + 1).replace(/\+/g, ' '));
    } catch {
      return null;
    }
    if (!query.has(name)) query.set(name, []);
    query.get(name).push(value);
  }
  return query;
}

export function upstreamTarget(url, cfg) {
  // gateway.py upstream_target: the [host, port] allowlist entry the request may open, or null. The query's strings
  // are compared with the allowlist and are never the TCP target themselves.
  if (url.pathname !== '/connect' || url.hash) return null;
  const query = parseQuery(url.search.startsWith('?') ? url.search.slice(1) : url.search);
  if (
    query === null ||
    query.size !== 2 ||
    !query.has('host') ||
    !query.has('port') ||
    query.get('host').length !== 1 ||
    query.get('port').length !== 1
  )
    return null;
  const host = query.get('host')[0],
    port = query.get('port')[0];
  for (const entry of cfg.pairs) {
    if (host === entry[0] && port === String(entry[1])) return entry;
  }
  // extraUpstreamNetworks: a canonical IPv4 literal inside one of them, at the client port; the target is the
  // parsed address.
  if (cfg.networks.length && port === String(cfg.networkPort) && IPV4.test(host)) {
    const n = ipv4Number(host);
    if (cfg.networks.some((net) => (n & net.mask) >>> 0 === net.base)) return [host, cfg.networkPort];
  }
  return null;
}

function now() {
  return Date.now() / 1000;
}

function log(row) {
  console.log(JSON.stringify(row));
}

function refuse(request, url, ip, status, reason) {
  // gateway.py refuse: the status and reason, and one JSON log line (status, reason, route, origin, client address).
  // The route holds only the upstream host and port; the join token travels in Sec-WebSocket-Protocol and is never
  // logged.
  log({
    t: Math.round(now() * 1000) / 1000,
    refused: status,
    reason,
    path: url.pathname + url.search,
    origin: request.headers.get('Origin'),
    ip,
  });
  return new Response(reason + '\n', {
    status,
    headers: { 'Content-Type': 'text/plain; charset=utf-8', 'X-Content-Type-Options': 'nosniff', Connection: 'close' },
  });
}

export function nonceShard(env, nonce) {
  return env.JOIN_NONCES.get(env.JOIN_NONCES.idFromName('nonce-' + nonce.slice(0, 1)));
}

async function spendNonce(env, nonce, expiry) {
  // The replay check of joinToken.check: true when the nonce was fresh and is now spent.
  const result = await nonceShard(env, nonce).spend(nonce, expiry);
  if (result === 'full') throw new joinToken.Refusal(503, 'Gateway at capacity');
  return result === 'spent';
}

async function rateOk(env, cfg, key) {
  // gateway.py Admission.rate_ok: every handshake counts against its address first, refused ones included. The
  // rate-limiting binding CONNECTS carries connectsPerMinutePerIp.
  if (cfg.limits.connectsPerMinutePerIp <= 0) return false;
  const { success } = await env.CONNECTS.limit({ key });
  return success;
}

async function acquireSlots(env, cfg, key) {
  // gateway.py Admission.acquire: a lease on the address's slots (connectionsPerIp), then one on the gateway's
  // (connections). Returns the leases held, or the refusal [status, reason]; on a refusal nothing stays held.
  const perIp = env.IP_SLOTS.get(env.IP_SLOTS.idFromName(key));
  const ipLease = cfg.limits.connectionsPerIp > 0 ? await perIp.acquire(cfg.limits.connectionsPerIp) : null;
  if (ipLease === null) return [429, 'Too many concurrent connections from this address'];
  const global = env.CAPACITY.get(env.CAPACITY.idFromName('connections'));
  let globalLease = null;
  try {
    if (cfg.limits.connections > 0) globalLease = await global.acquire(cfg.limits.connections);
  } catch (e) {
    await perIp.release(ipLease);
    throw e;
  }
  if (globalLease === null) {
    await perIp.release(ipLease);
    return [503, 'Gateway at capacity'];
  }
  return [
    { stub: perIp, id: ipLease },
    { stub: global, id: globalLease },
  ];
}

function releaseSlots(slots) {
  return Promise.all(slots.map((s) => s.stub.release(s.id).catch(() => {})));
}

class Bucket {
  // gateway.py Bucket: one direction of one connection, rate bytes/s, a burst of one second.
  constructor(rate) {
    this.rate = rate;
    this.tokens = rate;
    this.t = now();
  }

  async take(n) {
    const t = now();
    this.tokens = Math.min(this.rate, this.tokens + (t - this.t) * this.rate) - n;
    this.t = t;
    if (this.tokens < 0) await new Promise((r) => setTimeout(r, (-this.tokens / this.rate) * 1000));
  }
}

export async function handleConnect(request, env, ctx) {
  const url = new URL(request.url);
  const cfg = parseConfig(env);
  const ip = clientAddress(request);
  if (cfg === null) return refuse(request, url, ip, 500, 'Gateway not configured');
  if (ip === null) return refuse(request, url, ip, 400, 'Client address missing from the proxy');
  const key = ipKey(ip);
  if (!(await rateOk(env, cfg, key))) {
    return refuse(request, url, ip, 429, 'Too many connections per minute from this address');
  }
  const target = upstreamTarget(url, cfg);
  if (target === null) return refuse(request, url, ip, 403, 'Unknown or unauthorized shard service');
  if (!cfg.hosts.includes(request.headers.get('Host'))) return refuse(request, url, ip, 403, 'Host not allowed');
  if (!cfg.origins.includes(request.headers.get('Origin'))) return refuse(request, url, ip, 403, 'Origin not allowed');
  const protocols = request.headers.get('Sec-WebSocket-Protocol');
  try {
    await joinToken.check(cfg.key, cfg.deployment, cfg.ttl, joinToken.offeredToken(protocols), (nonce, expiry) =>
      spendNonce(env, nonce, expiry),
    );
  } catch (e) {
    if (e instanceof joinToken.Refusal) return refuse(request, url, ip, e.status, e.reason);
    throw e;
  }
  const slots = await acquireSlots(env, cfg, key);
  if (typeof slots[0] === 'number') return refuse(request, url, ip, slots[0], slots[1]);
  // From here every path either hands the slots to the relay, which releases them when it ends, or releases them.
  let sock;
  try {
    if ((request.headers.get('Upgrade') || '').toLowerCase() !== 'websocket') {
      // The WebSocket server answers a request without the upgrade headers 426, after gateway.py's checks ran.
      ctx.waitUntil(releaseSlots(slots));
      return refuse(request, url, ip, 426, 'Upgrade Required');
    }
    try {
      // Nagle stays as the runtime sets it, for every connection type. gateway.py sets TCP_NODELAY per type, as the
      // native client does (pnNcCli.cpp NetCliConnectAccept, unbuffered only from plNglGame.cpp's CliGmConn),
      // but connect()'s SocketOptions have no such setting: Cloudflare documents secureTransport and allowHalfOpen
      // (https://developers.cloudflare.com/workers/runtime-apis/tcp-sockets/), and the pinned wrangler's workerd
      // (1.20261006.1) takes only those and highWaterMark.
      sock = connect({ hostname: target[0], port: target[1] }, { secureTransport: 'off', allowHalfOpen: false });
      let timer;
      await Promise.race([
        sock.opened,
        new Promise((_, reject) => {
          timer = setTimeout(() => reject(new Error('connect timeout')), CONNECT_TIMEOUT_MS);
        }),
      ]).finally(() => clearTimeout(timer));
    } catch {
      if (sock) sock.close().catch(() => {});
      ctx.waitUntil(releaseSlots(slots));
      return refuse(request, url, ip, 502, 'Shard connection failed');
    }
    const [client, server] = Object.values(new WebSocketPair());
    // Binary frames as ArrayBuffers; the standard default is Blob.
    server.binaryType = 'arraybuffer';
    server.accept();
    ctx.waitUntil(relay(server, sock, { target, limits: cfg.limits, ip, slots }));
    const headers = {};
    if (joinToken.offeredProtocols(protocols).includes(joinToken.PROTOCOL)) {
      headers['Sec-WebSocket-Protocol'] = joinToken.PROTOCOL;
    }
    return new Response(null, { status: 101, webSocket: client, headers });
  } catch (e) {
    if (sock) sock.close().catch(() => {});
    ctx.waitUntil(releaseSlots(slots));
    throw e;
  }
}

function relay(ws, sock, { target, limits, ip, slots }) {
  // gateway.py make_relay: binary frames only, the first byte a Plasma connection type, bytes copied verbatim both ways
  // through the per-direction token buckets, the idle timeout, and one JSON log line per connection (byte counts;
  // never payloads, accounts, query strings, tokens or credentials). Resolves once the slots are released.
  const writer = sock.writable.getWriter();
  const reader = sock.readable.getReader();
  const counts = { sent: 0, received: 0 };
  const started = Date.now();
  const up = new Bucket(limits.bytesPerSecond),
    down = new Bucket(limits.bytesPerSecond);
  let first = true,
    finished = false,
    idled = false,
    last = Date.now(),
    queued = 0;
  let chain = Promise.resolve(),
    idleTimer = null,
    renewTimer = null,
    resolveDone;
  const done = new Promise((r) => {
    resolveDone = r;
  });

  const closeWs = (code, reason) => {
    try {
      ws.close(code, reason);
    } catch {
      // already closed
    }
  };
  const finish = (how) => {
    if (finished) return;
    finished = true;
    const end = idled ? 'idle timeout' : how;
    clearTimeout(idleTimer);
    clearInterval(renewTimer);
    reader.cancel().catch(() => {});
    writer.abort().catch(() => {});
    sock.close().catch(() => {});
    log({
      t: Math.round(now() * 1000) / 1000,
      ip,
      upstream: `${target[0]}:${target[1]}`,
      sent: counts.sent,
      received: counts.received,
      seconds: Math.round(Date.now() - started) / 1000,
      end,
    });
    releaseSlots(slots).finally(resolveDone);
  };

  // While the relay runs it renews its leases. A lease that is gone went stale (three renewals failed) and its slot
  // may already be another relay's; the relay then closes, so the caps hold.
  renewTimer = setInterval(() => {
    for (const s of slots) {
      s.stub.renew(s.id).then(
        (held) => {
          if (!held && !finished) {
            closeWs(1011, 'Gateway capacity lease lost');
            finish('lease lost');
          }
        },
        () => {},
      );
    }
  }, LEASE_RENEW_MS);

  const idle = () => {
    // No payload byte in either direction for idleSeconds: close 4000. The client sees a disconnect, as when a native
    // TCP connection drops.
    const remaining = last + limits.idleSeconds * 1000 - Date.now();
    if (remaining <= 0) {
      idled = true;
      closeWs(4000, 'Idle timeout');
      finish('idle timeout');
      return;
    }
    idleTimer = setTimeout(idle, remaining);
  };
  idleTimer = setTimeout(idle, limits.idleSeconds * 1000);

  const upstream = async (data, size) => {
    if (finished) return;
    if (typeof data === 'string') {
      closeWs(1003, 'Binary Plasma protocol required');
      finish('error');
      return;
    }
    const bytes = new Uint8Array(data);
    if (first) {
      if (!bytes.length || !CONNECTION_TYPES.has(bytes[0])) {
        closeWs(1008, 'Connection type does not match route');
        finish('error');
        return;
      }
      first = false;
    }
    last = Date.now();
    await up.take(size);
    if (finished) return;
    await writer.write(bytes);
    counts.sent += size;
    queued -= size;
  };

  ws.addEventListener('message', (event) => {
    if (finished) return;
    const data = event.data; // read now: the event's data is not kept after this handler returns
    const size = typeof data === 'string' ? 0 : data.byteLength;
    if (size > MAX_MESSAGE) {
      // the reason the websockets library (gateway.py) sends
      closeWs(1009, `frame with ${size} bytes exceeds limit of ${MAX_MESSAGE} bytes`);
      finish('browser closed');
      return;
    }
    queued += size;
    if (queued > MAX_QUEUED) {
      closeWs(1008, 'Too much data queued');
      finish('browser closed');
      return;
    }
    chain = chain
      .then(() => upstream(data, size))
      .catch(() => {
        if (!finished) {
          closeWs(1011, 'Test shard connection unavailable');
          finish('upstream error');
        }
      });
  });
  ws.addEventListener('close', (event) => {
    if (finished) return;
    // Echo the browser's close code when it may be sent on the wire (RFC 6455 7.4.1 reserves 1004, 1005, 1006, 1015).
    const code = event.code;
    if (code >= 1000 && code < 5000 && ![1004, 1005, 1006, 1015].includes(code)) closeWs(code, event.reason);
    else closeWs(1000, '');
    finish('browser closed');
  });
  ws.addEventListener('error', () => finish('browser closed'));

  (async () => {
    try {
      for (;;) {
        const { value, done: eof } = await reader.read();
        if (eof) break;
        for (let o = 0; o < value.byteLength; o += MAX_FRAME_OUT) {
          const piece = value.subarray(o, o + MAX_FRAME_OUT);
          last = Date.now();
          await down.take(piece.byteLength);
          if (finished) return;
          ws.send(piece);
          counts.received += piece.byteLength;
        }
      }
      if (!finished) {
        closeWs(1000, 'Shard disconnected');
        finish('shard closed');
      }
    } catch {
      if (!finished) {
        closeWs(1011, 'Test shard connection unavailable');
        finish('upstream error');
      }
    }
  })();
  return done;
}
