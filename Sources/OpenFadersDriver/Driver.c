// OpenFaders virtual audio driver: factory, lifecycle, real-time IO, and clock.
//
// This file owns the AudioServerPlugInDriverInterface vtable and everything on
// the real-time IO threads. The property model lives in Properties.c. See
// OpenFadersDriver.h for the object model and state layout.

#include "OpenFadersDriver.h"
#include "LoopbackRing.h"

#include <CoreFoundation/CFPlugInCOM.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <string.h>

OpenFadersDriverState gOpenFadersDriver = {0};

// The driver is a process-lifetime singleton. The reference count is maintained
// for COM correctness, but the underlying storage is static and never freed.
static _Atomic uint32_t gInterfaceReferenceCount = 1;

// Default device set: one stereo loopback device per RODECaster Pro II Main
// Multitrack USB return pair. Each name mirrors the board source that pair feeds
// (device 0 -> channels 0/1 "System", device 1 -> 2/3 "Game", and so on) so the
// macOS output picker reads the same as the board's own channel. The UIDs stay
// index-based and stable; only the display names carry the source label.
static const struct {
    const char *uid;
    const char *name;
} kOpenFadersDefaultDevices[kOpenFadersDeviceCount] = {
    {"app.openfaders.device.0", "OpenFaders System"},
    {"app.openfaders.device.1", "OpenFaders Game"},
    {"app.openfaders.device.2", "OpenFaders Music"},
    {"app.openfaders.device.3", "OpenFaders Virtual A"},
    {"app.openfaders.device.4", "OpenFaders Virtual B"},
};

#pragma mark - Object classification

OpenFadersObjectClass OpenFadersClassifyObject(AudioObjectID objectID, uint32_t *outDeviceIndex) {
    if (objectID == kAudioObjectPlugInObject) {
        return kOpenFadersObjectClass_PlugIn;
    }
    if (objectID < kOpenFadersDeviceBaseObjectID) {
        return kOpenFadersObjectClass_Unknown;
    }
    uint32_t offset = objectID - kOpenFadersDeviceBaseObjectID;
    uint32_t deviceIndex = offset / kOpenFadersObjectIDStride;
    uint32_t slot = offset % kOpenFadersObjectIDStride;
    if (deviceIndex >= gOpenFadersDriver.deviceCount) {
        return kOpenFadersObjectClass_Unknown;
    }
    if (outDeviceIndex != NULL) {
        *outDeviceIndex = deviceIndex;
    }
    switch (slot) {
        case 0: return kOpenFadersObjectClass_Device;
        case 1: return kOpenFadersObjectClass_OutputStream;
        case 2: return kOpenFadersObjectClass_InputStream;
        default: return kOpenFadersObjectClass_Unknown;
    }
}

OpenFadersVirtualDevice *OpenFadersDeviceForObjectID(AudioObjectID objectID) {
    uint32_t deviceIndex = 0;
    OpenFadersObjectClass objectClass = OpenFadersClassifyObject(objectID, &deviceIndex);
    switch (objectClass) {
        case kOpenFadersObjectClass_Device:
        case kOpenFadersObjectClass_OutputStream:
        case kOpenFadersObjectClass_InputStream:
            return &gOpenFadersDriver.devices[deviceIndex];
        default:
            return NULL;
    }
}

AudioStreamBasicDescription OpenFadersStreamFormat(void) {
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = kOpenFadersNominalSampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = kOpenFadersBytesPerFrame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = kOpenFadersBytesPerFrame;
    format.mChannelsPerFrame = kOpenFadersChannelsPerFrame;
    format.mBitsPerChannel = 32;
    return format;
}

#pragma mark - Timeline

// Establishes a fresh device timeline anchored at the current host time and
// bumps the seed so the HAL treats the restarted timeline as discontinuous.
// Called from StartIO on the 0 -> 1 client transition, while IO is stopped.
static void OpenFadersAnchorTimeline(OpenFadersVirtualDevice *device) {
    uint64_t now = mach_absolute_time();
    device->anchorHostTimeFraction = 0.0;
    atomic_store_explicit(&device->anchorSampleFrames, 0, memory_order_release);
    atomic_store_explicit(&device->anchorHostTime, now, memory_order_release);
    atomic_fetch_add_explicit(&device->timelineSeed, 1, memory_order_release);
}

#pragma mark - COM plumbing

