# Notice and disclaimer

OpenFaders is an independent, open-source project. It is not affiliated with,
endorsed by, or sponsored by RODE Microphones / Freedman Electronics.

"RODE" and "RODECaster" are trademarks of their respective owner. This project
refers to the RODECaster Pro II only to describe hardware compatibility
(nominative use). OpenFaders ships no RODE trademarks or logos as part of its own
identity, and its virtual audio devices use their own names.

OpenFaders was written from scratch against Apple's public Core Audio APIs and
publicly available documentation. It does not include, copy, or derive from any
RODE software. The only information taken from observing RODE hardware and the
operating system is factual interoperability data (USB channel counts, the
48 kHz sample rate, and the fact that the relevant macOS driver type is a Core
Audio AudioServerPlugIn). See `docs/` and the companion clean-room notes for the
provenance of every component.

OpenFaders installs a Core Audio driver into a system location and routes audio to
your hardware. Use it at your own risk. Reloading the system audio server
briefly interrupts all audio on the machine.

## Precondition

Your RODECaster Pro II must be set to Multitrack USB mode on the device itself.
No software can toggle this for you, and OpenFaders does not attempt to.
