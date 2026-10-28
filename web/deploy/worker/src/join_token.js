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

// The join-token scheme of plasma_web/join_token.py on Web Crypto. The wire format is the same, so one browser
// build works with either gateway:
//
//   v1.<payload>.<mac>   payload = base64url(JSON {"d": deployment name, "e": expiry (unix s), "n": 16-byte nonce})
//                        mac     = base64url(HMAC-SHA256(key, "plasma-join-v1." + payload))
//
// The key is the Worker secret JOIN_KEY, 64 lowercase hex characters; it is never in the Worker's configuration, the
// repository or a log. A token admits one connection: its nonce is spent in the JoinNonces Durable Object
// (objects.js), which every isolate of the Worker shares.

export const PREFIX = 'plasma.join.';
export const PROTOCOL = 'plasma';
export const MAX_TOKEN = 512;
export const SKEW = 5; // seconds of clock skew accepted on a future expiry (join_token.py SKEW)
const TOKEN_RE = /^v1\.[A-Za-z0-9_-]{1,400}\.[A-Za-z0-9_-]{43}$/;
const KEY_RE = /^[0-9a-f]{64}$/;
// A JSON number written as an integer: no fraction, no exponent. json.loads makes such a literal an int and any
// other number a float, which join_token.py refuses as an expiry; JSON.parse makes 30, 30.0 and 3e1 the same number,
// so the expiry's source text decides.
const INTEGER_LITERAL = /^-?(0|[1-9][0-9]*)$/;
const B64URL = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
const encoder = new TextEncoder();

export class Refusal extends Error {
  // A refused token: the status (401, 403, or 503 when the replay store is full) and the reason, logged and returned
  // to the browser (join_token.py Refusal).
  constructor(status, reason) {
    super(reason);
    this.status = status;
    this.reason = reason;
  }
}

export function b64url(bytes) {
  let out = '';
  let i = 0;
  for (; i + 2 < bytes.length; i += 3) {
    const v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out += B64URL[v >> 18] + B64URL[(v >> 12) & 63] + B64URL[(v >> 6) & 63] + B64URL[v & 63];
  }
  if (bytes.length - i === 1) {
    const v = bytes[i] << 16;
    out += B64URL[v >> 18] + B64URL[(v >> 12) & 63];
  } else if (bytes.length - i === 2) {
    const v = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out += B64URL[v >> 18] + B64URL[(v >> 12) & 63] + B64URL[(v >> 6) & 63];
  }
  return out; // unpadded, as join_token.py _b64 (rstrip '=')
}

export function unb64url(text) {
  // join_token.py _unb64: urlsafe_b64decode after re-padding. Python refuses a length of 1 mod 4 (binascii.Error) and
  // ignores the unused low bits of the last character; the callers only pass [A-Za-z0-9_-] (the token pattern).
  if (text.length % 4 === 1) return null;
  const out = new Uint8Array(Math.floor((text.length * 3) / 4));
  let bits = 0,
    acc = 0,
    o = 0;
  for (const c of text) {
    const v = B64URL.indexOf(c);
    if (v < 0) return null;
    acc = (acc << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[o++] = (acc >> bits) & 255;
    }
  }
  return out.subarray(0, o);
}

const keys = new Map();
export function importKey(hex) {
  // The JOIN_KEY secret as an HMAC-SHA256 key; refuses anything but 64 lowercase hex characters (join_token.py
  // read_secret). Never logged; the error names the variable, never its content.
  if (typeof hex !== 'string' || !KEY_RE.test(hex.trim())) return null;
  hex = hex.trim();
  let p = keys.get(hex);
  if (!p) {
    const raw = new Uint8Array(32);
    for (let i = 0; i < 32; i++) raw[i] = parseInt(hex.slice(2 * i, 2 * i + 2), 16);
    p = crypto.subtle.importKey('raw', raw, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign', 'verify']);
    keys.set(hex, p);
  }
  return p;
}

function macInput(payload) {
  return encoder.encode('plasma-join-v1.' + payload);
}

export async function issue(key, deployment, ttl, now = Date.now() / 1000) {
  // A new token for this deployment; returns [token, expiry] (join_token.py issue: expiry = int(now + ttl)).
  const expiry = Math.floor(now + ttl);
  const nonce = b64url(crypto.getRandomValues(new Uint8Array(16)));
  // json.dumps(..., separators=(',', ':')) of {"d", "e", "n"} in that order; the name is [A-Za-z0-9_.-]
  // (deployment.py), so JSON.stringify gives the same bytes.
  const payload = b64url(encoder.encode(JSON.stringify({ d: deployment, e: expiry, n: nonce })));
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', await key, macInput(payload)));
  return [`v1.${payload}.${b64url(mac)}`, expiry];
}

export async function check(key, deployment, ttl, token, spend, now = () => Date.now() / 1000) {
  // join_token.py Verifier.check: accepts and spends the token, or throws Refusal. Order: shape, MAC, deployment,
  // expiry, replay. `spend(nonce, expiry)` is the replay store: true when the nonce was fresh and is now spent; it may
  // throw a Refusal itself.
  if (token === null || token === undefined) throw new Refusal(401, 'Join token required');
  if (token.length > MAX_TOKEN || !TOKEN_RE.test(token)) throw new Refusal(401, 'Invalid join token');
  const [, payload, mac] = token.split('.');
  const macBytes = unb64url(mac);
  let claims = null;
  const expirySources = new Map(); // holder object -> the source text of its "e" member
  let ok =
    macBytes !== null &&
    macBytes.length === 32 &&
    (await crypto.subtle.verify('HMAC', await key, macBytes, macInput(payload)));
  if (ok) {
    const raw = unb64url(payload);
    try {
      // The reviver's context.source is the literal text of a primitive value (JSON.parse source text access).
      claims =
        raw === null
          ? null
          : JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(raw), function (name, value, context) {
              if (name === 'e') expirySources.set(this, context?.source);
              return value;
            });
    } catch {
      ok = false;
    }
    if (raw === null) ok = false;
  }
  if (
    !ok ||
    claims === null ||
    typeof claims !== 'object' ||
    Array.isArray(claims) ||
    Object.keys(claims).sort().join(',') !== 'd,e,n' ||
    !Number.isInteger(claims.e) ||
    !INTEGER_LITERAL.test(expirySources.get(claims) ?? '') ||
    typeof claims.n !== 'string' ||
    typeof claims.d !== 'string'
  ) {
    throw new Refusal(401, 'Invalid join token');
  }
  if (claims.d !== deployment) throw new Refusal(403, 'Join token for another deployment');
  const t = now();
  if (claims.e < t) throw new Refusal(401, 'Join token expired');
  if (claims.e > t + ttl + SKEW) throw new Refusal(401, 'Invalid join token');
  if (!(await spend(claims.n, claims.e))) throw new Refusal(401, 'Join token already used');
}

export function offeredToken(header) {
  // join_token.py offered_token: the token from the offered Sec-WebSocket-Protocol values (null if absent; '' when
  // two are offered, which check() refuses as invalid). Workers join repeated header lines with ", ", which the comma
  // split below treats exactly as Python's loop over separate values.
  if (header === null || header === undefined) return null;
  let found = null;
  for (let item of header.split(',')) {
    item = item.trim();
    if (item.startsWith(PREFIX)) {
      if (found !== null) return '';
      found = item.slice(PREFIX.length);
    }
  }
  return found;
}

export function offeredProtocols(header) {
  return header === null || header === undefined ? [] : header.split(',').map((s) => s.trim());
}
