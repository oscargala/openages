# Deploying the browser client

This document is for an operator who wants to offer the browser client to the
players of a shard. It describes the deployment file, the join key, the two ways
to host a deployment (your own servers, or a static kit with the gateway
Worker), TLS and reverse proxies, the limits, what the gateway's admission does
and does not protect against, what the servers log, the fonts and the HD texture
packs, and what a player's browser must support.

`Architecture.md` in this folder describes the components themselves: the
client, the page, the gateway and the deployment tools.

## What a deployment is made of

A deployment serves one shard. Its parts:

- **the page**: `index.html`, `boot.mjs`, the runtime configuration module
  `runtime/config.mjs` and the input modules, all from the repository;
- **the client**: a product build of `plClient.js` and `plClient.wasm`, or of
  `plClient-py2.js` and `plClient-py2.wasm` for a shard whose game code is
  Python 2.3 (see `Python 2.3 flavour.md`). A build with the test hooks
  (`PLASMA_TEST_HOOKS` on) is never deployed;
- **the page document** `/runtime/config.json` and the shard's own `server.ini`
  at `/runtime/server.ini`. The deployment tools derive both from the
  deployment file; the page refuses a document that does not come from its own
  origin;
- **the join endpoint** `POST /runtime/join` on the page's own origin, which
  issues the gateway's admission tokens;
- **the status relay** `/runtime/status` on the page's own origin, when the
  shard's `server.ini` names a `Server.Status` URL: the login dialog's status
  text, which the page cannot read from the shard's own host;
- **the gateway**, which relays the client's WebSockets to the shard's TCP
  ports;
- **the game files**: either fetched by the client's original patcher from the
  shard's file server, through the gateway (`data.files` `patcher`, the
  production path), or a tree of the game's files served over HTTP;
- **the fonts** of the client's debug and loading text, and optionally the
  sound tree and the HD texture packs;
- **the source and licence files**: `source.json`, `LICENSE.txt`,
  `COPYING.txt` and `THIRD_PARTY_NOTICES.md`.

The tools are one Python package, `web/deploy/plasma_web`. There are two ways
to host a deployment with them:

1. **Your own servers.** The origin server (`server.py`) serves the page, the
   client, the page document, the join endpoint and the trees; the gateway
   (`gateway.py`) relays the sockets. Both read the same deployment file.
2. **A static kit and the gateway Worker.** `emit_static.py` writes the page,
   the client and the documents into a folder. The gateway Worker, a Cloudflare
   Worker on the page's own hostname, serves that folder and answers the join
   endpoint, the status relay and the gateway's sockets.

## The deployment file

The deployment file is a JSON file of at most 64 KiB, read by every tool through
`deployment.load`. It is server-side only: no tool serves it, and it holds no
credentials. An unknown key, a value of the wrong kind or a missing file stops
the tool with a message that says what is wrong. Relative paths are resolved against
the file's own folder, and `~` is expanded.

A deployment for a shard whose players patch from its file server:

```json
{
 "schema": 1,
 "name": "example-shard",
 "serverIni": "server.ini",
 "data": {"files": "patcher", "gameCode": "server"},
 "gateway": {
  "url": "wss://play.example.org",
  "origins": ["https://play.example.org"],
  "publicHosts": ["play.example.org"],
  "join": {"secretFileEnv": "PLASMA_JOIN_SECRET_FILE"}
 },
 "source": {"repository": "https://example.org/your/fork"}
}
```

To check a file, print its summary: `plasma-web-deployment <file>` writes what
the tools will use as JSON (the hosts the gateway may open, the fonts it will
serve, the limits in force) and exits with 1 and the reason when the file is
refused.

### Top level

| Key | Meaning |
|---|---|
| `schema` | must be 1 |
| `name` | the deployment's name, 1 to 64 of `A-Z a-z 0-9 _ . -`; default the file's name without its extension. The login dialog keys the remembered login by it, and every join token names it |
| `serverIni` | the shard's own `server.ini`, as its native players use it (below) |
| `data`, `fonts`, `sound` | where the client's files come from |
| `gateway` | the gateway's address, its page origins, its upstreams, its admission and its limits |
| `capabilities` | test settings; a player deployment names none |
| `graphics` | the first-run `graphics.ini` |
| `textures` | the HD texture packs |
| `source` | where the source of this client is published |

### `data`

