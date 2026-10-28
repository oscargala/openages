# HD texture pack format

The browser client can replace an Age's shipped DXT textures with higher-resolution
versions of the same art. The replacements come in one file per Age, the HD
texture pack (`<Age>.hdpack`). A pack is optional: it is used only when the
deployment lists the Age and the player has turned the setting on, and every
texture the pack cannot prove to belong to is drawn from the shipped data as
usual.

This document describes the file exactly as the client reads it
(`plWGPUTexturePack.cpp` in `pfWGPUPipeline`), how its entries are matched to the
shipped textures, and how the client fetches, checks and keeps a pack. The tool
that builds packs is not part of this repository; the last section says what it
reads and writes, so that another builder can produce the same files.

## Licence of the content

The packs hold art derived from Cyan's original texture sources, which are
licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0
licence (CC BY-NC-SA 4.0). A pack names its licence in its own header text. The
page credits the art while the setting is on, and a deployment that serves
packs must be non-commercial. Packs never go into the repository: the
deployment tools refuse a pack directory inside the source tree.

## File layout

All integers are little-endian. Offsets are in bytes from the start of the file.

### Header

| Offset | Size | Field | Reader's check |
|---|---|---|---|
| 0 | 8 | magic, the ASCII bytes `PLHDPK01` | must match |
| 8 | 4 | version (u32) | must be 1 |
| 12 | 4 | entry count N (u32) | |
| 16 | 4 | text length T (u32) | the text and the hash below must fit in the file |
| 20 | 4 | entry size (u32) | must be 160 |
| 24 | T | text: the pack's licence and attribution, UTF-8 | not interpreted |
| 24 + T | 32 | SHA-256 of the data blob | must equal the hash of the blob |

The entry table starts at `E = 24 + T + 32` and holds N entries of 160 bytes.
The data blob starts at `B = E + 160 * N` and runs to the end of the file; `B`
must not be past the end.

### Entry (160 bytes)

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | location sequence (u32) | the shipped texture's key: `plLocation` sequence number |
| 4 | 2 | location flags (u16) | `plLocation` flags (compared without `kItinerant`) |
| 6 | 2 | class type (u16) | the key's class index; must be the key's class (plMipmap) |
| 8 | 4 | object id (u32) | the key's object id |
| 12 | 1 | compression | shipped `plBitmap` compression; must be 1 (`kDirectXCompression`) |
| 13 | 1 | DXT type | shipped DXT type; must be 1 (DXT1, BC1) or 5 (DXT5, BC3) |
| 14 | 1 | pixel size | shipped `plBitmap` pixel size |
| 15 | 1 | rule | how the builder made the entry (1, 2 or 3, below); the client only logs it |
| 16 | 2 | flags (u16) | shipped `plBitmap` flags |
| 18 | 2 | padding | |
| 20 | 4 | width (u32) | shipped level-0 width |
| 24 | 4 | height (u32) | shipped level-0 height |
| 28 | 1 | level count | shipped `plMipmap` level count |
| 29 | 3 | padding | |
| 32 | 4 | total size (u32) | shipped image size in bytes, all levels |
| 36 | 32 | SHA-256, all levels | of the shipped image data, every level |
| 68 | 32 | SHA-256, levels 1.. | of the shipped image data without level 0 |
| 100 | 32 | SHA-256, level 0 | of the shipped level 0 |
| 132 | 1 | g | the number of HD levels above the shipped level 0 (log2 of the gain per side); at most 12 |
| 133 | 1 | tail | 0: the pack holds only the new top levels; 1: the pack holds the whole chain |
| 134 | 2 | padding | |
| 136 | 4 | HD width (u32) | width of the pack's level 0 |
| 140 | 4 | HD height (u32) | height of the pack's level 0 |
| 144 | 1 | pack level count | the number of levels stored in the blob for this entry |
| 145 | 3 | padding | |
| 148 | 8 | data offset (u64) | from the start of the data blob |
| 156 | 4 | data size (u32) | bytes of blob that belong to this entry |

The entry's bytes `[B + offset, B + offset + size)` must lie inside the file.

### Levels in the blob

An entry's levels follow one another from its data offset, each in BC block
layout: rows of 4x4 blocks, 8 bytes per block for DXT1 and 16 for DXT5. A level
of `w` by `h` texels takes `max(1, ceil(w/4)) * max(1, ceil(h/4))` blocks.

