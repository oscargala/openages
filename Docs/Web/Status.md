# Status of the browser client

This document lists what has been measured up to v0.1. Everything below was
measured on one Apple-silicon Mac running macOS. The shards were test shards
running [Dirtsand](https://github.com/H-uru/dirtsand).

## Browsers

| Browser | Tested | Result |
|---|---|---|
| Chrome: Chromium 156 (Chrome for Testing) | every check below | works |
| Firefox 157 | once, in October 2026; not in the regular checks | start-up, an online session in Relto and Eder Gira, sound and input work |
| Safari 27 | once, in October 2026; not in the regular checks | start-up, an online session in Relto and Eder Gira, sound and input work |

Before it loads the client, the page checks that the browser has cross-origin
isolation, JavaScript Promise Integration, WebGPU and canvas transfer to a
worker, and lists what is missing
([Deploying.md, "Browser requirements"](Deploying.md#browser-requirements)).
Windows, Linux, phones and tablets have not been tested.

## Ages and journeys

These were played in Chrome in October 2026, online, on test builds of the
client driven by scripted mouse and keyboard input. Each milestone was read
back from the shard after a fresh login. Where a step could not be done, its
cause was traced in the game's scripts and the shard's state, and the step was
repeated in the native client where that was needed to tell a fault of the
port from the game's or the shard's state.

Before the release, a scripted part of this was played again on the release's
own source, in Chrome on the same test shard: login and the player list,
Relto, the link to Eder Gira, a second login that finds the first session's
state, the intro movies and the game's Quit. The last column says what else
was played again.

| Age or feature | Played in the browser (Python 3 client) | Played again before the release |
|---|---|---|
| A new explorer | to the end: the avatar, the Cleft, the Relto book, Relto | Relto |
| Gahreesen | to the end: the seven cloths, the Bahro cave and the pole | no |
| Teledahn | to the end | no |
| Kadish Tolesa | to the end | no |
| Eder Gira and Eder Kemo | to the end | the link to Eder Gira; both Eder Gira views of the rendering table |
| Ahnonay | to the end; the fourth sphere was played with two players | the link into Ahnonay |
| Minkata and Jalak Dador | to the end | the link into Minkata |
| Phil's Relto | reached through the Spyroom: its journals, blinds, vest and calendar star | no |
| The Path of the Shell | the four poles, the fissure, the Cleft's final vision, K'veer and the Myst library | no |
| Er'cana | the seven cloths and the Bahro cave; the pellet ovens fail with the test shard's game files, in the native client too | its view in the rendering table |
| The Neighborhood | its books, doors, imager and journals; the common room and some items depend on hood state or a KI level the test player did not have | no |
| The Great Zero, the City and the Nexus | as far as the test shard's state lets any client go: the KI marker machine's first upload, 6 of the 15 markers of the first game, the ferry district, the Nexus machine | the Great Zero's view in the rendering table |
| The four Pods | linking, power, lights and Tetsonot's Bahro symbol; the other symbols appear on a schedule and were not played | no |
| The KI | journal notes, pictures and chat; a note kept after the client is closed in the middle of an Age | no |
| Two players | presence, movement, chat, a shared clickable and a Relto visit, with a native client and with a second browser | no |
| Graphics settings | changed in the game's own menu and kept across a reload | no |

The Python 2.3 client was played on a test shard that serves MOULa's own game
code: login, a new explorer, Relto, the Neighborhood, links to Teledahn and
Eder Gira, the KI, and the minigames (Heek with two players, the KI marker
quest, Blue Spiral and Ahnonay's Quabs). The minigame runs logged no Python
tracebacks. Before the release, login, Relto and the link to Eder Gira were
played again there, with no Python tracebacks.

## Rendering compared with the native client

At six fixed camera poses, the browser's frame is compared pixel by pixel with
the native client's (H-uru Plasma on macOS) in the same game state. The frames
are not identical yet. The share of pixels that differ by more than 16 of 255:

| Pose | Pixels that differ by more than 16 |
|---|---|
| Er'cana | 0.6 % |
| Eder Gira, two views | 2.6 % and 3.3 % |
| The Great Zero | 9.8 % |
| Eder Tsogal | 17.9 % |
| Relto | 31.5 % |

Measured again before the release, every pose gave the same frame, pixel for
pixel. The Ages are handcrafted, so any visible difference from the native
client is a bug to fix.

## Frame rate and memory

On the measured route (Relto and Eder Gira, an 800 by 600 view, a 60 Hz
display), the client draws a median of 60 frames per second in every phase and
at least 58.9 in every one-second window inside an Age. The longest frame
inside an Age took 35 ms; while a link loads, one frame can take up to 0.63 s.

Over eight minutes with three links, the client's WebAssembly memory stays at
491 MiB, and the browser's processes for the page peak at about 2.3 GiB
(2.8 GiB in a private window). These numbers were measured again before the
release.

## Not working, or not known yet

- Firefox and Safari are not part of the regular checks. In Firefox, a
  screenshot of the page's canvas sometimes comes back empty while the client
  keeps drawing; whether the window itself stops updating has not been checked.
- Mouse capture (pointer lock) has not been verified in any browser.
- The rendering differences above.
- The parts of the Ages above that the test shards' state closed are untested,
  and so is the Descent: the way to its book needs shard state that the test
  shard does not have.
- On a shard whose game scripts predate H-uru's fix to `Cleft.py`, the Python 3
  client does not load Tomahna after the Cleft's final vision, as H-uru's
  native client does not. The Python 2.3 client skips the missing pages, as
  Cyan's client does.
- The builder of HD texture packs is not part of this release. The pack format
  is documented in [`Docs/Web/HD texture pack format.md`](HD%20texture%20pack%20format.md).