static HRESULT OpenFaders_QueryInterface(void *inDriver, REFIID inUUID, LPVOID *outInterface) {
    if (outInterface == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    CFUUIDRef requestedUUID = CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault, inUUID);
    if (requestedUUID == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    HRESULT result = E_NOINTERFACE;
    if (CFEqual(requestedUUID, IUnknownUUID) ||
        CFEqual(requestedUUID, kAudioServerPlugInDriverInterfaceUUID)) {
        atomic_fetch_add_explicit(&gInterfaceReferenceCount, 1, memory_order_relaxed);
        *outInterface = inDriver;
        result = S_OK;
    }
    CFRelease(requestedUUID);
    return result;
}

static ULONG OpenFaders_AddRef(void *inDriver) {
    (void)inDriver;
    return atomic_fetch_add_explicit(&gInterfaceReferenceCount, 1, memory_order_relaxed) + 1;
}

static ULONG OpenFaders_Release(void *inDriver) {
    (void)inDriver;
    uint32_t previous = atomic_fetch_sub_explicit(&gInterfaceReferenceCount, 1, memory_order_relaxed);
    return previous > 0 ? previous - 1 : 0;
}

#pragma mark - Lifecycle

static OSStatus OpenFaders_Initialize(AudioServerPlugInDriverRef inDriver,
                                      AudioServerPlugInHostRef inHost) {
    (void)inDriver;
    gOpenFadersDriver.host = inHost;

    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    double ticksPerSecond = 1.0e9 * (double)timebase.denom / (double)timebase.numer;
    gOpenFadersDriver.hostTicksPerFrame = ticksPerSecond / kOpenFadersNominalSampleRate;

    gOpenFadersDriver.deviceCount = kOpenFadersDeviceCount;
    for (uint32_t index = 0; index < gOpenFadersDriver.deviceCount; ++index) {
        OpenFadersVirtualDevice *device = &gOpenFadersDriver.devices[index];
        device->deviceObjectID =
            kOpenFadersDeviceBaseObjectID + index * kOpenFadersObjectIDStride + 0;
        device->outputStreamObjectID =
            kOpenFadersDeviceBaseObjectID + index * kOpenFadersObjectIDStride + 1;
        device->inputStreamObjectID =
            kOpenFadersDeviceBaseObjectID + index * kOpenFadersObjectIDStride + 2;
        strlcpy(device->uid, kOpenFadersDefaultDevices[index].uid, sizeof(device->uid));
        strlcpy(device->name, kOpenFadersDefaultDevices[index].name, sizeof(device->name));

        atomic_store_explicit(&device->ioRunningClients, 0, memory_order_relaxed);
        atomic_store_explicit(&device->isRunning, false, memory_order_relaxed);
        atomic_store_explicit(&device->anchorHostTime, 0, memory_order_relaxed);
        atomic_store_explicit(&device->anchorSampleFrames, 0, memory_order_relaxed);
        atomic_store_explicit(&device->timelineSeed, 0, memory_order_relaxed);
        device->anchorHostTimeFraction = 0.0;
        atomic_store_explicit(&device->rateScalar, 0.0, memory_order_relaxed);
        memset(device->ringSamples, 0, sizeof(device->ringSamples));
    }

    gOpenFadersDriver.initialized = true;
    return kAudioHardwareNoError;
}

static OSStatus OpenFaders_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription,
                                        const AudioServerPlugInClientInfo *inClientInfo,
                                        AudioObjectID *outDeviceObjectID) {
    (void)inDriver;
    (void)inDescription;
    (void)inClientInfo;
    (void)outDeviceObjectID;
    // Our devices are static, published through kAudioPlugInPropertyDeviceList.
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus OpenFaders_DestroyDevice(AudioServerPlugInDriverRef inDriver,
                                         AudioObjectID inDeviceObjectID) {
    (void)inDriver;
    (void)inDeviceObjectID;
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus OpenFaders_AddDeviceClient(AudioServerPlugInDriverRef inDriver,
                                           AudioObjectID inDeviceObjectID,
                                           const AudioServerPlugInClientInfo *inClientInfo) {
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inClientInfo;
    return kAudioHardwareNoError;
}

static OSStatus OpenFaders_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver,
                                              AudioObjectID inDeviceObjectID,
                                              const AudioServerPlugInClientInfo *inClientInfo) {
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inClientInfo;
    return kAudioHardwareNoError;
}

static OSStatus OpenFaders_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver,
                                                            AudioObjectID inDeviceObjectID,
                                                            UInt64 inChangeAction,
                                                            void *inChangeInfo) {
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inChangeAction;
    (void)inChangeInfo;
    // Phase 1 has no runtime configuration changes.
    return kAudioHardwareNoError;
}

