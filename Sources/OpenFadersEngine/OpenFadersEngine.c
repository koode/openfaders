// OpenFaders routing engine implementation.
//
// For each enabled route, an input AUHAL unit captures a virtual device's
// mirrored input (what applications played into it) and pushes it into a
// per-route ring. One output AUHAL unit bound to the physical multichannel device
// pulls from all the rings and places each route's stereo audio into its channel
// pair.
//
// Adaptive clock sync (drift correction) is introduced separately; this file
// carries the sync mode through the config and leaves the per-device rate control
// to that later stage. With hardware-timed sync the ring headroom absorbs drift.

#include "OpenFadersEngine.h"
#include "EngineRing.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    kMaxRenderFrames = 8192, // clamp for the fixed-size real-time scratch buffers
    kEngineChannels = 2,
    kTargetFillFrames = kEngineRingCapacityFrames / 2, // hold the ring half full
};

#define kEngineSampleRate 48000.0

// Custom driver property that slaves a virtual device's clock. This four-character
// selector must match kOpenFadersRateScalarSelector in the driver; the two targets
// share it as an ABI contract rather than a header.
enum { kOpenFadersRateScalarSelector = 'rtsc' };

// Drift-correction control loop. A PI controller on a normal-priority thread reads
// each ring's fill (the integrated phase error between the virtual and hardware
// clocks) four times a second and nudges the device's rate scalar to hold the fill
// at target. Gains are critically damped near 0.016 Hz; the clamp bounds the
// authority to +/-100 ppm. These are conservative starting values and may want
// tuning against a measured crystal mismatch.
#define kControlPeriodSeconds 0.25
#define kProportionalGain 0.2                             // Kp, 1/s
#define kIntegralGain 0.01                                // Ki, 1/s^2
#define kMaxRateScalar 1.0e-4                             // +/-100 ppm
#define kIntegralClamp (kMaxRateScalar / kIntegralGain)   // anti-windup bound on the integrator

typedef struct {
    AudioObjectID virtualDeviceID;
    AudioUnit inputUnit;
    uint32_t outputChannelPairStart;
    EngineRing ring;

    // Control-loop state, touched only by the control thread.
    double controlIntegral;
    double currentRateScalar;
} EngineRoute;

struct OpenFadersEngine {
    AudioObjectID outputDeviceID;
    AudioUnit outputUnit;
    uint32_t outputChannelCount;
    uint32_t routeCount;
    bool running;
    OpenFadersSyncMode syncMode;
    EngineRoute routes[kOpenFadersMaxRoutes];

    // Drift-correction control thread.
    _Atomic bool controlRunning;
    bool controlThreadStarted;
    pthread_t controlThread;
};

#pragma mark - Core Audio discovery helpers

static AudioObjectPropertyAddress MakeGlobalAddress(AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address = {selector, kAudioObjectPropertyScopeGlobal,
                                          kAudioObjectPropertyElementMain};
    return address;
}

// Copies the system device list. Returns the count and, via outDevices, a
// malloc'd array the caller must free.
static uint32_t CopyDeviceList(AudioObjectID **outDevices) {
    AudioObjectPropertyAddress address = MakeGlobalAddress(kAudioHardwarePropertyDevices);
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, NULL, &dataSize) != noErr) {
        return 0;
    }
    uint32_t deviceCount = dataSize / (UInt32)sizeof(AudioObjectID);
    AudioObjectID *devices = (AudioObjectID *)calloc(deviceCount, sizeof(AudioObjectID));
    if (devices == NULL) {
        return 0;
    }
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &dataSize, devices) !=
        noErr) {
        free(devices);
        return 0;
    }
    *outDevices = devices;
    return deviceCount;
}

