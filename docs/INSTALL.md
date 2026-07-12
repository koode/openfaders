# Installing OpenFaders

## From the app (recommended)

The app installs the audio driver for you — no Terminal, no separate installer.

1. Put `OpenFaders.app` in `/Applications` and open it. It lives in the menu bar
   (no Dock icon).
2. On first launch it shows a short setup window. Click **Install audio driver**
   and authenticate with the standard macOS password prompt. OpenFaders copies its
   bundled driver into place and restarts the audio server for you. **This briefly
   interrupts all audio on the Mac** (about a second).
3. The first time it routes audio, macOS asks for microphone access. **Allow it** —
   OpenFaders captures the audio you send into its virtual devices to forward it to
   the board; it records no microphone.
4. You can reopen setup any time from the menu (**Set Up OpenFaders…**), and the
   Startup settings tab offers to install or update the driver if needed.

That's it. Then set your RODECaster to Multitrack USB mode, send an app to an
OpenFaders device, and bring the matching source up on a fader
(see [CHANNEL-MAP.md](CHANNEL-MAP.md)).

## From the installer

Prefer a traditional installer? The signed `.pkg` places the same components and
restarts the audio server:



1. Open `OpenFaders-<version>.pkg` and follow the prompts. It installs:
   - `OpenFaders.app` into `/Applications`
   - `OpenFadersDriver.driver` into `/Library/Audio/Plug-Ins/HAL`
2. The installer restarts the system audio server so the virtual devices appear
   immediately. **This briefly interrupts all audio on the Mac** (about a second).
   If for some reason the devices do not show up, log out and back in.
3. Launch OpenFaders from `/Applications`. It lives in the menu bar (no Dock icon).
4. The first time it routes audio, macOS asks for microphone access. **Allow it** —
   OpenFaders captures the audio you send into its virtual devices so it can forward
   it to the board; it records no microphone.
5. In System Settings ▸ Sound (or an app's own output menu) you will see
   **OpenFaders System / Game / Music / Virtual A / Virtual B**. Send an app to one,
   then bring the matching source up on a fader (see [CHANNEL-MAP.md](CHANNEL-MAP.md)).

Optionally turn on **Open at login** in Settings ▸ Startup.

## Uninstalling

- Quit OpenFaders and drag `/Applications/OpenFaders.app` to the Trash.
- Remove the driver and restart the audio server:

  ```sh
  sudo Scripts/uninstall-driver.sh
  ```

  (or delete `/Library/Audio/Plug-Ins/HAL/OpenFadersDriver.driver` and restart the
  Mac).

## Building the installer yourself

`Scripts/build-release.sh` builds the app and driver, signs both with a Developer
ID, and produces a signed `.pkg` in `build/dist/`:

```sh
OPENFADERS_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
OPENFADERS_INSTALLER_IDENTITY="Developer ID Installer: Your Name (TEAMID)" \
Scripts/build-release.sh
```

To notarize and staple in the same run, first store notary credentials once:

```sh
xcrun notarytool store-credentials openfaders-notary \
    --apple-id you@example.com --team-id TEAMID
```

then set `OPENFADERS_NOTARY_PROFILE=openfaders-notary` before running the script.

For local development without building a full installer, see
[BUILDING.md](BUILDING.md).
