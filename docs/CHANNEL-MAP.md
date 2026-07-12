# RODECaster Pro II channel map

In Multitrack USB mode the RODECaster Pro II exposes a Core Audio output device
named **RODECaster Pro II Main Multitrack** with ten output channels (audio the
Mac sends to the board). OpenFaders sends five stereo signals to five channel
pairs, each of which feeds a named return source on the board:

| Channels (1-based) | Board source | OpenFaders device    |
| ------------------ | ------------ | -------------------- |
| 1–2                | System       | OpenFaders System    |
| 3–4                | Game         | OpenFaders Game      |
| 5–6                | Music        | OpenFaders Music     |
| 7–8                | Virtual A    | OpenFaders Virtual A |
| 9–10               | Virtual B    | OpenFaders Virtual B |

`Chat` is not one of these ten channels. It is a separate two-channel Core Audio
device (`RODECaster Pro II Chat`), a comms/mix-minus bus, and OpenFaders does not
use it.

## Why a device might be silent

**System** is the only pair monitored by default. Sending audio to any of the other
four devices produces no sound until, on the board:

1. **USB 1 Input is set to Expanded.** In Standard/Stereo mode only the System pair
   (channels 1–2) carries audio; channels 3–10 are silent by design. Settings ▸
   Outputs ▸ Multitrack ▸ USB 1 Input ▸ Expanded.
2. **The matching source is assigned to a fader.** Game, Music, Virtual A, and
   Virtual B all start unassigned. Press the button above a spare fader (or tap the
   channel icon under a level meter, then the settings cog), pick the source, and
   save. The board has nine faders (six physical, three virtual) shared across all
   sources, so you may need to free one first.

Once expanded and assigned, feed the OpenFaders device from the Mac and raise the
fader; its meter should move and you should hear it.

## A note on the labels

That channels 1–2 are System is well established (it is the RODECaster's default
main return). The exact labels for the other four pairs come primarily from the
community reverse-engineering of the board's USB layout, and RØDE treats the four
virtual sources as interchangeable. If a device does not land where the table says,
the definitive check is an elimination test: take a fader that already carries an
audible pair, reassign it to the source you are chasing, and see which OpenFaders
device lights it up.
