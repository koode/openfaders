// OpenFaders virtual audio driver: shared declarations.
//
// This is a from-scratch Core Audio AudioServerPlugIn. It publishes a fixed set
// of stereo, 48 kHz loopback virtual output devices. Each device exposes an
// output stream (applications play into it) and a mirrored input stream, so a
// separate routing engine can read back exactly what was played and forward it
// to the physical hardware.
//
// Written against the public <CoreAudio/AudioServerPlugIn.h> API only. See the
// clean-room design notes for provenance.

#ifndef OPENFADERS_DRIVER_H
#define OPENFADERS_DRIVER_H

#include <CoreAudio/AudioServerPlugIn.h>
#include <mach/mach_time.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

// When set, a mirrored-input read clears the ring slots it consumed. This gives
// silence when playback stops (rather than replaying stale audio a ring-length
// later) and suits our single-consumer routing model. See LoopbackRing.h.
#define OPENFADERS_CLEAR_RING_ON_READ 1

// Audio format. Everything is stereo 32-bit float at 48 kHz to match the
// RODECaster Pro II hardware, which removes any sample-rate conversion.
enum {
    kOpenFadersChannelsPerFrame = 2,
    kOpenFadersBytesPerFrame = kOpenFadersChannelsPerFrame * (uint32_t)sizeof(float),
};
#define kOpenFadersNominalSampleRate 48000.0

// Loopback ring. Indexed by absolute IO-cycle sample time modulo its capacity
// (the model the adversarial review verified against BlackHole), so the offset
// between the application writing and the engine reading is deterministic rather
// than set by scheduling jitter. Capacity is a power of two so the modulo is a
// mask. It must comfortably exceed the largest IO buffer the HAL will request.
enum {
    kOpenFadersRingCapacityFrames = 1u << 16, // 65536 frames, ~1.36 s of headroom
    kOpenFadersRingCapacityMask = kOpenFadersRingCapacityFrames - 1u,
};

// Frames between successive zero timestamps. GetZeroTimeStamp advances the
// device timeline by exactly this many frames per step, and it is reported
// verbatim as kAudioDevicePropertyZeroTimeStampPeriod. It must be at least the
// largest IO buffer the HAL uses; 16384 is safely above typical buffer sizes.
enum {
    kOpenFadersZeroTimeStampPeriodFrames = 1u << 14, // 16384
    kOpenFadersSafetyOffsetFrames = 32,
};

// Object identity scheme. The plug-in object has the fixed id
// kAudioObjectPlugInObject. Every device and its sub-objects get deterministic
// ids so classifying an AudioObjectID is arithmetic, not a lookup. Each device
// reserves a stride of ids for its own sub-objects.
enum {
    kOpenFadersDeviceBaseObjectID = 1000,
    kOpenFadersObjectIDStride = 10,
    kOpenFadersDeviceCount = 5, // Phase 1: one device per hardware fader pair
    kOpenFadersMaxDevices = 16, // hard cap on static storage
};

typedef enum {
    kOpenFadersObjectClass_PlugIn,
    kOpenFadersObjectClass_Device,
    kOpenFadersObjectClass_OutputStream,
    kOpenFadersObjectClass_InputStream,
    kOpenFadersObjectClass_Unknown,
} OpenFadersObjectClass;

// One virtual loopback device. All large storage lives here and is allocated
// once, at Initialize, so no real-time path ever allocates.
typedef struct {
    AudioObjectID deviceObjectID;
    AudioObjectID outputStreamObjectID;
    AudioObjectID inputStreamObjectID;

    char uid[128];  // stable unique id, e.g. "app.openfaders.device.0"
    char name[128]; // user-visible device name

    // Loopback ring, interleaved stereo float, indexed by sample time.
    float ringSamples[kOpenFadersRingCapacityFrames * kOpenFadersChannelsPerFrame];

    // IO run state. ioRunningClients counts StartIO/StopIO calls across clients.
    _Atomic uint32_t ioRunningClients;
    _Atomic bool isRunning;

    // Zero-timestamp clock state. The device has no hardware FIFO, so we
    // synthesise a steady 48 kHz timeline from mach_absolute_time. The anchor is
    // advanced forward in whole periods so the reported sample time is monotonic
    // by construction. Only the device's single IO thread advances the anchor;
    // StartIO re-anchors while IO is stopped.
    _Atomic uint64_t anchorHostTime;    // integer host ticks at anchorSampleFrames
    _Atomic uint64_t anchorSampleFrames; // device timeline position, in frames
    _Atomic uint64_t timelineSeed;       // bumped only on a discontinuous reset
    double anchorHostTimeFraction;       // sub-tick remainder, IO thread only

    // Clock-slave seam for Phase 2. A multiplier on host-ticks-per-frame, 1.0 is
    // free-running. The routing engine will drive it to cancel drift against the
    // hardware clock. Phase 1 leaves it at 1.0.
    _Atomic double clockRatio;
} OpenFadersVirtualDevice;

// Process-wide driver state, built once at Initialize.
typedef struct {
    bool initialized;
    AudioServerPlugInHostRef host; // used to post change notifications
    uint32_t deviceCount;
    double hostTicksPerFrame; // mach timebase converted to ticks per 48 kHz frame
    OpenFadersVirtualDevice devices[kOpenFadersMaxDevices];
} OpenFadersDriverState;

extern OpenFadersDriverState gOpenFadersDriver;

// Classifies an AudioObjectID and, for device-owned objects, reports the owning
// device index. Defined in Driver.c, used by the property dispatch in
// Properties.c.
OpenFadersObjectClass OpenFadersClassifyObject(AudioObjectID objectID, uint32_t *outDeviceIndex);

// Returns the device owning objectID (the device itself or one of its streams),
// or NULL if objectID does not belong to a device.
OpenFadersVirtualDevice *OpenFadersDeviceForObjectID(AudioObjectID objectID);

// The single canonical stream format shared by every stream, in both its
// virtual and physical form.
AudioStreamBasicDescription OpenFadersStreamFormat(void);

// Property entry points, implemented in Properties.c and wired into the vtable
// in Driver.c.
Boolean OpenFaders_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                               pid_t inClientProcessID, const AudioObjectPropertyAddress *inAddress);
OSStatus OpenFaders_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                       pid_t inClientProcessID,
                                       const AudioObjectPropertyAddress *inAddress,
                                       Boolean *outIsSettable);
OSStatus OpenFaders_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                        pid_t inClientProcessID,
                                        const AudioObjectPropertyAddress *inAddress,
                                        UInt32 inQualifierDataSize, const void *inQualifierData,
                                        UInt32 *outDataSize);
OSStatus OpenFaders_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                    pid_t inClientProcessID,
                                    const AudioObjectPropertyAddress *inAddress,
                                    UInt32 inQualifierDataSize, const void *inQualifierData,
                                    UInt32 inDataSize, UInt32 *outDataSize, void *outData);
OSStatus OpenFaders_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                    pid_t inClientProcessID,
                                    const AudioObjectPropertyAddress *inAddress,
                                    UInt32 inQualifierDataSize, const void *inQualifierData,
                                    UInt32 inDataSize, const void *inData);

#endif // OPENFADERS_DRIVER_H