- **tail 0.** The pack level count must be `g + 1`. Levels `0..g-1` are the HD
  levels, level `k` of size `max(1, HDwidth >> k)` by `max(1, HDheight >> k)`.
  Level `g` is a verbatim copy of the shipped level 0 (width by height), and its
  SHA-256 must equal the entry's level-0 hash. The levels below the shipped
  level 0 are not in the pack: the client uploads them from the shipped data.
- **tail 1.** Every level is the pack's own: level `k` is
  `max(1, HDwidth >> k)` by `max(1, HDheight >> k)`, for `k` from 0 to the pack
  level count minus one.

The levels must fit in the entry's data size. The client checks this when it
reads the entry table.

## Versioning

The magic ends in `01`, the version field is 1, and the entry size is 160. The
client accepts exactly these values: a file with another magic, version or
entry size is refused as a whole ("bad magic", "bad header"), and the Age is
drawn with its shipped textures. A change to the layout therefore needs a new
version; clients that do not know it fall back to the shipped textures.

The client keeps verified packs in the browser's Cache Storage under the cache
name `plasma-hdpack-v1`.

## How a pack is checked when it loads

The client checks every byte of a pack before it uses any of it, in this order,
as the bytes arrive:

1. the magic and the header (the first 24 bytes);
2. the entry table: each entry's DXT type, compression, `g`, the tail rule's
   level count, and its data range and level sizes;
3. the SHA-256 of the data blob against the header's hash;
4. for each tail-0 entry, the SHA-256 of its copy of the shipped level 0
   against the entry's level-0 hash;
5. a malformed entry found in step 2 is reported after steps 3 and 4.

Any failure refuses the whole pack for this visit to the Age.

## How a pack is matched to the shipped textures

A pack entry is a candidate for a texture when all of these hold:

- the texture is a `plMipmap` of exactly that class (not a subclass such as
  `plDynamicTextMap`) and has a key;
- the key's location sequence, location flags (without `kItinerant`, as
  `plLocation::operator==` compares them) and object id equal the entry's, and
  its class type equals the entry's class type. Names are never compared;
- the texture does not have `kAutoGenMipmap`;
- the device uploads it as BC1 or BC3 (a device without BC texture support uses
  the shipped textures for every key and logs this once).

The client then checks the loaded data against the entry on every upload
(`plWGPUDevice::IUploadPackTexture`). The game's TextureQuality setting can make
`plMipmap::Read` drop the shipped level 0 (at most one level). The loaded
texture is therefore either the shipped one (c = 0) or the shipped one without
its level 0 (c = 1):

- c is 0 when the loaded width and height equal the entry's, 1 when they are
  half of them, and anything else refuses the entry;
- the loaded level count plus c equals the entry's level count, and the loaded
  flags, compression, DXT type and pixel size equal the entry's;
- the SHA-256 of the loaded image data equals the entry's all-levels hash
  (c = 0) or its levels-1.. hash (c = 1);
- the HD size after the chop, `max(HDwidth >> c, HDheight >> c)`, is within the
  device's `maxTextureDimension2D`;
- for tail 1, the pack holds at least `c + GPU levels` levels (below).

A refused entry is logged once and the shipped texture is used; it is not
offered again until the pack is loaded again. A match is never cached: the
check runs on every upload.

### What the GPU receives

Let `A` be the highest level index the D3D9 pipeline would upload for the loaded
texture (`plWGPUDevice::IAllowedLevels`: compressed levels whose width or height
is not a multiple of four are dropped). The GPU texture has `g + A + 1` levels
and a top level of `HDwidth >> c` by `HDheight >> c`.

- **tail 0**: pack levels `c..g-1`, then, when c = 1, the pack's copy of the
  shipped level 0, then the loaded shipped levels `0..A` through the shipped
  upload path.
- **tail 1**: pack levels `c` to `c + g + A`.

For tail-0 entries the shipped levels keep their shipped bytes; the new levels
are only those above the shipped level 0. The texture reference
keeps the shipped D3D9 level count (`A + 1`) for everything else that reads it.
The CPU-side `plMipmap` is never changed.

## When the client fetches and uses a pack

- **Configuration.** The deployment lists the Ages with packs and where they
  are: `textures.hdPack` with `root` (a directory the origin server serves at
  `/hdpack`) or `url` (an https directory on another origin), and `ages`
  (1 to 64 Age names). The client receives the base URL and the Age list
  (`PLASMA_TEXTURES_HDPACK_URL`, `PLASMA_TEXTURES_HDPACK_AGES`) and fetches
  `<base>/<Age>.hdpack`. Age names compare without regard to case.
