# Running the browser client locally

This document runs a client you have built (`Building.md`) on one machine: the
origin server and the gateway of the deployment tools on loopback, a browser
on the same machine, and a shard the gateway can reach. It ends with the web
tests, which need no shard. `Deploying.md` describes each part in full, and
what a deployment for other players needs on top.

## What you need

- **A client build**: the build directory of `plClient`, or of `plClient-py2`
  (`Building.md`).
- **A shard, and an account on it.** The client logs in as the native client
  does. The gateway relays its connections to the hosts and the port in the
  shard's `server.ini`, the file the shard gives its native players. A shard of
  your own, such as [Dirtsand](https://github.com/H-uru/dirtsand), works. The
  shard's game scripts decide the flavour: H-uru's Python 3 scripts need
  `plClient`, the original Python 2.3 scripts of Myst Online: Uru Live need
  `plClient-py2` (`Python 2.3 flavour.md`).
- **The game files.** By default the client's original patcher fetches them
  from the shard's file server, as the native launcher does. A copy of the
  game files on your disk works too (below).
- **Python 3.13**, with one virtual environment (next section).
- **A browser** with WebGPU, JavaScript Promise Integration, cross-origin
  isolation and canvas transfer to a worker (`Deploying.md`, "Browser
  requirements"). `Status.md` lists the browsers that were tried. A page from
  `http://127.0.0.1` counts as a secure page, so loopback needs no certificate.

## One Python environment

One virtual environment runs both the servers and the tests:

```sh
python3.13 -m venv <venv>
<venv>/bin/python -m pip install -r web/tests/requirements.txt
```

`web/tests/requirements.txt` pins the tests' packages (Playwright and its
dependencies) and includes `web/deploy/requirements.txt`, which pins the
gateway's one dependency, `websockets`. Keep `<venv>` outside the checkout: a
release build refuses a checkout with untracked files. On Debian and Ubuntu,
`venv` needs the `python3-venv` package (or `python3.13-venv`).

The deployment tools run from the checkout, with `web/deploy` on the import
path. Run the commands below from the root of the checkout, in a shell where

```sh
export PYTHONPATH=web/deploy
```

is set. (`pip install -e web/deploy` installs the same tools as commands,
`plasma-web-server` and the others, as `Deploying.md` describes.)

## A deployment for one machine

Make a folder outside the checkout, `<local>` below, and copy the shard's
`server.ini` into it. Then write `<local>/local.json`:

```json
{
 "schema": 1,
 "name": "local",
 "serverIni": "server.ini",
 "data": {"files": "patcher", "gameCode": "server"},
 "gateway": {
  "url": "ws://127.0.0.1:8766",
  "origins": ["http://127.0.0.1:8613"],
  "join": {"secretFile": "join.key"}
 },
 "source": {"repository": "https://github.com/oscargala/openages"}
}
```

- Relative paths are read from the file's own folder.
- `data.files` `patcher` lets the client's patcher fill the browser's storage
  from the shard's file server. To read a copy of the game files on your disk
  instead, write
  `"data": {"files": "http", "root": "<game files>", "gameCode": "server"}`,
  where `<game files>` is the folder of a native installation, the one that
  holds `dat/` and `sfx/`.
- `data.gameCode` is `server` for `plClient`. For `plClient-py2` it is
  `server-py2`, which needs `patcher`.
- `gateway.url` and `gateway.origins` are the gateway's and the page's
  addresses: the gateway's default port, 8766, and the origin server's, 8613.
- `gateway.join` names the key with which the origin server signs the
  gateway's admission tokens. It must be outside the checkout.
- `source.repository` names where the source of the client you serve is
  published: this repository, or your own fork if you changed the code. The
  origin server, and the check below, refuse a deployment without it
  (`Deploying.md`, "Source code and licence files").

Create the key, then print what the tools will use, the shard's hosts, the
fonts and the limits among it:

```sh
<venv>/bin/python -m plasma_web.deployment --init-join-secret <local>/local.json
<venv>/bin/python -m plasma_web.deployment <local>/local.json
```

The key file is written with mode 0600 and is never printed. A file the tools
refuse prints the reason instead of the summary. `Deploying.md`, "The
deployment file", lists every key.

## Starting the servers

Start the origin server and the gateway, each in its own terminal, both from
the root of the checkout with `PYTHONPATH` set:

```sh
<venv>/bin/python -m plasma_web.server --deployment <local>/local.json \
    --build-dir <build dir>/Sources/Plasma/Apps/plClient \
    --access-log <local>/access.log
```

```sh
<venv>/bin/python -m plasma_web.gateway --deployment <local>/local.json \
    --log <local>/gateway.log
```

`--build-dir` is the folder that holds `plClient.js` and `plClient.wasm` (or
the py2 files). The origin server prints the shard's entry host, the
`server.ini` SHA-256, the fonts it serves and the address it serves on,
`http://127.0.0.1:8613/`. The gateway prints one line with its policy: the
shard's hosts it may open, the page origins it admits, and its limits.

Open `http://127.0.0.1:8613/` in the browser, at exactly the origin that
`gateway.origins` lists: the join endpoint and the gateway compare the page's
origin with that list, so `http://localhost:8613/` is refused unless you list
it too. The page checks the browser and loads the client. In a `patcher`
deployment the launcher dialog then brings the game files up to date with
the shard's; the first start fetches all of them into the browser's storage,
and a later start fetches only what changed. Then log in with your account
on the shard.

Stop both servers with Ctrl-C. The access log records each request, the
gateway's log each connection (`Deploying.md`, "Logs: privacy and
retention"). The client's own logs, its settings and the game files the
patcher fetched stay in the browser's storage for `http://127.0.0.1:8613`;
clearing that site's data in the browser removes them.

If the shard's `Server.Status` is an `https` URL and your Python comes from
python.org, give the origin server a certificate bundle for the status text
of the login dialog, for example `--ca-file /etc/ssl/cert.pem` on macOS
(`Deploying.md`, "The origin server").

## When it does not start

| What you see | Where to look |
|---|---|
| The page lists features the browser lacks (`BROWSER-MISSING`), or reports that it could not open a WebGPU device (`BROWSER-GPU`) | `Deploying.md`, "Browser requirements" |
| A `CONFIG-` or `INI-` code on the page's status line | the page refused the page document or the `server.ini`: `Deploying.md`, "The page and the client" |
| The client cannot connect | the gateway's log: a refused handshake has its reason. A refused page origin means the browser's address is not in `gateway.origins`; a 502 means the gateway could not reach the shard |
| A server does not start | its message, and the summary of `python -m plasma_web.deployment <file>` |

## The tests

The web tests check the deployment tools, the gateway, the page and the WGSL
generator. None needs a shard, game files or an account:

```sh
<venv>/bin/python -m playwright install chromium
<venv>/bin/python web/tests/run.py --build-dir <build dir>
```

The first command installs the Chromium that the page's tests drive, into
Playwright's own cache folder (`PLAYWRIGHT_BROWSERS_PATH` chooses another).
On Debian and Ubuntu, `playwright install --with-deps chromium` also
installs the system libraries Chromium needs, with `sudo apt-get`.
`run.py` runs each suite in its own interpreter and prints one PASS or FAIL
line per suite, and exits with 1 when one fails. `--build-dir` is the build
directory of `build-client.sh`; three page checks read the built client, and
are skipped without it. `run.py --help` lists the suites; name some to run
only those, and add `--out <folder>` to keep their output.

A suite skips the checks whose prerequisite is missing, and says which:

- `node` on `PATH`, for the checks that run the page's modules. Sourcing
  `<emsdk>/emsdk_env.sh` puts the emsdk's Node on `PATH`;
- `openssl` on `PATH`, for the TLS checks' fixture certificates;
- Playwright's Chromium, for the page's checks. `run.py` starts it once
  before the suites, and names it when it does not start;
- `wrangler`, for the gateway Worker's half of the gateway contract
  (`PLASMA_WRANGLER`, or `wrangler` on `PATH`). The release the tests use is
  `WRANGLER_VERSION` in `.github/workflows/web-build.yml`, 4.149.0:
  `npm install --prefix <wrangler> wrangler@4.149.0`, then
  `PLASMA_WRANGLER=<wrangler>/node_modules/.bin/wrangler`. Without it the
  contract runs against `gateway.py` alone.

`run.py` prints what is missing before the suites run. `--require all` (or a
comma list of `node`, `openssl`, `playwright`, `chromium`, `wrangler` and
`build`) turns a missing prerequisite into an error instead: `run.py` stops
with status 2 before any suite runs. The CI runs the tests that way, so a run
cannot pass by skipping.

With every prerequisite present, the whole run took four to five minutes on
the Mac that `Building.md` timed, most of it in the gateway contract. The
suites write nothing into the checkout.
