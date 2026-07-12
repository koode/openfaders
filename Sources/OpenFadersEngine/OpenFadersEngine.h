// OpenFaders routing engine — public C API.
//
// The engine captures the mirrored input of each OpenFaders virtual device and
// plays it into a stereo channel pair of a physical multichannel output device
// (the RODECaster Pro II "Main Multitrack" USB output). It is built as a static
// library so both the command-line diagnostic tool and the menu-bar app link the
// same real-time core; the app drives it through this API and runs it in-process,
// which is why launching it triggers the microphone/audio-input permission prompt
// (the mirrored device inputs are audio-input streams).
//
// Threading: OpenFadersEngineCreate/Start/Stop/Destroy and the enumeration and
// status calls are meant to be used from one control thread (the app's main
// actor is fine). The audio work happens on Core Audio's real-time threads inside
// the engine and never blocks.

#ifndef OPENFADERS_ENGINE_H
#define OPENFADERS_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    kOpenFadersMaxRoutes = 16,
    kOpenFadersUIDLength = 256,
    kOpenFadersNameLength = 256,
};

// A candidate physical output device the engine can target.
typedef struct {
    char uid[kOpenFadersUIDLength];
    char name[kOpenFadersNameLength];
    uint32_t outputChannelCount;
    bool looksLikeMultitrackBoard; // heuristic: name mentions the board and it has >= 10 output channels
} OpenFadersOutputDevice;

// One virtual-device-to-hardware-channel route.
typedef struct {
    char virtualDeviceUID[kOpenFadersUIDLength]; // e.g. "app.openfaders.device.0"
    uint32_t outputChannelPairStart;             // first of the two output channels: 0, 2, 4, 6, 8
    bool enabled;                                // disabled routes are captured-but-muted (skipped)
} OpenFadersRoute;

typedef enum {
    // Rely on ring headroom only; the virtual and hardware clocks run free. Good
    // enough for short sessions and the Phase 1 proof.
    kOpenFadersSyncHardwareTimed = 0,
    // Drive each virtual device's clock with a control loop so the ring fill stays
    // constant and the clocks stay locked over long runs (no resampling).
    kOpenFadersSyncAdaptive = 1,
} OpenFadersSyncMode;

typedef struct {
    // Preferred: match the output device by UID. If outputDeviceUID is empty, the
    // engine falls back to matching outputDeviceName (with an output-channel-count
    // check), which is how the board is found before its UID is known.
    char outputDeviceUID[kOpenFadersUIDLength];
    char outputDeviceName[kOpenFadersNameLength];

    OpenFadersRoute routes[kOpenFadersMaxRoutes];
    uint32_t routeCount;

    uint32_t bufferFrames;       // desired IO buffer size in frames; 0 leaves the device default
    OpenFadersSyncMode syncMode;
} OpenFadersEngineConfig;

typedef struct {
    bool running;
    bool outputDevicePresent;
    uint32_t activeRouteCount;
    uint64_t overrunFrames;  // summed across routes since start; a rising value means audio was dropped
    uint64_t underrunFrames; // summed across routes since start; a rising value means silence was inserted
} OpenFadersEngineStatus;

typedef struct OpenFadersEngine OpenFadersEngine;

// Config builders. These exist mainly so callers in languages where writing into
// fixed-size C char arrays is awkward (Swift) can assemble a configuration with
// plain C strings. They are equivalent to zeroing the struct and filling the
// fields by hand.

// Zeroes the config and sets safe defaults (hardware-timed sync, device default
// buffer).
void OpenFadersEngineConfigInit(OpenFadersEngineConfig *config);

// Sets the target output device. Either identifier may be empty; the engine
// prefers the UID and falls back to the name.
void OpenFadersEngineConfigSetOutput(OpenFadersEngineConfig *config, const char *uid,
                                     const char *name);

// Appends one route. Returns false if the route table is already full.
bool OpenFadersEngineConfigAddRoute(OpenFadersEngineConfig *config, const char *virtualDeviceUID,
                                    uint32_t outputChannelPairStart, bool enabled);

// Fills outDevices (up to capacity) with the output devices that have at least two
// output channels. Returns the number written. Pass a null buffer with capacity 0
// to just probe; the return value is clamped to capacity. Safe to call anytime.
uint32_t OpenFadersEngineCopyOutputDevices(OpenFadersOutputDevice *outDevices, uint32_t capacity);

// Creates an idle engine. Returns NULL only on allocation failure.
OpenFadersEngine *OpenFadersEngineCreate(void);

// Resolves the configured devices, builds the audio units, and starts forwarding
// audio. Returns false and writes a human-readable reason into outError (if
// non-null) on failure. Calling Start on an already-running engine restarts it
// with the new configuration.
bool OpenFadersEngineStart(OpenFadersEngine *engine, const OpenFadersEngineConfig *config,
                           char *outError, uint32_t errorCapacity);

// Stops all IO and tears down the audio units. The engine stays allocated and can
// be started again. Safe to call when already stopped.
void OpenFadersEngineStop(OpenFadersEngine *engine);

// Stops (if needed) and frees the engine.
void OpenFadersEngineDestroy(OpenFadersEngine *engine);

// Snapshots the current engine status. Safe to call from the control thread while
// audio is running.
void OpenFadersEngineGetStatus(OpenFadersEngine *engine, OpenFadersEngineStatus *outStatus);

#ifdef __cplusplus
}
#endif

#endif // OPENFADERS_ENGINE_H