- **The setting.** The player's choice is the profile file
  `Init/webtextures.txt`, a line `HDTextures 1` (on) or `HDTextures 0`. The page
  writes it from a checkbox, which it offers only when the deployment has
  packs. No file means off.
- **Fetch.** When the client joins a listed Age (`plAgeLoader::UpdateAge`), it
  starts the pack's fetch on the browser's main thread, streaming into one
  allocation of the response's length. The client thread checks the bytes as
  they arrive, a few milliseconds per frame (`plClient::IUpdate`).
- **Decision.** Before the Age's first room pages in (`plClient::ILoadNextRoom`),
  the client decides once for the whole visit: the pack if it is verified by
  then, after waiting at most two seconds for missing bytes, otherwise the
  shipped textures. A visit never switches in the middle. Turning the setting
  on or off takes effect at the next Age.
- **Origins.** The pack must come from the page's own origin or from one of the
  deployment's pinned asset origins, over https (or http on a loopback host for
  a local page), without credentials, query or fragment.
- **Response.** A 200 with a `Content-Length` of at most 1 GiB and no content
  encoding other than identity. The client asks the browser not to store it in
  the HTTP cache.
- **Keeping it.** A pack verified from the network is kept in Cache Storage
  when its response had a strong ETag, under its URL with its data SHA-256. On a
  later visit the client asks the server whether the kept pack is current (a
  request conditional on that ETag). On 304 it reads the kept copy and checks
  every byte again, as for the network. A kept pack that fails a check is
  evicted and fetched once more from the network. The kept packs share a cap of
  256 MiB; the least recently used are evicted first, and a larger pack is not
  kept.

One Age's pack is held in memory at a time. Its texture lookups during uploads
read memory only.

## Building a pack

The builder is not part of this repository. A builder that writes the layout
above, with correct hashes, produces packs the client accepts. This section
describes what the existing builder reads and decides.

### Inputs

- **The shipped textures.** For every `plMipmap` key of the Age: its key
  (location sequence and flags, class type, object id), its `plBitmap` header
  (compression, DXT type, pixel size, flags, width, height, level count, total
  size), its level sizes and raw level bytes. The entry's identity, header and
  hashes are taken from these bytes.
- **The layers that use each texture.** For every `plLayer` that uses the key,
  its `hsGMatState` clamp flags (`kClampTextureU`, `kClampTextureV`) and its
  blend flags. This is the blend-flag export.
- **A survey** that pairs each shipped texture with its source image and
  classifies the pair.
- **The source images**, under their own licence (above).

### What the builder decides

- **Rule 1**: the source matches the shipped texture and is at least twice its
  size per side. The pack holds new HD levels made from the source with the
  exporter's own mip filter, then the shipped level 0 (tail 0).
- **Rule 2**: the source's colour matches but its alpha differs. The HD levels
  take the source's colour and the shipped alpha, upsampled (tail 0).
- **Rule 3**: the survey marks the shipped texture as one whose look comes
  from DXT compression noise rather than from the art. The whole chain is
  re-encoded from the source (tail 1).
- Among the keys left out: those that are not DXT1 or DXT5, those with
  `kAutoGenMipmap`, those with `kForceOneMipLevel` (0x0020) and more than one
  level, DXT1 keys that carry `kAlphaChannelFlag`, and DXT1 keys with
  transparent texels in their shipped blocks. Every emitted BC1 block is a
  four-colour block, so it decodes opaque.
- Intensity maps (`kIntensityMap`, 0x0080) are treated like any other DXT key.
  A pack is used only when the device samples DXT data directly, and on that
  path neither D3D9 nor the browser client reads the flag: both choose the
  texture format from the compression type alone.

### The clamp and blend flags

The clamp flags of the layers decide whether the builder upsamples a texture's
alpha with wrap or clamp at each edge (all layers clamp: clamp; any layer
wraps: wrap). Whether an edge keeps one constant colour, as the exporter's
`EnsureKonstantBorder` makes it, is decided from the shipped levels themselves;
the clamp flags are only compared with that choice, and disagreements are
reported.

The blend flags are a cross-check for DXT1 keys only, recorded in the builder's
report and never used to include or exclude a key. For each DXT1 key the report
says whether any layer that uses it has `kBlendTest` (0x1), `kBlendAlpha` (0x2)
or `kBlendAlphaTestHigh` (0x4000000), the states in which the texture's alpha
reaches blending or the alpha test (`hsGMatStateEnums.h`). It also counts the
shipped blocks whose endpoints are in three-colour order. A key that is high on
both would suggest that its shipped alpha matters although the pack draws it
opaque. Neither the clamp flags nor the blend flags are stored in the pack.