| Key | Meaning |
|---|---|
| `files` | `patcher`: the client's original patcher fills a per-deployment install directory in the browser's storage from the shard's file server. `http` (the default): the client reads a tree of game files served over HTTP |
| `root` | a local tree of game files. With `http` it is the game data; with `patcher` it is optional and holds the client package, the files a native installer carries beyond the shard's manifests |
| `url` | the same tree on another origin (an `https://` URL), instead of serving it next to the page |
| `urlPrefix` | the path the origin server serves `root` under; default `/data` |
| `gameCode` | where the game's Python and SDL come from: `server` (the shard's file server, the default), `server-py2` (the same, run by the Python 2.3 build; `patcher` only), `local` (the origin server also serves `root`'s own `python/` and `SDL/`; not with `patcher`). `client` and `client-python` are test modes: the product's tools do not know them, and only the test harness (`testhooks/web/`) accepts them, for a loopback test deployment |
| `gameCodeDir` | only with the test modes (the test harness's key) |

`server-py2` selects the `plClient-py2` build: the page loads it, and the
origin server and the kit serve it under its own name.

### `fonts`

The client draws its debug and loading text with two faces, "Courier New" and
"Trebuchet MS Bold", which it opens as `/fonts/<face>.ttf`. The `fonts` entry
says which file is served under each name:

| Key | Meaning |
|---|---|
| none | the free substitutes the repository carries (see Fonts) |
| `root` | a folder of your own font files |
| `map` | with `root`: the file in `root` for a face, for example `{"Courier New": "cour.ttf", "Trebuchet MS Bold": "trebucbd.ttf"}`. A face the map leaves out is `<face>.ttf` in `root`. File names are plain names in `root` |
| `url` | the fonts tree on another origin (`https://`), which serves each face as `<face>.ttf` with its `.fetch-manifest` |
| `hostCopies` | a test setting: this machine's own copies of the original faces, on a loopback test deployment only. It excludes the other keys |

### `sound`

`root` is a local sound tree served at `/sound` (otherwise the origin server
serves the data tree's `sfx/` there), and `url` the same tree on another origin.

### `gateway`

| Key | Meaning |
|---|---|
| `url` | the gateway's address for the page: `wss://`, or `ws://` on a loopback host; no credentials or query. The client opens `<url>/connect?host=<host>&port=<port>` |
| `bind`, `port` | where `gateway.py` listens; default `127.0.0.1`, and port 8766 when `port` is 0 or absent |
| `origins` | the exact page origins the gateway admits and the join endpoint serves: `https://<host>[:port]`, or `http://` on `127.0.0.1` or `localhost`; lower-case, no path, at most 16 |
| `publicHosts` | the `Host` values (host, or host and port) under which the gateway is reached. `gateway.py` also accepts `127.0.0.1:<port>` and `localhost:<port>` of its own port |
| `extraUpstreamHosts` | hosts the gateway may open besides those in `server.ini` |
| `extraUpstreamNetworks` | IPv4 networks (CIDR, `/16` or narrower) whose addresses the gateway may open at the client port, for a shard whose game servers the auth server names by addresses that are not fixed |
| `resolveUpstreams` | default `true`: the `server.ini` hosts and the extra hosts are resolved to their IPv4 addresses when the tool starts, since the auth server names a game server by address |
| `tlsCert`, `tlsKey` | a certificate chain and its key for `gateway.py` to serve `wss://` itself; both or neither |
| `trustedProxies` | the networks (CIDR, IPv4 or IPv6, not `/0`) of the reverse proxies in front of the gateway and the origin server (see TLS and reverse proxies) |
| `join` | the admission tokens: `secretFile` or `secretFileEnv` (exactly one), and `ttlSeconds` (10 to 300, default 60). Required for every deployment that is not a loopback test one |
| `limits` | see Limits |

The upstreams, the hosts and port the gateway may open, come from the shard's
`server.ini`: `Server.Auth.Host`, `Server.Gate.Host` and `Server.File.Host`,
each at the one client port `Server.Port` (default 14617), plus the extra hosts
and networks above. A request for any other host or port is refused.

### `capabilities`

`harness` and `gameCodeCapture` belong to the repository's test harness. Both
need a loopback `gateway.bind`, the static kit refuses both, the origin server
refuses `gameCodeCapture`, and a player deployment names neither.

### `graphics`

`initialIni` is the first-run `graphics.ini` the page writes into a new
profile, only when the profile has none. It holds the settings the native
client's default writer writes, each required, and the shadow distance, which
is optional:

| Setting | Value |
|---|---|
| `Graphics.Windowed`, `Graphics.EnableVSync` | `true` or `false` |
| `Graphics.Shadow.Enable`, `Graphics.EnablePlanarReflections` | `true` or `false` (written as 1 or 0) |
| `Graphics.AntiAliasAmount`, `Graphics.AnisotropicLevel` | 0 to 16 |
| `Graphics.TextureQuality` | 0 to 2 |
| `Quality.Level` | 0 to 3 |
| `Graphics.Shadow.VisibleDistance` | optional, 0 to 1 |

The page adds the size, from the canvas, as the native writer takes it from the
display mode.

### `textures`

`hdPack` names the HD texture packs: exactly one of `root` (a folder of
`<Age>.hdpack` files outside the repository, each listed Age's file present) and
`url` (an `https://` origin that serves them), and `ages`, 1 to 64 Age names.
See HD texture packs.

### `source`

`repository` (an `https://` URL), `tag` (a Git tag) and `thirdPartySources`
(an `https://` URL of the source bundle of the third-party components) go into
`source.json`. See Source code and licence files.

### The shard's `server.ini`

The deployment names the shard's own `server.ini`, the file its native players
use. The tools check it before they serve it, the page checks it again against
the SHA-256 that the page document pins, and the client then reads it with the
original `pfServerIni`. The accepted form is the stricter of what the client and
the tools read:

- plain UTF-8 text of at most 64 KiB (the encrypted form is not served);
- one setting per line, among `Server.Status`, `Server.Signup`,
  `Server.DispName`, `Server.Port`, `Server.File.Host`, `Server.Auth.Host`,
  `Server.Auth.N`, `.X`, `.G`, `Server.Game.N`, `.X`, `.G`, `Server.Gate.Host`,
  `Server.Gate.N`, `.X`, `.G`, each at most once;
- a comment line starts with `#` in its first column; a setting may end with a
  comment (`#`, `;` or `//`), after a space or tab when its value is bare;
- a value is in double quotes, or bare; a bare value holds no `=`, `,`, `#` or
  `;`. Quote a value that needs one;
- the six N and X keys are present, each 64 bytes in base64 (88 characters
  ending in `==`); the G values are positive integers; `Server.Port` is 1 to
  65535;
- `Server.Auth.Host` or `Server.Gate.Host` names the host the client connects to
  first.

The values of `server.ini` are public: the page and the client read them.

## The join key

Every deployment that is not a loopback test one needs a join key. The origin
server signs each join token with it, and the gateway checks the token with it.

- **Where it lives.** A file outside the repository: the tools refuse a key file
  inside it. `gateway.join.secretFile` names the file, or
  `gateway.join.secretFileEnv` names an environment variable that holds the
  file's absolute path, so that a deployment file kept in Git names no path of
  one machine.
- **Creating it.** `plasma-web-deployment --init-join-secret <deployment>`
  writes 32 random bytes as 64 lowercase hex characters, with mode 0600, and
  never prints them. It checks an existing file and leaves it as it is.
- **Reading it.** The origin server and the gateway each read it once, at start,
  and refuse a file that gives the group or others any access, or that does not
  hold 64 lowercase hex characters. Neither prints or logs it.
- **Changing it.** Write a new file and restart the origin server and the
  gateway together. A token signed with the old key is refused by a gateway that
  holds the new one.
- **The Worker** keeps its key as the Worker secret `JOIN_KEY`, in the same
  format (64 lowercase hex characters), and both issues and checks its tokens
  with it.

The issuer and the gateway may run on different machines. A token's expiry is
checked against the gateway's clock, which may be at most 5 seconds behind the
issuer's, so keep both clocks synchronised.

## Hosting it yourself: the origin server and the gateway

### Installing the package

The package runs from a checkout of the repository, since it serves the
checkout's page files and fonts. Install it in editable form with Python 3.13 or
newer, `pip install -e web/deploy`, which also installs its one dependency
(`websockets`, pinned in `web/deploy/requirements.txt`) and the commands
`plasma-web-server`, `plasma-web-gateway`, `plasma-web-deployment` and
`plasma-web-emit-static`. `PYTHONPATH=web/deploy` with `python -m plasma_web.<module>`
works as well.

### The origin server

```
plasma-web-server --deployment <file> --build-dir <client build folder> \
    --access-log <file> [--bind <address>] [--port <port>] \
    [--tls-cert <chain.pem> --tls-key <key.pem>] [--ca-file <bundle.pem>]
```

It serves, on one origin:

- `/`, `/index.html`, `/boot.mjs`, `/input/*.mjs` and `/runtime/config.mjs`:
  the page;
- `/build/<client>.js` and `.wasm`: the client the deployment selects, from
  `--build-dir`;
- `/runtime/config.json`, the page document, and `/runtime/server.ini`;
- `/runtime/status`: the login dialog's status text, relayed from the
  `Server.Status` URL in `server.ini` (the page cannot read that URL across
  origins). The answer is reused for 10 seconds. An `https` status URL is
  verified against `--ca-file`, or Python's default store; a Python from
  python.org has no store of its own, so name the system's bundle;
- `POST /runtime/join`, when the deployment has `gateway.join`;
- `/data/`, `/sound/` and `/fonts/`: the trees, each with its listing
  `.fetch-manifest`, and `/hdpack/<Age>.hdpack`: files with strong content
  ETags, conditional requests and byte ranges. `/data/.asset-catalog` lists
  every data file's size and ETag. Their answers say `no-transform`, so a CDN in
  front of the server leaves them uncompressed. With the free substitutes,
  `/fonts/` also serves their licence texts (`LICENSE-*.txt`), as the kit
  carries them;
- `/source.json`, `/LICENSE.txt`, `/COPYING.txt`, `/THIRD_PARTY_NOTICES.md`.

Its answers to `GET`, `HEAD` and the join endpoint carry
`Cross-Origin-Opener-Policy: same-origin`,
`Cross-Origin-Embedder-Policy: require-corp` and
`Cross-Origin-Resource-Policy: same-origin`. With `--deployment`, its `GET` and
`HEAD` answers also carry the deployment's Content-Security-Policy, whose
`connect-src` names only the page's own origin and the gateway, join, status
and tree origins the page document names. Every answer carries
`X-Content-Type-Options: nosniff`.

Other options:

- `--bind` (default `127.0.0.1`) and `--port` (default 8613). A bind that is not
  loopback needs `--deployment`.
- `--gateway-url`: the gateway's address when the deployment file has no
  `gateway.url`.
- `--asset-origin <origin>` (repeatable): an exact page origin on another site
  that may read the trees through CORS. Answers to it expose the headers the
  client reads, `ETag`, `Content-Length` and `Content-Range` among them.
- `--timeout` and `--max-connections`: see Limits.

The server prints, at start, the shard's entry host and port, the `server.ini`
SHA-256, the gateway URL, which font files it serves, the source link it
publishes and the size of the data manifest.

### The gateway

```
plasma-web-gateway --deployment <file> [--port <port>] [--log <file>]
```

It listens on `gateway.bind` and `gateway.port` (or `--port`), with TLS when the
deployment names `gateway.tlsCert` and `gateway.tlsKey`, and prints one line
with its policy: the upstreams, the page origins, whether join tokens are
required, the trusted proxies and the limits. A deployment that lists no
`gateway.origins` takes `--origin` instead, for loopback page origins only; one
that has neither is refused.

The gateway answers the WebSocket upgrade only after it has opened the TCP
connection to the shard, so an unreachable shard is a refused handshake (502),
which the client reports as a failed connection, as the native client does.

It turns Nagle's algorithm off on its TCP connection to the shard for the game
connection only, and leaves it on for the auth, gatekeeper and file
connections, as the native client does; a gateway hosted in the Worker cannot
choose it per connection, because Cloudflare's `connect()` takes no such option
(its documented `SocketOptions` are `secureTransport` and `allowHalfOpen`,
https://developers.cloudflare.com/workers/runtime-apis/tcp-sockets/), so the
Worker's TCP leg keeps the platform's default.

### A development server

For a deployment on one machine, bind both servers to loopback: a deployment
with `gateway.bind` `127.0.0.1`, `gateway.url` `ws://127.0.0.1:8766` and
`gateway.origins` `["http://127.0.0.1:8613"]`, served by `plasma-web-server
--deployment <file>` on its default port and by `plasma-web-gateway` with the
same file. It still needs a join key. A page on a loopback host may use
`http://` and `ws://`. `Running locally.md` walks through such a deployment,
from the Python environment to the browser.

Without `--deployment`, the origin server serves a local game tree
(`--data-root`, required) on loopback only, and `/runtime/config.json` only when
`--runtime-config` names a page document written by hand. The page does not
start without a page document.

## A static kit and the gateway Worker

### The kit

```
plasma-web-emit-static <deployment> <out folder> [--gateway-url wss://...] \
    [--build-dir <client build folder>]
```

writes into an empty folder:

- the page and its modules, unchanged;
- `build/<client>.js` and `.wasm` with `--build-dir`, after checking that they
  are the build the deployment selects (by its Python version) and a product
  build: a client built with the test hooks is refused;
- `runtime/config.json` and `runtime/server.ini`. When `server.ini` names a
  `Server.Status` URL, the page document names the status relay
  `/runtime/status` on the page's own origin, as the origin server's document
  does, so the Content-Security-Policy needs no origin for the status host;
- `_headers`: the isolation headers, nosniff, the Content-Security-Policy and
  the cache policy, in the `_headers` format of Pages-style static hosts;
- `fonts/`, unless the deployment names `fonts.url`: each face as
  `<face>.ttf`, the listing `fonts/.fetch-manifest`, and with the free
  substitutes their licence files;
- `LICENSE.txt`, `COPYING.txt`, `THIRD_PARTY_NOTICES.md` and `source.json`.

Beside the folder it writes `<out folder>.publish-report.json`: every kit file's
size, the files over 25 MiB, which roots the page expects next to it, and the
size classes of the data tree. The kit carries no game files, no game code and
no HD packs. Test deployments, and the test game-code modes, are refused.

### What a host of the trees must serve

The client mounts each tree it reads (fonts, data, sound) from its listing,
`<base>/.fetch-manifest`, a file whose name starts with a dot: make sure the
host serves it. A tree whose listing is missing or empty stops the client's
start. For each file of a tree the client requires:

- a `HEAD` answer with a strong `ETag` (not `W/`), a `Content-Length`, and no
  content encoding: serve these files uncompressed;
- byte ranges: a `GET` with `Range` and `If-Match: <that ETag>` must answer 206
  with the exact `Content-Range`, or 200 with the whole file; the same `ETag`
  on every answer;
- no redirects.

A host that sends `Cache-Control: private, no-cache` and
`X-Plasma-Asset-Cache: chunks-v1` on the game trees, as the origin server does,
lets the client keep the chunks it has read between sessions; without them, it
keeps them for the session only. A tree on another origin must also answer the
page's origin with CORS and expose `ETag` and `Content-Range`, since the page
is cross-origin isolated.

### The Worker

The page document names `/runtime/join` on the page's own origin, and
`/runtime/status` when the shard has a status URL; a static host cannot answer
either. The gateway Worker serves the kit, the join endpoint, the status relay
and the gateway's sockets on the page's hostname, with the headers the origin
server sends. It answers `If-Match` itself, and byte ranges in the trees the
client reads by range (fonts, data, sound); elsewhere it ignores
`Range` and sends the whole file. It adds `no-transform` to the Cache-Control of
the trees and of the page document: without it, Cloudflare's edge compresses the
fonts, weakening their ETags, and removes the page's ETag. The page's modules
and the provenance files keep the edge's compression, so a browser gets them
with a weak ETag (`W/`), which revalidates them all the same (304). Its folder
holds `wrangler.toml` (its fixed shape, with no deployment value or secret), its
sources, and `emit_worker.py`, which turns a
deployment file and its kit into a Worker configuration (run it with the
package on the import path):

```
python3 emit_worker.py <deployment> <kit folder> <out folder> [--name <worker name>]
```

`<out folder>` receives `wrangler.json` and `assets/`, the kit without
`_headers` (the Worker sets the headers itself). The configuration carries the
deployment's name and token lifetime, the upstreams, the origins, the public
hosts, the limits, the status URL and the Content-Security-Policy. The
upstreams are resolved when `emit_worker.py` runs: run it again when a shard
host's address changes.

`emit_worker.py` refuses a deployment without `gateway.join`, `gateway.origins`
or `gateway.publicHosts`, a test deployment, and one with
`gateway.trustedProxies`, since Cloudflare sets the client's address itself.
It also refuses a kit whose page document does not name the status relay
exactly when the deployment has a status URL (a kit of another deployment, or
one written before the relay existed): emit the kit again.

The status relay answers `GET` and `HEAD /runtime/status` as the origin server
does. It fetches the `Server.Status` URL of the deployment's `server.ini`, never
a URL from the request, and answers the body whatever the HTTP status, at most
255 bytes, following redirects; a request that fails or takes more than 5
seconds answers its error text instead. Each Worker instance fetches the URL
once at a time and reuses the answer for 10 seconds, so an anonymous request
costs no request to the shard's status host beyond one per instance every 10
seconds. An `https` status URL is checked against Cloudflare's trust store;
there is no `--ca-file`. The Worker fetches it from Cloudflare's network, so the
status host must be reachable from the internet. A private address, such as a
test shard's on a LAN, is not, and the dialog then shows the failed request's
error text. A plain `http` URL on its own port works under `wrangler dev`; on
Cloudflare this has not been measured, since nothing was deployed for it.

Before the Worker can admit anyone, set its secret with `wrangler secret put
JOIN_KEY`. Without it, the Worker refuses every join and connect request with
500; without the generated configuration, it refuses the page too. The
Worker uses three Durable Object classes (`JoinNonces` for spent tokens,
`IpSlots` and `Capacity` for the connection caps) and two rate-limit bindings
(`CONNECTS` and `JOINS`), whose namespace ids must be unique in the Cloudflare
account. Attach the Worker to the page's hostname, which must be the one in
`gateway.publicHosts`. The shipped `wrangler.toml` turns the Worker's
`workers.dev` address off.

## TLS and reverse proxies

A page that is not on a loopback host must be served over HTTPS, and its gateway
over `wss://`: the page origins and `gateway.url` accept nothing else, and a
browser grants WebGPU and cross-origin isolation only to a secure page.

- **TLS in the tools.** The origin server serves HTTPS with `--tls-cert` and
  `--tls-key`, the gateway with `gateway.tlsCert` and `gateway.tlsKey`; both
  accept TLS 1.2 or newer. Use a certificate the players' browsers trust.
- **Behind a TLS-terminating proxy.** The servers may listen on loopback over
  plain HTTP while a proxy serves the public names. The join endpoint and the
  gateway compare the request's `Origin` with `gateway.origins`, never with the
  `Host` header, so list the public page origin there. The gateway compares
  `Host` with `gateway.publicHosts`, so list the public name there. The proxy
  must pass the WebSocket upgrade, `Origin`, `Host` and
  `Sec-WebSocket-Protocol` unchanged, and must not remove the isolation headers
  or compress the trees.
- **The client's address.** The rate and connection limits count per client
  address. Behind a proxy every request comes from the proxy, so list the
  proxy's networks in `gateway.trustedProxies`: from a peer in one of them, the
  origin server and the gateway take the client's address from the
  `CF-Connecting-IP` header, and refuse a request that carries none or several
  (400). From any other peer they ignore that header. A proxy other than
  Cloudflare must set the header itself and replace any value the client sent.

## Limits

The gateway's limits, `gateway.limits`, with their defaults:

| Limit | Default | Meaning |
|---|---|---|
| `connectionsPerIp` | 8 | open connections per client address. A client holds up to four sockets, and a link opens the new game socket before the old one closes |
| `connectsPerMinutePerIp` | 30 | new connections per client address in a sliding minute. A fresh start opens five, each link one |
| `bytesPerSecond` | 8 MiB | per connection and direction, with a burst of one second |
| `idleSeconds` | 120 | a connection with no payload byte in either direction for this long is closed (code 4000); every Plasma connection pings every 30 seconds |
| `connections` | 64 | open connections of the whole gateway |
| `memoryBytes` | 1 GiB | `gateway.py` only: each connection reserves about 18 MB of buffers, so the default admits 58 connections |

A value of 0 for `connectionsPerIp`, `connectsPerMinutePerIp`, `connections` or
`memoryBytes` refuses every connection; `bytesPerSecond` and `idleSeconds` are
at least 1. Size `connections` to your shard: a player holds three or four
sockets.

Other bounds:

- **A connection.** Frames from the browser are binary and at most 1 MiB, and a
  connection's first byte must be one of the client's connection types (auth,
  game, file, gatekeeper). The TCP connection to the shard must open within 4
  seconds.
- **The join endpoint** issues at most twice `connectsPerMinutePerIp` tokens
  per client address and minute, to `POST` requests with a JSON content type
  and a body of at most 1 KiB.
- **The rate tables** of `gateway.py` and the origin server track at most
  16,384 client addresses each; an IPv6 client counts by its /64.
- **The origin server** serves at most `--max-connections` connections at once
  (default 64; the next is closed unanswered), and each socket operation,
  the TLS handshake included, times out after `--timeout` seconds (default
  30). It holds at most 1 MiB of a body in memory per answer, and writes
  nothing to disk. The status relay and the asset catalog are recomputed at
  most every 10 seconds, so an anonymous request costs neither a request to the
  shard nor a walk of the data tree. The Worker's status relay is recomputed at
  most every 10 seconds in each of its instances.
- **The Worker** enforces the same limits except `memoryBytes`. It keeps at most
  16 MiB of frames waiting per connection (more closes it with 1008), and at
  most 4,096 unexpired spent tokens in each of 64 stores; when one is full, a
  handshake is refused with 503. A connection renews its slots every 30
  seconds, and a slot not renewed for 90 seconds is freed.

## The gateway's admission: what it protects against

The gateway relays bytes to the shard and does not read the game's protocols.
A player's account is checked by the shard itself, inside the client's
encrypted connections, as for a native client. The gateway's admission limits
what a browser page and a scripted client can make the gateway do:

- **It is not an open relay.** It opens only the hosts and the port of the
  shard's `server.ini` and the extra hosts and networks of the deployment,
  whatever the request asks; it connects to the allowlist's entry, never to the
  request's text. A connection whose first byte is not a Plasma connection type
  is closed before it reaches the shard.
- **Other sites' pages cannot use it.** A handshake must carry an `Origin` that
  is one of `gateway.origins` and a `Host` that is one of the gateway's own. The
  join endpoint issues tokens only to requests from those origins with
  `Sec-Fetch-Site: same-origin` (when sent) and a JSON content type, which a
  cross-site form cannot send.
- **A token admits one connection.** A token names the deployment, expires
  after `ttlSeconds`, and is spent by the first handshake that offers it. It
  travels as a WebSocket subprotocol (`plasma.join.<token>`), never in a URL,
  so it is not in any access log.
- **What it does not do.** The token is not authentication. A scripted client
  can send any `Origin`, fetch a token and connect, as a browser can. What bounds
  such a client is the per-address limits and the gateway's global caps, and
  what bounds what it can reach is the upstream allowlist.

Each handshake is checked in this order, every check before any connection to
the shard: the client's address, the connect rate, the route against the
allowlist, `Host`, `Origin`, the token, the open connections of the address, the
gateway's global cap.

## Join tokens and gateway processes

`gateway.py` remembers spent tokens in the memory of its process, until they
expire. After a restart, or across two gateway processes that share one key,
an unexpired token can be used once more in each process, until it expires
(`ttlSeconds`, at most 300 seconds). Run one `gateway.py` process per join key.

The Worker keeps spent tokens in Durable Objects that all its instances share,
so this does not apply to it.

## Logs: privacy and retention

What each component records:

- **The origin server's access log** (`--access-log`, required): one JSON line
  per request with its time, duration, method, path as requested (with its
  query), status, bytes sent and byte range, and why an answer ended early. It
  records no client address and no request header. The join endpoint's line
  never holds the token.
- **The gateway's log** (`--log`, optional): one JSON line per connection with
  its time, the client's address, the upstream host and port, the bytes each
  way, its duration and how it ended; and one per refused handshake with its
  status, reason, path (the upstream host and port it asked for), `Origin` and
  client address. Payloads, accounts, tokens and credentials are never written.
- **The Worker** writes the same two kinds of lines to its console, which goes
  wherever the Cloudflare account's logging for the Worker sends it.
- **Standard output** of both servers: the start-up lines above, the origin
  server's count of connections refused at its cap, and the path of an answer
  that ended because its file changed while it was sent.

Client addresses are personal data in many places. None of the tools rotates or
deletes its logs: set a retention period for the log files and the Worker's
logs, keep them only as long as you need them for operations and abuse
handling, and say so in your site's privacy notice.

The client's own logs stay in the player's browser, in the profile's storage.

## Fonts

The client's original faces, "Courier New" and "Trebuchet MS Bold", are not
free fonts: a deployment serves them only when its operator holds a licence for
them. A deployment that names no fonts serves free substitutes under those
names:

- Liberation Mono Regular 2.1.5 (SIL Open Font License 1.1) for "Courier New",
  with the same metrics;
- DejaVu Sans Bold 2.37 (Bitstream Vera and Arev licences) for "Trebuchet MS
  Bold", which has no metric-compatible free face.

Only the debug and loading text looks different.

The served file names stay the face names, since the client opens the faces by
name. The kit's `THIRD_PARTY_NOTICES.md` says which font each file holds. The
kit carries both licence texts next to the fonts, and the origin server serves
them at the same paths.

An operator who holds a licence for the original faces keeps them by naming them
in `fonts.root`, with `fonts.map` when the files have other names (for example
`cour.ttf` and `trebucbd.ttf`). The kit then carries those files, and its notices
say they are under the operator's own licence. With `fonts.url` the kit carries
no fonts, and the host at that URL serves the tree.

Whoever serves the kit's `fonts/` folder must serve it as a tree: the
`fonts/.fetch-manifest` dotfile, and the font files with a strong ETag and byte
ranges, as in What a host of the trees must serve.

## HD texture packs

The HD texture packs replace shipped textures with higher-resolution versions
of the same art (`HD texture pack format.md`). Their content is licensed under
CC BY-NC-SA 4.0:

- a commercial deployment must not serve them;
- the setting is off by default, the player turns it on, and while it is on
  the page credits the art;
- the packs never go into the repository, and the tools refuse a pack folder
  inside it.

The origin server serves `textures.hdPack.root` at `/hdpack/<Age>.hdpack`. The
static kit does not copy packs. With a kit, either name `textures.hdPack.url`,
an `https://` origin that answers the page's exact origin with CORS and exposes
`ETag` and `Content-Length`, or serve the root's files at `/hdpack/` next to the
page, as the origin server does. `HD texture pack format.md` describes how the
client fetches and keeps a pack.

## Source code and licence files

The client and its page are under the GNU General Public License version 3, with
the additional permissions in `COPYING.txt`. Whoever serves them conveys object
code, so every kit, and the origin server, carries `LICENSE.txt`, `COPYING.txt`,
`THIRD_PARTY_NOTICES.md` and `source.json`, which the page's "Source code
(GPLv3)" link reads.

`source.json` names `source.repository`, the SHA-256 of the third-party source
pins, and the client files' digests. Only a release build records the commit it
was built from; for such a build the tools refuse to serve or write a kit unless
the checkout is at that commit with nothing uncommitted, so that the page files
are that commit's too, and `source.tag` must name that commit; without a
release build, no tag is accepted. A deployment that names no repository gets a
`source.json` that says so: offer the source another way, as the licence
requires.

## Browser requirements

Before it loads the client, the page checks that the browser has:

- cross-origin isolation, which needs the isolation headers above and a secure
  page;
- JavaScript Promise Integration (`WebAssembly.Suspending` and
  `WebAssembly.promising`);
- WebGPU;
- canvas transfer to a worker (`OffscreenCanvas`).

A missing feature stops the page with the list of what is missing
(`BROWSER-MISSING`); a WebGPU device the client cannot open stops it with
`BROWSER-GPU`. Either way the page keeps its "Source code (GPLv3)" and
"Notices" links (`source.json` and `THIRD_PARTY_NOTICES.md` beside the page).
Textures in the BC formats upload directly where the device supports them and
are decompressed otherwise.

The profile and, in `patcher` deployments, the game files are kept in the
browser's origin private file system; the client keeps the profile in memory
where that is not available. A patcher deployment needs room for the shard's
files in the browser's storage; the client and its patcher report when the
storage cannot hold them.

## The page and the client

The page reads its deployment only from `/runtime/config.json` on its own origin,
and `server.ini` from the URL that document names, checked against the
document's SHA-256. A document or `server.ini` it refuses stops it with a code
(`CONFIG-*` or `INI-*`) on its status line, without the URL or the answer's text,
and with the "Source code (GPLv3)" and "Notices" links, as on every other ending.
The one query parameter the page reads is `renderscale`, the player's render
scale.

The page and the client meet in one object, `plasma`, which the page passes to
the client script's factory, `createPlasmaClient`. The client takes the
deployment's settings only from its `config` member, which the page builds from
the page document; the client's environment is made from that member alone.
`Architecture.md` ("The page and the module: `plasma`") lists its members and the
configuration's keys.
