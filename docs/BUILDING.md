# Building OpenFaders

## Prerequisites

- Xcode (with the macOS SDK).
- [xcodegen](https://github.com/yonaskolb/XcodeGen): `brew install xcodegen`.

The Xcode project is generated from `project.yml`; regenerate it after changing
that file:

```sh
xcodegen generate
```

## Targets

| Target             | Kind           | Notes                                              |
| ------------------ | -------------- | -------------------------------------------------- |
| `OpenFadersDriver` | `.driver` bundle | Core Audio AudioServerPlugIn, pure C             |
| `OpenFadersEngine` | static library | routing + drift-correction core, pure C            |
| `OpenFadersRoute`  | CLI tool       | `openfaders-route`, headless engine front end      |
| `OpenFadersApp`    | app            | SwiftUI menu-bar app (macOS 14+)                   |

Build any target with:

```sh
xcodebuild -project OpenFaders.xcodeproj -target <target> -configuration Release build
```

Products land in `build/Release/`.

## Loading the driver locally (important)

The build signs **ad-hoc**, so contributors without a certificate can still
compile. macOS will **not load an ad-hoc signed HAL driver**: coreaudiod's AMFI
check rejects it (error `-423`) and the device silently never appears. To load the
driver on your own machine it must be signed with a real Apple developer
certificate chain (a Developer ID Application, or an Apple Development cert).
Notarization is **not** required to load locally.

`Scripts/reload-driver.sh` does the whole dev loop: it re-signs the built driver
with a Developer ID, installs it to `/Library/Audio/Plug-Ins/HAL`, restarts
coreaudiod, and reports whether the devices loaded.

```sh
# defaults to the maintainer's identity; override for your own
OPENFADERS_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
sudo -E Scripts/reload-driver.sh
```

Restarting coreaudiod briefly interrupts all audio on the Mac. With SIP enabled,
`launchctl kickstart -k` on the audio service is refused; the scripts fall back to
signalling the exact PID launchd reports for the service (never a name/pattern
kill), and launchd respawns it.

Other helpers in `Scripts/`:

- `install-driver.sh` / `uninstall-driver.sh` — install or remove the driver.
- `reload-coreaudiod.sh` — just restart the audio server.

## Testing routing without the app

```sh
build/Release/openfaders-route            # hardware-timed
build/Release/openfaders-route --adaptive # with drift correction
```

Grant the microphone prompt on first run. It routes the five virtual devices to
the board and runs until Ctrl-C.

## Building the release installer

See [INSTALL.md](INSTALL.md#building-the-installer-yourself) for
`Scripts/build-release.sh`, which signs both components with a Developer ID, builds
the `.pkg`, and optionally notarizes and staples it.

`Scripts/build-dmg.sh` then wraps the signed app into a drag-to-Applications
`.dmg`. Both files land in `build/dist/` and both go on the GitHub release.