// Copies a string property (UID or name) as a heap C string, or NULL. The caller
// frees the result.
static char *CopyDeviceStringProperty(AudioObjectID deviceID, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address = MakeGlobalAddress(selector);
    CFStringRef value = NULL;
    UInt32 dataSize = sizeof(value);
    if (AudioObjectGetPropertyData(deviceID, &address, 0, NULL, &dataSize, &value) != noErr ||
        value == NULL) {
        return NULL;
    }
    CFIndex maximumSize =
        CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1;
    char *buffer = (char *)malloc((size_t)maximumSize);
    if (buffer != NULL && !CFStringGetCString(value, buffer, maximumSize, kCFStringEncodingUTF8)) {
        free(buffer);
        buffer = NULL;
    }
    CFRelease(value); // a property get of a CFStringRef returns a +1 reference
    return buffer;
}

// Total channel count for a scope, summed across the device's stream buffers.
static uint32_t DeviceChannelCount(AudioObjectID deviceID, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress address = {kAudioDevicePropertyStreamConfiguration, scope,
                                          kAudioObjectPropertyElementMain};
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize(deviceID, &address, 0, NULL, &dataSize) != noErr) {
        return 0;
    }
    AudioBufferList *bufferList = (AudioBufferList *)malloc(dataSize);
    if (bufferList == NULL) {
        return 0;
    }
    uint32_t channelCount = 0;
    if (AudioObjectGetPropertyData(deviceID, &address, 0, NULL, &dataSize, bufferList) == noErr) {
        for (UInt32 bufferIndex = 0; bufferIndex < bufferList->mNumberBuffers; ++bufferIndex) {
            channelCount += bufferList->mBuffers[bufferIndex].mNumberChannels;
        }
    }
    free(bufferList);
    return channelCount;
}

// A device whose Core Audio name mentions a multitrack USB return and that exposes
// the full ten-channel bank is almost certainly the board we target. This is a
// functional detection heuristic, not a claim of affiliation.
static bool LooksLikeMultitrackBoard(const char *name, uint32_t outputChannelCount) {
    return name != NULL && strstr(name, "Multitrack") != NULL && outputChannelCount >= 10;
}

uint32_t OpenFadersEngineCopyOutputDevices(OpenFadersOutputDevice *outDevices, uint32_t capacity) {
    AudioObjectID *devices = NULL;
    uint32_t deviceCount = CopyDeviceList(&devices);
    uint32_t written = 0;
    for (uint32_t index = 0; index < deviceCount; ++index) {
        uint32_t outputChannelCount = DeviceChannelCount(devices[index], kAudioObjectPropertyScopeOutput);
        if (outputChannelCount < 2) {
            continue;
        }
        if (outDevices != NULL && written < capacity) {
            OpenFadersOutputDevice *entry = &outDevices[written];
            memset(entry, 0, sizeof(*entry));
            char *name = CopyDeviceStringProperty(devices[index], kAudioObjectPropertyName);
            char *uid = CopyDeviceStringProperty(devices[index], kAudioDevicePropertyDeviceUID);
            if (name != NULL) {
                strncpy(entry->name, name, sizeof(entry->name) - 1);
            }
            if (uid != NULL) {
                strncpy(entry->uid, uid, sizeof(entry->uid) - 1);
            }
            entry->outputChannelCount = outputChannelCount;
            entry->looksLikeMultitrackBoard = LooksLikeMultitrackBoard(name, outputChannelCount);
            free(name);
            free(uid);
        }
        written += 1;
    }
    free(devices);
    return (capacity == 0 || outDevices == NULL) ? written : (written < capacity ? written : capacity);
}

static AudioObjectID FindDeviceByUID(const char *uid) {
    if (uid == NULL || uid[0] == '\0') {
        return kAudioObjectUnknown;
    }
    AudioObjectID *devices = NULL;
    uint32_t deviceCount = CopyDeviceList(&devices);
    AudioObjectID match = kAudioObjectUnknown;
    for (uint32_t index = 0; index < deviceCount && match == kAudioObjectUnknown; ++index) {
        char *deviceUID = CopyDeviceStringProperty(devices[index], kAudioDevicePropertyDeviceUID);
        if (deviceUID != NULL && strcmp(deviceUID, uid) == 0) {
            match = devices[index];
        }
        free(deviceUID);
    }
    free(devices);
    return match;
}