static OSStatus OpenFaders_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver,
                                                          AudioObjectID inDeviceObjectID,
                                                          UInt64 inChangeAction, void *inChangeInfo) {
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inChangeAction;
    (void)inChangeInfo;
    return kAudioHardwareNoError;
}

#pragma mark - IO start/stop

static OSStatus OpenFaders_StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                                   UInt32 inClientID) {
    (void)inDriver;
    (void)inClientID;
    OpenFadersVirtualDevice *device = OpenFadersDeviceForObjectID(inDeviceObjectID);
    if (device == NULL) {
        return kAudioHardwareBadObjectError;
    }
    uint32_t previous = atomic_fetch_add_explicit(&device->ioRunningClients, 1, memory_order_acq_rel);
    if (previous == 0) {
        memset(device->ringSamples, 0, sizeof(device->ringSamples));
        OpenFadersAnchorTimeline(device);
        atomic_store_explicit(&device->isRunning, true, memory_order_release);
    }
    return kAudioHardwareNoError;
}

static OSStatus OpenFaders_StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                                  UInt32 inClientID) {
    (void)inDriver;
    (void)inClientID;
    OpenFadersVirtualDevice *device = OpenFadersDeviceForObjectID(inDeviceObjectID);
    if (device == NULL) {
        return kAudioHardwareBadObjectError;
    }
    uint32_t previous = atomic_fetch_sub_explicit(&device->ioRunningClients, 1, memory_order_acq_rel);
    if (previous == 1) {
        atomic_store_explicit(&device->isRunning, false, memory_order_release);
    }
    return kAudioHardwareNoError;
}

#pragma mark - Real-time IO

static OSStatus OpenFaders_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver,
                                            AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                            Float64 *outSampleTime, UInt64 *outHostTime,
                                            UInt64 *outSeed) {
    (void)inDriver;
    (void)inClientID;
    OpenFadersVirtualDevice *device = OpenFadersDeviceForObjectID(inDeviceObjectID);
    if (device == NULL) {
        return kAudioHardwareBadObjectError;
    }

    uint64_t anchorHost = atomic_load_explicit(&device->anchorHostTime, memory_order_acquire);
    uint64_t anchorFrames = atomic_load_explicit(&device->anchorSampleFrames, memory_order_relaxed);
    uint64_t seed = atomic_load_explicit(&device->timelineSeed, memory_order_acquire);
    double rateScalar = atomic_load_explicit(&device->rateScalar, memory_order_relaxed);

    // A positive scalar makes the clock run faster: fewer host ticks per frame.
    double ticksPerFrame = gOpenFadersDriver.hostTicksPerFrame / (1.0 + rateScalar);
    double ticksPerPeriod = ticksPerFrame * (double)kOpenFadersZeroTimeStampPeriodFrames;

    // Advance the anchor forward by whole periods only, so the reported sample
    // time is monotonic by construction even when rateScalar is nudged later.
    uint64_t now = mach_absolute_time();
    double exactAnchorHost = (double)anchorHost + device->anchorHostTimeFraction;
    double elapsedTicks = (double)now - exactAnchorHost;
    if (elapsedTicks >= ticksPerPeriod) {
        double periodsElapsed = floor(elapsedTicks / ticksPerPeriod);
        exactAnchorHost += periodsElapsed * ticksPerPeriod;
        anchorFrames += (uint64_t)periodsElapsed * kOpenFadersZeroTimeStampPeriodFrames;
        anchorHost = (uint64_t)exactAnchorHost;
        device->anchorHostTimeFraction = exactAnchorHost - (double)anchorHost;
        atomic_store_explicit(&device->anchorSampleFrames, anchorFrames, memory_order_relaxed);
        atomic_store_explicit(&device->anchorHostTime, anchorHost, memory_order_relaxed);
    }

    *outSampleTime = (Float64)anchorFrames;
    *outHostTime = anchorHost;
    *outSeed = seed;
    return kAudioHardwareNoError;
}

static OSStatus OpenFaders_WillDoIOOperation(AudioServerPlugInDriverRef inDriver,
                                             AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                             UInt32 inOperationID, Boolean *outWillDo,
                                             Boolean *outWillDoInPlace) {
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inClientID;
    Boolean willDo = false;
    switch (inOperationID) {
        case kAudioServerPlugInIOOperationWriteMix:
        case kAudioServerPlugInIOOperationReadInput:
            willDo = true;
            break;
        default:
            willDo = false;
            break;
    }
    if (outWillDo != NULL) {
        *outWillDo = willDo;
    }
    if (outWillDoInPlace != NULL) {
        *outWillDoInPlace = true;
    }
    return kAudioHardwareNoError;
}

