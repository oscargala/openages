# Free substitutes for the client's two text faces

The client draws its debug and loading text with two faces, "Courier New" and "Trebuchet MS Bold"
(`plDebugText.h`, `plNetClientMgrShow.cpp`). In a browser, `plTextFont` opens each one by name as
`/fonts/<face>.ttf` (`plTextFont.cpp`, `IInitFontTexture`). Both originals are Microsoft fonts, which
must not be redistributed. A deployment that names no fonts, and every static kit built from one,
serves these two free fonts under the faces' names instead
(`web/deploy/plasma_web/deployment.py`, `FREE_FONTS`):

| Served as | File here | Font | Release |
|---|---|---|---|
| `Courier New.ttf` | `LiberationMono-Regular.ttf` | Liberation Mono Regular, Version 2.1.5 | Liberation Fonts 2.1.5 |
| `Trebuchet MS Bold.ttf` | `DejaVuSans-Bold.ttf` | DejaVu Sans Bold, Version 2.37 | DejaVu Fonts 2.37 |

Liberation Mono has the same metrics as Courier New. Trebuchet MS has no metric-compatible free
face; DejaVu Sans Bold is the sans-serif bold that fontconfig picks for it on a common Linux host.
Only the debug and loading text looks different from the originals.

An operator who holds a licence for the originals can serve them instead: `fonts.root` in the
deployment file names a directory, and `fonts.map` names the file for each face in it
(`Docs/Web/Deploying.md`, "Fonts").

## Provenance

Both files are copied unmodified out of the projects' official TTF release archives.

| Archive | URL | Bytes | SHA-256 |
|---|---|---|---|
| `liberation-fonts-ttf-2.1.5.tar.gz` | https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz (linked from https://github.com/liberationfonts/liberation-fonts/releases/tag/2.1.5) | 2,385,008 | `7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0` |
| `dejavu-fonts-ttf-2.37.tar.bz2` | https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2 | 5,429,777 | `fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7` |

| File here | Path in the archive | Bytes | SHA-256 |
|---|---|---|---|
| `LiberationMono-Regular.ttf` | `liberation-fonts-ttf-2.1.5/LiberationMono-Regular.ttf` | 319,508 | `f2b83c763e8afd21709333370bed4774337fae82267937e2b5aea7e2fbd922c1` |
| `LICENSE-LiberationFonts.txt` | `liberation-fonts-ttf-2.1.5/LICENSE` | 4,414 | `93fed46019c38bbe566b479d22148e2e8a1e85ada614accb0211c37b2c61c19b` |
| `DejaVuSans-Bold.ttf` | `dejavu-fonts-ttf-2.37/ttf/DejaVuSans-Bold.ttf` | 705,684 | `e6476c1b80502924294eed40894c5b18e06c181444ca953e5334262df9c27724` |
| `LICENSE-DejaVuFonts.txt` | `dejavu-fonts-ttf-2.37/LICENSE` | 8,816 | `7a083b136e64d064794c3419751e5c7dd10d2f64c108fe5ba161eae5e5958a93` |

## Licences

- **Liberation Mono:** SIL Open Font License, Version 1.1, with the Reserved Font Name "Liberation".
  Digitized data copyright (c) 2010 Google Corporation; copyright (c) 2012 Red Hat, Inc. Full text:
  `LICENSE-LiberationFonts.txt`.
- **DejaVu Sans Bold:** the Bitstream Vera Fonts licence and the Arev Fonts licence; the DejaVu
  changes are in the public domain. Copyright (c) 2003 by Bitstream, Inc.; copyright (c) 2006 by
  Tavmjong Bah. Full text: `LICENSE-DejaVuFonts.txt`.

Both licences allow the unmodified fonts to be redistributed with other software, as long as the
licence text goes with them. A static kit therefore copies both licence files next to the fonts
and names them in its `THIRD_PARTY_NOTICES.md` (`web/deploy/plasma_web/emit_static.py`).