static AudioObjectID FindOutputDeviceByName(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return kAudioObjectUnknown;
    }
    AudioObjectID *devices = NULL;
    uint32_t deviceCount = CopyDeviceList(&devices);
    AudioObjectID match = kAudioObjectUnknown;
    for (uint32_t index = 0; index < deviceCount && match == kAudioObjectUnknown; ++index) {
        char *deviceName = CopyDeviceStringProperty(devices[index], kAudioObjectPropertyName);
        if (deviceName != NULL && strcmp(deviceName, name) == 0 &&
            DeviceChannelCount(devices[index], kAudioObjectPropertyScopeOutput) > 0) {
            match = devices[index];
        }
        free(deviceName);
    }
    free(devices);
    return match;
}

// Re-pins the device's nominal sample rate to 48 kHz so a stale negotiated rate
// cannot leave the routing mismatched. Best effort.
static void PinNominalSampleRate(AudioObjectID deviceID) {
    AudioObjectPropertyAddress address = MakeGlobalAddress(kAudioDevicePropertyNominalSampleRate);
    Boolean settable = false;
    if (AudioObjectIsPropertySettable(deviceID, &address, &settable) == noErr && settable) {
        Float64 sampleRate = kEngineSampleRate;
        AudioObjectSetPropertyData(deviceID, &address, 0, NULL, sizeof(sampleRate), &sampleRate);
    }
}

// Requests an IO buffer size on the device. Best effort: the HAL may clamp it to a
// supported range, which is fine.
static void SetDeviceBufferFrameSize(AudioObjectID deviceID, uint32_t bufferFrames) {
    if (bufferFrames == 0) {
        return;
    }
    AudioObjectPropertyAddress address = MakeGlobalAddress(kAudioDevicePropertyBufferFrameSize);
    Boolean settable = false;
    if (AudioObjectIsPropertySettable(deviceID, &address, &settable) == noErr && settable) {
        UInt32 value = bufferFrames;
        AudioObjectSetPropertyData(deviceID, &address, 0, NULL, sizeof(value), &value);
    }
}

static AudioStreamBasicDescription MakeInterleavedFloatFormat(uint32_t channelCount) {
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = kEngineSampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = channelCount;
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = channelCount * (UInt32)sizeof(float);
    format.mBytesPerPacket = channelCount * (UInt32)sizeof(float);
    return format;
}

#pragma mark - Real-time callbacks

// Captures one virtual device's stereo input and writes it into that route's ring.
// inRefCon is the owning EngineRoute.
static OSStatus InputRenderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                                    const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                                    UInt32 inNumberFrames, AudioBufferList *ioData) {
    (void)ioData;
    EngineRoute *route = (EngineRoute *)inRefCon;
    uint32_t frameCount = inNumberFrames <= kMaxRenderFrames ? inNumberFrames : kMaxRenderFrames;

    float captureScratch[kMaxRenderFrames * kEngineChannels];
    AudioBufferList captureList;
    captureList.mNumberBuffers = 1;
    captureList.mBuffers[0].mNumberChannels = kEngineChannels;
    captureList.mBuffers[0].mDataByteSize = frameCount * kEngineChannels * (UInt32)sizeof(float);
    captureList.mBuffers[0].mData = captureScratch;

    OSStatus status = AudioUnitRender(route->inputUnit, ioActionFlags, inTimeStamp, inBusNumber,
                                      frameCount, &captureList);
    if (status == noErr) {
        EngineRingWrite(&route->ring, captureScratch, frameCount);
    }
    return noErr;
}

