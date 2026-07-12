# OpenFaders

OpenFaders is a native macOS app that creates virtual audio devices and routes them
into a RODECaster Pro II, so you can send each application to its own fader. It
is a clean-room, open-source alternative to the vendor's virtual-channels
software, written from scratch against Apple's public Core Audio APIs.

Status: early development. See `docs/ARCHITECTURE.md` for the design and the
build plan.

## Why

The RODECaster Pro II presents a multichannel USB output on macOS, but macOS has
no built-in way to send a specific app to a specific output channel. OpenFaders fills
that gap with a small Core Audio driver that publishes stereo virtual output
devices, plus a routing engine that copies each device into the right channel
pair of the board's `Main Multitrack` output. A menu-bar app manages it all and
launches quietly at login.

OpenFaders also aims to fix two problems the vendor software has: audio slowly
degrading during long sessions (clock drift) and audio coming back mangled after
a restart or reconnect (channel map and sample rate not being re-pinned).

## How it works

```
apps ──▶ OpenFaders virtual output devices (driver) ──▶ routing engine ──▶ RODECaster Pro II
                                                        │              Main Multitrack (10 ch)
                                                        └── clock sync (locked to the board)
```

- **Driver** (`Sources/OpenFadersDriver`): a from-scratch Core Audio AudioServerPlugIn
  that publishes N stereo, 48 kHz loopback virtual output devices.
- **Routing engine** (`Sources/OpenFadersKit`): reads each virtual device and writes
  its stereo audio into a channel pair of the RODECaster's 10-channel output,
  with clock synchronization so nothing drifts.
- **App** (`Sources/OpenFadersApp`): a SwiftUI menu-bar app for configuration and
  status. No Dock icon; runs in the background.

## Requirements

- macOS (built and tested on macOS 26, Apple Silicon).
- A RODECaster Pro II set to **Multitrack** USB mode on the device.

## Building

OpenFaders builds from the command line with [xcodegen](https://github.com/yonaskolb/XcodeGen)
and `xcodebuild`. See `docs/BUILDING.md` (added alongside the build config).

## License

MIT, see [LICENSE](LICENSE). Please read [NOTICE.md](NOTICE.md) for the
non-affiliation and trademark disclaimer and the clean-room provenance summary.
