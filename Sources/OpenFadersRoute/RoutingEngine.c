// OpenFaders routing engine (Phase 1) implementation.
//
// For each virtual device, an input AUHAL unit captures the device's mirrored
// input (what applications played into it) and pushes it into a per-device ring.
// One output AUHAL unit bound to the physical multichannel device pulls from all
// the rings and places each device's stereo audio into its channel pair.

#include "RoutingEngine.h"
#include "RouteRing.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kMaxRoutes = 16,
    kMaxRenderFrames = 8192, // clamp for the fixed-size real-time scratch buffers
    kEngineChannels = 2,
};

#define kEngineSampleRate 48000.0

typedef struct {
    AudioObjectID virtualDeviceID;
    AudioUnit inputUnit;
    uint32_t outputChannelPairStart;
    RouteRing ring;
} EngineRoute;

struct RoutingEngine {
    AudioObjectID outputDeviceID;
    AudioUnit outputUnit;
    uint32_t outputChannelCount;
    uint32_t routeCount;
    EngineRoute routes[kMaxRoutes];
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

static AudioObjectID FindDeviceByUID(const char *uid) {
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

// Captures one virtual device's stereo input and writes it into that device's
// ring. inRefCon is the owning EngineRoute.
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
        RouteRingWrite(&route->ring, captureScratch, frameCount);
    }
    return noErr;
}

// Fills the physical device's interleaved multichannel output buffer, placing
// each route's stereo audio at its channel pair. inRefCon is the RoutingEngine.
static OSStatus OutputRenderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                                     const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                                     UInt32 inNumberFrames, AudioBufferList *ioData) {
    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;
    RoutingEngine *engine = (RoutingEngine *)inRefCon;
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
        RouteRingRead(&route->ring, readScratch, frameCount);
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

// Builds the input unit for one route, following the AUHAL input sequence:
// enable input bus, disable output bus, bind the device, set the format the unit
// hands us, install the callback, then initialise.
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
static OSStatus BuildOutputUnit(RoutingEngine *engine) {
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

#pragma mark - Public API

RoutingEngine *RoutingEngineStart(const char *outputDeviceName, const RoutingRoute *routes,
                                  uint32_t routeCount) {
    if (routeCount > kMaxRoutes) {
        routeCount = kMaxRoutes;
    }

    AudioObjectID outputDeviceID = FindOutputDeviceByName(outputDeviceName);
    if (outputDeviceID == kAudioObjectUnknown) {
        fprintf(stderr, "OpenFaders: output device not found: \"%s\"\n", outputDeviceName);
        fprintf(stderr, "  Make sure the RODECaster is connected and set to Multitrack USB mode.\n");
        return NULL;
    }

    RoutingEngine *engine = (RoutingEngine *)calloc(1, sizeof(RoutingEngine));
    if (engine == NULL) {
        return NULL;
    }
    engine->outputDeviceID = outputDeviceID;
    engine->outputChannelCount = DeviceChannelCount(outputDeviceID, kAudioObjectPropertyScopeOutput);
    PinNominalSampleRate(outputDeviceID);

    printf("Output device: \"%s\" (%u output channels)\n", outputDeviceName,
           engine->outputChannelCount);

    for (uint32_t index = 0; index < routeCount; ++index) {
        AudioObjectID virtualDeviceID = FindDeviceByUID(routes[index].virtualDeviceUID);
        if (virtualDeviceID == kAudioObjectUnknown) {
            fprintf(stderr, "OpenFaders: skipping missing virtual device \"%s\"\n",
                    routes[index].virtualDeviceUID);
            continue;
        }
        EngineRoute *route = &engine->routes[engine->routeCount];
        route->virtualDeviceID = virtualDeviceID;
        route->outputChannelPairStart = routes[index].outputChannelPairStart;
        RouteRingReset(&route->ring);
        PinNominalSampleRate(virtualDeviceID);

        OSStatus status = BuildInputUnit(route);
        if (status != noErr) {
            fprintf(stderr, "OpenFaders: failed to build input for \"%s\" (status %d)\n",
                    routes[index].virtualDeviceUID, (int)status);
            continue;
        }
        printf("Route: \"%s\" -> output channels %u/%u\n", routes[index].virtualDeviceUID,
               route->outputChannelPairStart, route->outputChannelPairStart + 1);
        engine->routeCount += 1;
    }

    if (engine->routeCount == 0) {
        fprintf(stderr, "OpenFaders: no virtual devices matched; nothing to route.\n");
        RoutingEngineStop(engine);
        return NULL;
    }

    if (BuildOutputUnit(engine) != noErr) {
        fprintf(stderr, "OpenFaders: failed to build the output unit.\n");
        RoutingEngineStop(engine);
        return NULL;
    }

    // Start the inputs first so the rings begin filling before the output pulls.
    for (uint32_t index = 0; index < engine->routeCount; ++index) {
        AudioOutputUnitStart(engine->routes[index].inputUnit);
    }
    if (AudioOutputUnitStart(engine->outputUnit) != noErr) {
        fprintf(stderr, "OpenFaders: failed to start the output unit.\n");
        RoutingEngineStop(engine);
        return NULL;
    }

    return engine;
}

void RoutingEngineStop(RoutingEngine *engine) {
    if (engine == NULL) {
        return;
    }
    if (engine->outputUnit != NULL) {
        AudioOutputUnitStop(engine->outputUnit);
        AudioUnitUninitialize(engine->outputUnit);
        AudioComponentInstanceDispose(engine->outputUnit);
    }
    for (uint32_t index = 0; index < engine->routeCount; ++index) {
        AudioUnit inputUnit = engine->routes[index].inputUnit;
        if (inputUnit != NULL) {
            AudioOutputUnitStop(inputUnit);
            AudioUnitUninitialize(inputUnit);
            AudioComponentInstanceDispose(inputUnit);
        }
    }
    free(engine);
}