// Fills the physical device's interleaved multichannel output buffer, placing each
// route's stereo audio at its channel pair. inRefCon is the OpenFadersEngine.
static OSStatus OutputRenderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                                     const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                                     UInt32 inNumberFrames, AudioBufferList *ioData) {
    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;
    OpenFadersEngine *engine = (OpenFadersEngine *)inRefCon;
    float *output = (float *)ioData->mBuffers[0].mData;
    uint32_t channelCount = engine->outputChannelCount;

    memset(output, 0, ioData->mBuffers[0].mDataByteSize);

    uint32_t frameCount = inNumberFrames <= kMaxRenderFrames ? inNumberFrames : kMaxRenderFrames;
    float readScratch[kMaxRenderFrames * kEngineChannels];
    for (uint32_t routeIndex = 0; routeIndex < engine->routeCount; ++routeIndex) {
        EngineRoute *route = &engine->routes[routeIndex];
        uint32_t leftChannel = route->outputChannelPairStart;
        if (leftChannel + 1 >= channelCount) {
            continue;
        }
        EngineRingRead(&route->ring, readScratch, frameCount);
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            output[frame * channelCount + leftChannel + 0] = readScratch[frame * kEngineChannels + 0];
            output[frame * channelCount + leftChannel + 1] = readScratch[frame * kEngineChannels + 1];
        }
    }
    return noErr;
}

#pragma mark - Audio unit construction

static AudioComponent HALOutputComponent(void) {
    AudioComponentDescription description = {0};
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_HALOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    return AudioComponentFindNext(NULL, &description);
}