static OSStatus OpenFaders_BeginIOOperation(AudioServerPlugInDriverRef inDriver,
                                            AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                            UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                                            const AudioServerPlugInIOCycleInfo *inIOCycleInfo) {
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inClientID;
    (void)inOperationID;
    (void)inIOBufferFrameSize;
    (void)inIOCycleInfo;
    return kAudioHardwareNoError;
}

static OSStatus OpenFaders_DoIOOperation(AudioServerPlugInDriverRef inDriver,
                                         AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID,
                                         UInt32 inClientID, UInt32 inOperationID,
                                         UInt32 inIOBufferFrameSize,
                                         const AudioServerPlugInIOCycleInfo *inIOCycleInfo,
                                         void *ioMainBuffer, void *ioSecondaryBuffer) {
    (void)inDriver;
    (void)inStreamObjectID;
    (void)inClientID;
    (void)ioSecondaryBuffer;
    OpenFadersVirtualDevice *device = OpenFadersDeviceForObjectID(inDeviceObjectID);
    if (device == NULL) {
        return kAudioHardwareBadObjectError;
    }

    switch (inOperationID) {
        case kAudioServerPlugInIOOperationWriteMix: {
            const float *applicationAudio = (const float *)ioMainBuffer;
            uint64_t startSampleFrame = (uint64_t)inIOCycleInfo->mOutputTime.mSampleTime;
            OpenFadersRingWrite(device, applicationAudio, startSampleFrame, inIOBufferFrameSize);
            break;
        }
        case kAudioServerPlugInIOOperationReadInput: {
            float *inputClientBuffer = (float *)ioMainBuffer;
            uint64_t startSampleFrame = (uint64_t)inIOCycleInfo->mInputTime.mSampleTime;
            OpenFadersRingRead(device, inputClientBuffer, startSampleFrame, inIOBufferFrameSize);
            break;
        }
        default:
            break;
    }
    return kAudioHardwareNoError;
}

static OSStatus OpenFaders_EndIOOperation(AudioServerPlugInDriverRef inDriver,
                                          AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                          UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                                          const AudioServerPlugInIOCycleInfo *inIOCycleInfo) {
    (void)inDriver;
    (void)inDeviceObjectID;
    (void)inClientID;
    (void)inOperationID;
    (void)inIOBufferFrameSize;
    (void)inIOCycleInfo;
    return kAudioHardwareNoError;
}

#pragma mark - Interface and factory

static AudioServerPlugInDriverInterface gOpenFadersInterface = {
    .QueryInterface = OpenFaders_QueryInterface,
    .AddRef = OpenFaders_AddRef,
    .Release = OpenFaders_Release,
    .Initialize = OpenFaders_Initialize,
    .CreateDevice = OpenFaders_CreateDevice,
    .DestroyDevice = OpenFaders_DestroyDevice,
    .AddDeviceClient = OpenFaders_AddDeviceClient,
    .RemoveDeviceClient = OpenFaders_RemoveDeviceClient,
    .PerformDeviceConfigurationChange = OpenFaders_PerformDeviceConfigurationChange,
    .AbortDeviceConfigurationChange = OpenFaders_AbortDeviceConfigurationChange,
    .HasProperty = OpenFaders_HasProperty,
    .IsPropertySettable = OpenFaders_IsPropertySettable,
    .GetPropertyDataSize = OpenFaders_GetPropertyDataSize,
    .GetPropertyData = OpenFaders_GetPropertyData,
    .SetPropertyData = OpenFaders_SetPropertyData,
    .StartIO = OpenFaders_StartIO,
    .StopIO = OpenFaders_StopIO,
    .GetZeroTimeStamp = OpenFaders_GetZeroTimeStamp,
    .WillDoIOOperation = OpenFaders_WillDoIOOperation,
    .BeginIOOperation = OpenFaders_BeginIOOperation,
    .DoIOOperation = OpenFaders_DoIOOperation,
    .EndIOOperation = OpenFaders_EndIOOperation,
};

static AudioServerPlugInDriverInterface *gOpenFadersInterfacePtr = &gOpenFadersInterface;

// The factory named in Info.plist. The HAL calls it with the requested plug-in
// type; we return our interface (as an AudioServerPlugInDriverRef, which is a
// pointer to the vtable pointer) when the type matches.
void *OpenFadersDriver_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID);
void *OpenFadersDriver_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID) {
    (void)inAllocator;
    if (!CFEqual(inRequestedTypeUUID, kAudioServerPlugInTypeUUID)) {
        return NULL;
    }
    return &gOpenFadersInterfacePtr;
}
