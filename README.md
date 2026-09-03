# OpenFaders

OpenFaders creates virtual audio output devices on macOS and routes each one into
its own channel pair of a RODECaster Pro II, so every app can land on its own
fader. It is a clean-room, open-source alternative to the vendor's virtual-channels
software, written from scratch against Apple's public Core Audio APIs.

It is **not affiliated with or endorsed by RØDE** — see [NOTICE.md](NOTICE.md).

## Why

The RODECaster Pro II presents a ten-channel USB output on macOS, but macOS has no
built-in way to send a specific app to a specific output channel. OpenFaders fills
that gap with a small Core Audio driver that publishes stereo virtual output
devices, plus a routing engine that copies each device into the right channel pair
of the board's `Main Multitrack` output. A menu-bar app manages it all and launches
quietly at login.

It also aims to fix two problems people hit with the vendor software: audio slowly
degrading over long sessions (clock drift) and audio coming back mangled after a
restart or reconnect (sample rate and channel map not being re-pinned).

## How it works

```
apps ──▶ OpenFaders virtual output devices (driver) ──▶ routing engine ──▶ RODECaster Pro II
                                                       │              Main Multitrack (10 ch)
                                                       └── optional clock sync (locked to the board)
```

- **Driver** (`Sources/OpenFadersDriver`): a from-scratch Core Audio
  AudioServerPlugIn that publishes five stereo, 48 kHz loopback virtual output
  devices, each with a mirrored input the engine reads back.
- **Engine** (`Sources/OpenFadersEngine`): a reusable C library that captures each
  virtual device and writes its stereo audio into a channel pair of the board's
  ten-channel output, with optional drift correction that slaves the virtual clock
  to the board.
- **App** (`Sources/OpenFadersApp`): a SwiftUI menu-bar app for configuration and
  status. No Dock icon; runs in the background.
- **CLI** (`Sources/OpenFadersRoute`): `openfaders-route`, a headless front end over
  the same engine for testing.

## Channel map

Each virtual device feeds one RODECaster Main Multitrack return source:

| Device (macOS output)  | Channels | Board source |
| ---------------------- | -------- | ------------ |
| OpenFaders System      | 1–2      | System       |
| OpenFaders Game        | 3–4      | Game         |
| OpenFaders Music       | 5–6      | Music        |
| OpenFaders Virtual A   | 7–8      | Virtual A    |
| OpenFaders Virtual B   | 9–10     | Virtual B    |

Only **System** is monitored out of the box. To hear the others, assign the
matching source to a fader on the board (and set USB 1 Input to Expanded). See
[docs/CHANNEL-MAP.md](docs/CHANNEL-MAP.md).

## Requirements

- macOS 14 or later (built and tested on macOS 26, Apple Silicon; Intel supported).
- A RODECaster Pro II set to **Multitrack** USB mode on the device itself. No
  software can toggle this for you, and OpenFaders does not try to.

## Install

Download the latest [release](https://github.com/koode/openfaders/releases/latest),
open the `.dmg` and drag `OpenFaders.app` to `/Applications`. Open it — on first
launch it walks you through installing its audio driver with the standard macOS
password prompt, no Terminal required. A signed `.pkg` is in the same release if
you prefer a traditional installer that places the driver for you. Full steps,
including building from source, are in [docs/INSTALL.md](docs/INSTALL.md).

## Use

1. Launch OpenFaders. It sits in the menu bar; there is no Dock icon.
2. In macOS Sound settings (or an app's own output picker), send audio to an
   OpenFaders device, e.g. **OpenFaders Music**.
3. On the RODECaster, bring up the matching source on a fader.
4. That's it: the app's audio now rides its own fader on the board.

The menu bar shows routing state and an enable/disable switch. **Settings** covers
which devices are active, the hardware output, drift correction, and launch-at-login.

## Building from source

OpenFaders builds from the command line with
[xcodegen](https://github.com/yonaskolb/XcodeGen) and `xcodebuild`:

```sh
xcodegen generate
xcodebuild -project OpenFaders.xcodeproj -target OpenFadersApp -configuration Release build
```

The build signs ad-hoc so anyone can compile. macOS will **not load** an ad-hoc
signed HAL driver, though (coreaudiod's AMFI check requires a real developer
certificate chain), so local driver work uses `Scripts/reload-driver.sh` to
re-sign with a Developer ID before installing. See [docs/BUILDING.md](docs/BUILDING.md).

## Clock sync

Routing is **hardware-timed** by default: buffer headroom absorbs the small drift
between the Mac and the board. Turning on **adaptive drift correction** (Settings ▸
Audio, or `openfaders-route --adaptive`) runs a control loop that slaves each
virtual device's clock to the board so long sessions stay glitch-free, with no
resampling. It needs the current OpenFaders driver installed.

## License

MIT, see [LICENSE](LICENSE). Please read [NOTICE.md](NOTICE.md) for the
non-affiliation and trademark disclaimer and the clean-room provenance summary.