// Builds the input unit for one route, following the AUHAL input sequence: enable
// input bus, disable output bus, bind the device, set the format the unit hands
// us, install the callback, then initialise.
static OSStatus BuildInputUnit(EngineRoute *route) {
    AudioComponent component = HALOutputComponent();
    if (component == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    OSStatus status = AudioComponentInstanceNew(component, &route->inputUnit);
    if (status != noErr) {
        return status;
    }

    UInt32 enableFlag = 1;
    status = AudioUnitSetProperty(route->inputUnit, kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Input, 1, &enableFlag, sizeof(enableFlag));
    if (status != noErr) {
        return status;
    }
    UInt32 disableFlag = 0;
    status = AudioUnitSetProperty(route->inputUnit, kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Output, 0, &disableFlag, sizeof(disableFlag));
    if (status != noErr) {
        return status;
    }
    status = AudioUnitSetProperty(route->inputUnit, kAudioOutputUnitProperty_CurrentDevice,
                                  kAudioUnitScope_Global, 0, &route->virtualDeviceID,
                                  sizeof(route->virtualDeviceID));
    if (status != noErr) {
        return status;
    }
    AudioStreamBasicDescription format = MakeInterleavedFloatFormat(kEngineChannels);
    status = AudioUnitSetProperty(route->inputUnit, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Output, 1, &format, sizeof(format));
    if (status != noErr) {
        return status;
    }
    AURenderCallbackStruct callback = {InputRenderCallback, route};
    status = AudioUnitSetProperty(route->inputUnit, kAudioOutputUnitProperty_SetInputCallback,
                                  kAudioUnitScope_Global, 0, &callback, sizeof(callback));
    if (status != noErr) {
        return status;
    }
    return AudioUnitInitialize(route->inputUnit);
}

// Builds the single output unit bound to the physical device. We supply an
// interleaved buffer with the device's channel count and fill it in the render
// callback.
static OSStatus BuildOutputUnit(OpenFadersEngine *engine) {
    AudioComponent component = HALOutputComponent();
    if (component == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    OSStatus status = AudioComponentInstanceNew(component, &engine->outputUnit);
    if (status != noErr) {
        return status;
    }
    status = AudioUnitSetProperty(engine->outputUnit, kAudioOutputUnitProperty_CurrentDevice,
                                  kAudioUnitScope_Global, 0, &engine->outputDeviceID,
                                  sizeof(engine->outputDeviceID));
    if (status != noErr) {
        return status;
    }
    AudioStreamBasicDescription format = MakeInterleavedFloatFormat(engine->outputChannelCount);
    status = AudioUnitSetProperty(engine->outputUnit, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input, 0, &format, sizeof(format));
    if (status != noErr) {
        return status;
    }
    AURenderCallbackStruct callback = {OutputRenderCallback, engine};
    status = AudioUnitSetProperty(engine->outputUnit, kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input, 0, &callback, sizeof(callback));
    if (status != noErr) {
        return status;
    }
    return AudioUnitInitialize(engine->outputUnit);
}

#pragma mark - Drift-correction control loop

// Pushes a rate correction to a virtual device via the driver's custom property.
// Called only from the control thread. It is marshaled as a CFNumber, which is how
// the driver receives it across the HAL boundary.
static void SetDeviceRateScalar(AudioObjectID deviceID, double scalar) {
    AudioObjectPropertyAddress address = {kOpenFadersRateScalarSelector,
                                          kAudioObjectPropertyScopeGlobal,
                                          kAudioObjectPropertyElementMain};
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &scalar);
    if (number == NULL) {
        return;
    }
    AudioObjectSetPropertyData(deviceID, &address, 0, NULL, (UInt32)sizeof(CFPropertyListRef),
                               &number);
    CFRelease(number);
}

// One PI update for a single route: drive the ring fill toward target by nudging
// the device rate scalar, with a clamped output and an anti-windup integrator.
static void UpdateRouteControl(EngineRoute *route) {
    uint64_t fillFrames = EngineRingFillFrames(&route->ring);
    double errorSeconds = ((double)kTargetFillFrames - (double)fillFrames) / kEngineSampleRate;

    double proposedIntegral = route->controlIntegral + errorSeconds * kControlPeriodSeconds;
    if (proposedIntegral > kIntegralClamp) {
        proposedIntegral = kIntegralClamp;
    } else if (proposedIntegral < -kIntegralClamp) {
        proposedIntegral = -kIntegralClamp;
    }

    double output = kProportionalGain * errorSeconds + kIntegralGain * proposedIntegral;
    if (output > kMaxRateScalar) {
        output = kMaxRateScalar; // saturated: freeze the integrator (anti-windup)
    } else if (output < -kMaxRateScalar) {
        output = -kMaxRateScalar;
    } else {
        route->controlIntegral = proposedIntegral; // commit growth only when unsaturated
    }

    route->currentRateScalar = output;
    SetDeviceRateScalar(route->virtualDeviceID, output);
}

static void *ControlLoopMain(void *context) {
    OpenFadersEngine *engine = (OpenFadersEngine *)context;
    while (atomic_load_explicit(&engine->controlRunning, memory_order_acquire)) {
        for (uint32_t index = 0; index < engine->routeCount; ++index) {
            UpdateRouteControl(&engine->routes[index]);
        }
        usleep((useconds_t)(kControlPeriodSeconds * 1.0e6));
    }
    return NULL;
}

// Starts the control thread when adaptive sync is selected. In hardware-timed mode
// the ring headroom alone absorbs drift, so no loop runs.
static void StartControlLoop(OpenFadersEngine *engine) {
    if (engine->syncMode != kOpenFadersSyncAdaptive) {
        return;
    }
    for (uint32_t index = 0; index < engine->routeCount; ++index) {
        engine->routes[index].controlIntegral = 0.0;
        engine->routes[index].currentRateScalar = 0.0;
    }
    atomic_store_explicit(&engine->controlRunning, true, memory_order_release);
    if (pthread_create(&engine->controlThread, NULL, ControlLoopMain, engine) == 0) {
        engine->controlThreadStarted = true;
    } else {
        atomic_store_explicit(&engine->controlRunning, false, memory_order_release);
    }
}

static void StopControlLoop(OpenFadersEngine *engine) {
    if (!engine->controlThreadStarted) {
        return;
    }
    atomic_store_explicit(&engine->controlRunning, false, memory_order_release);
    pthread_join(engine->controlThread, NULL);
    engine->controlThreadStarted = false;

    // Release each device's clock back to free-running.
    for (uint32_t index = 0; index < engine->routeCount; ++index) {
        SetDeviceRateScalar(engine->routes[index].virtualDeviceID, 0.0);
    }
}

#pragma mark - Config builders

void OpenFadersEngineConfigInit(OpenFadersEngineConfig *config) {
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->syncMode = kOpenFadersSyncHardwareTimed;
    config->bufferFrames = 0;
}

void OpenFadersEngineConfigSetOutput(OpenFadersEngineConfig *config, const char *uid,
                                     const char *name) {
    if (config == NULL) {
        return;
    }
    config->outputDeviceUID[0] = '\0';
    config->outputDeviceName[0] = '\0';
    if (uid != NULL) {
        strncpy(config->outputDeviceUID, uid, sizeof(config->outputDeviceUID) - 1);
    }
    if (name != NULL) {
        strncpy(config->outputDeviceName, name, sizeof(config->outputDeviceName) - 1);
    }
}

bool OpenFadersEngineConfigAddRoute(OpenFadersEngineConfig *config, const char *virtualDeviceUID,
                                    uint32_t outputChannelPairStart, bool enabled) {
    if (config == NULL || virtualDeviceUID == NULL || config->routeCount >= kOpenFadersMaxRoutes) {
        return false;
    }
    OpenFadersRoute *route = &config->routes[config->routeCount];
    memset(route, 0, sizeof(*route));
    strncpy(route->virtualDeviceUID, virtualDeviceUID, sizeof(route->virtualDeviceUID) - 1);
    route->outputChannelPairStart = outputChannelPairStart;
    route->enabled = enabled;
    config->routeCount += 1;
    return true;
}

#pragma mark - Lifecycle

OpenFadersEngine *OpenFadersEngineCreate(void) {
    return (OpenFadersEngine *)calloc(1, sizeof(OpenFadersEngine));
}

static void WriteError(char *outError, uint32_t errorCapacity, const char *message) {
    if (outError != NULL && errorCapacity > 0) {
        strncpy(outError, message, errorCapacity - 1);
        outError[errorCapacity - 1] = '\0';
    }
}

// Tears down all audio units without freeing the engine, leaving it idle.
static void TeardownUnits(OpenFadersEngine *engine) {
    StopControlLoop(engine);
    if (engine->outputUnit != NULL) {
        AudioOutputUnitStop(engine->outputUnit);
        AudioUnitUninitialize(engine->outputUnit);
        AudioComponentInstanceDispose(engine->outputUnit);
        engine->outputUnit = NULL;
    }
    for (uint32_t index = 0; index < engine->routeCount; ++index) {
        AudioUnit inputUnit = engine->routes[index].inputUnit;
        if (inputUnit != NULL) {
            AudioOutputUnitStop(inputUnit);
            AudioUnitUninitialize(inputUnit);
            AudioComponentInstanceDispose(inputUnit);
            engine->routes[index].inputUnit = NULL;
        }
    }
    engine->routeCount = 0;
    engine->outputUnit = NULL;
}

bool OpenFadersEngineStart(OpenFadersEngine *engine, const OpenFadersEngineConfig *config,
                           char *outError, uint32_t errorCapacity) {
    if (engine == NULL || config == NULL) {
        WriteError(outError, errorCapacity, "Invalid engine or configuration.");
        return false;
    }
    if (engine->running) {
        OpenFadersEngineStop(engine);
    }

    AudioObjectID outputDeviceID = FindDeviceByUID(config->outputDeviceUID);
    if (outputDeviceID == kAudioObjectUnknown) {
        outputDeviceID = FindOutputDeviceByName(config->outputDeviceName);
    }
    if (outputDeviceID == kAudioObjectUnknown) {
        WriteError(outError, errorCapacity,
                   "Output device not found. Connect the RODECaster and set it to Multitrack USB mode.");
        return false;
    }

    engine->outputDeviceID = outputDeviceID;
    engine->outputChannelCount = DeviceChannelCount(outputDeviceID, kAudioObjectPropertyScopeOutput);
    engine->syncMode = config->syncMode;
    PinNominalSampleRate(outputDeviceID);
    SetDeviceBufferFrameSize(outputDeviceID, config->bufferFrames);

    uint32_t requestedRoutes = config->routeCount <= kOpenFadersMaxRoutes ? config->routeCount
                                                                          : kOpenFadersMaxRoutes;
    engine->routeCount = 0;
    for (uint32_t index = 0; index < requestedRoutes; ++index) {
        if (!config->routes[index].enabled) {
            continue;
        }
        AudioObjectID virtualDeviceID = FindDeviceByUID(config->routes[index].virtualDeviceUID);
        if (virtualDeviceID == kAudioObjectUnknown) {
            continue; // a device the user disabled or hasn't installed; skip quietly
        }
        EngineRoute *route = &engine->routes[engine->routeCount];
        route->virtualDeviceID = virtualDeviceID;
        route->outputChannelPairStart = config->routes[index].outputChannelPairStart;
        route->inputUnit = NULL;
        EngineRingReset(&route->ring);
        PinNominalSampleRate(virtualDeviceID);

        OSStatus status = BuildInputUnit(route);
        if (status != noErr) {
            WriteError(outError, errorCapacity, "Failed to open a virtual device for capture.");
            TeardownUnits(engine);
            return false;
        }
        engine->routeCount += 1;
    }

    if (engine->routeCount == 0) {
        WriteError(outError, errorCapacity, "No enabled virtual devices were found to route.");
        TeardownUnits(engine);
        return false;
    }

    if (BuildOutputUnit(engine) != noErr) {
        WriteError(outError, errorCapacity, "Failed to open the output device.");
        TeardownUnits(engine);
        return false;
    }

    // Start the inputs first so the rings begin filling before the output pulls.
    for (uint32_t index = 0; index < engine->routeCount; ++index) {
        AudioOutputUnitStart(engine->routes[index].inputUnit);
    }
    if (AudioOutputUnitStart(engine->outputUnit) != noErr) {
        WriteError(outError, errorCapacity, "Failed to start the output device.");
        TeardownUnits(engine);
        return false;
    }

    StartControlLoop(engine);
    engine->running = true;
    return true;
}

void OpenFadersEngineStop(OpenFadersEngine *engine) {
    if (engine == NULL || !engine->running) {
        return;
    }
    TeardownUnits(engine);
    engine->running = false;
}

void OpenFadersEngineDestroy(OpenFadersEngine *engine) {
    if (engine == NULL) {
        return;
    }
    OpenFadersEngineStop(engine);
    free(engine);
}

void OpenFadersEngineGetStatus(OpenFadersEngine *engine, OpenFadersEngineStatus *outStatus) {
    if (outStatus == NULL) {
        return;
    }
    memset(outStatus, 0, sizeof(*outStatus));
    if (engine == NULL) {
        return;
    }
    outStatus->running = engine->running;
    outStatus->activeRouteCount = engine->routeCount;
    outStatus->outputDevicePresent =
        engine->running && engine->outputDeviceID != kAudioObjectUnknown;
    for (uint32_t index = 0; index < engine->routeCount; ++index) {
        outStatus->overrunFrames +=
            atomic_load_explicit(&engine->routes[index].ring.overrunFrames, memory_order_relaxed);
        outStatus->underrunFrames +=
            atomic_load_explicit(&engine->routes[index].ring.underrunFrames, memory_order_relaxed);
    }
}
