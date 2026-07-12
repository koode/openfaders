// OpenFaders routing engine (Phase 1).
//
// Captures the mirrored input of each OpenFaders virtual device and plays it
// into a stereo channel pair of a physical multichannel output device (the
// RODECaster Pro II "Main Multitrack" 10-channel USB output). This is the
// component that proves audio flows end to end: an app plays into a virtual
// device, the engine forwards it to the board.
//
// Phase 1 has no clock synchronisation. It carries enough ring headroom to run
// cleanly for the length of a test; Phase 2 adds drift correction.

#ifndef OPENFADERS_ROUTING_ENGINE_H
#define OPENFADERS_ROUTING_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct RoutingEngine RoutingEngine;

// Describes one virtual-device-to-hardware-channel route.
typedef struct {
    const char *virtualDeviceUID;    // e.g. "app.openfaders.device.0"
    uint32_t outputChannelPairStart; // first of the two output channels, e.g. 0, 2, 4, 6, 8
} RoutingRoute;

// Creates and starts the engine. Matches the output device by name and each
// virtual device by UID, then begins forwarding audio. Returns NULL and prints
// the reason on failure. The returned engine must be released with
// RoutingEngineStop.
RoutingEngine *RoutingEngineStart(const char *outputDeviceName, const RoutingRoute *routes,
                                  uint32_t routeCount);

// Stops all IO, tears down the audio units, and frees the engine.
void RoutingEngineStop(RoutingEngine *engine);

#endif // OPENFADERS_ROUTING_ENGINE_H
