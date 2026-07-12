// OpenFaders virtual audio driver: the Core Audio property model.
//
// Every AudioObject the driver vends (the plug-in, each device, each stream)
// answers a set of property queries. The four vtable entry points dispatch on
// the object's class and then on the property selector. All of this runs on the
// HAL's control thread, never the real-time IO threads, so it may allocate and
// create CoreFoundation objects.

#include "OpenFadersDriver.h"

#include <CoreFoundation/CoreFoundation.h>
#include <os/log.h>
#include <string.h>

// Manufacturer and model strings shared by every object.
#define kOpenFadersManufacturerName "OpenFaders"
#define kOpenFadersModelUID "app.openfaders.model"

// When enabled, property queries that return an error are logged with their
// four-character selector and scope. This is how we find the property that makes
// coreaudiod reject a device during registration. It runs on the control thread
// only, never a real-time path. Turn off for release builds.
#define OPENFADERS_DEBUG_LOG 1

#if OPENFADERS_DEBUG_LOG
static void OpenFadersLogPropertyFailure(const char *phase, AudioObjectID objectID,
                                         const AudioObjectPropertyAddress *address, OSStatus status) {
    uint32_t selector = address->mSelector;
    uint32_t scope = address->mScope;
    os_log(OS_LOG_DEFAULT,
           "OpenFaders %{public}s FAIL object=%u selector=%{public}c%{public}c%{public}c%{public}c "
           "scope=%{public}c%{public}c%{public}c%{public}c status=%d",
           phase, objectID, (char)(selector >> 24), (char)(selector >> 16), (char)(selector >> 8),
           (char)selector, (char)(scope >> 24), (char)(scope >> 16), (char)(scope >> 8), (char)scope,
           (int)status);
}
#define OPENFADERS_LOG_FAILURE(phase, objectID, address, status)                                    \
    OpenFadersLogPropertyFailure((phase), (objectID), (address), (status))
#else
#define OPENFADERS_LOG_FAILURE(phase, objectID, address, status) ((void)0)
#endif

#pragma mark - Small writers

// Writes a fixed-size scalar and returns from the enclosing function.
#define OPENFADERS_RETURN_SCALAR(TYPE, VALUE)                    \
    do {                                                         \
        if (inDataSize < sizeof(TYPE))                           \
            return kAudioHardwareBadPropertySizeError;           \
        *((TYPE *)outData) = (VALUE);                            \
        *outDataSize = (UInt32)sizeof(TYPE);                     \
        return kAudioHardwareNoError;                            \
    } while (0)

// Writes as many AudioObjectIDs as fit in the caller's buffer, reporting how
// many bytes were actually written (the HAL may ask for fewer than all).
static OSStatus OpenFadersWriteObjectIDList(const AudioObjectID *ids, uint32_t count,
                                            UInt32 inDataSize, UInt32 *outDataSize, void *outData) {
    uint32_t capacity = inDataSize / (UInt32)sizeof(AudioObjectID);
    uint32_t itemsToWrite = count < capacity ? count : capacity;
    if (itemsToWrite > 0) {
        memcpy(outData, ids, itemsToWrite * sizeof(AudioObjectID));
    }
    *outDataSize = itemsToWrite * (UInt32)sizeof(AudioObjectID);
    return kAudioHardwareNoError;
}

// Creates a CFString from a C string and hands ownership to the caller, which
// the HAL releases. Used for name/UID/manufacturer properties.
static OSStatus OpenFadersWriteCFString(const char *value, UInt32 inDataSize, UInt32 *outDataSize,
                                        void *outData) {
    if (inDataSize < sizeof(CFStringRef)) {
        return kAudioHardwareBadPropertySizeError;
    }
    *((CFStringRef *)outData) =
        CFStringCreateWithCString(kCFAllocatorDefault, value, kCFStringEncodingUTF8);
    *outDataSize = (UInt32)sizeof(CFStringRef);
    return kAudioHardwareNoError;
}

// Returns the list of device object IDs, used for both the plug-in's device list
// and its owned-objects list.
static uint32_t OpenFadersCollectDeviceIDs(AudioObjectID *outIDs) {
    for (uint32_t index = 0; index < gOpenFadersDriver.deviceCount; ++index) {
        outIDs[index] = gOpenFadersDriver.devices[index].deviceObjectID;
    }
    return gOpenFadersDriver.deviceCount;
}

#pragma mark - Plug-in object

static Boolean OpenFadersPlugIn_HasProperty(const AudioObjectPropertyAddress *address) {
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList:
        case kAudioPlugInPropertyTranslateUIDToDevice:
        case kAudioPlugInPropertyResourceBundle:
            return true;
        default:
            return false;
    }
}

static OSStatus OpenFadersPlugIn_GetPropertyDataSize(const AudioObjectPropertyAddress *address,
                                                     UInt32 *outDataSize) {
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
            *outDataSize = (UInt32)sizeof(AudioClassID);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:
        case kAudioPlugInPropertyTranslateUIDToDevice:
            *outDataSize = (UInt32)sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyName:
        case kAudioPlugInPropertyResourceBundle:
            *outDataSize = (UInt32)sizeof(CFStringRef);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList:
            *outDataSize = gOpenFadersDriver.deviceCount * (UInt32)sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus OpenFadersPlugIn_GetPropertyData(const AudioObjectPropertyAddress *address,
                                                 UInt32 inQualifierDataSize,
                                                 const void *inQualifierData, UInt32 inDataSize,
                                                 UInt32 *outDataSize, void *outData) {
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
            OPENFADERS_RETURN_SCALAR(AudioClassID, kAudioObjectClassID);
        case kAudioObjectPropertyClass:
            OPENFADERS_RETURN_SCALAR(AudioClassID, kAudioPlugInClassID);
        case kAudioObjectPropertyOwner:
            OPENFADERS_RETURN_SCALAR(AudioObjectID, kAudioObjectUnknown);
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyName:
            return OpenFadersWriteCFString(kOpenFadersManufacturerName, inDataSize, outDataSize, outData);
        case kAudioPlugInPropertyResourceBundle:
            return OpenFadersWriteCFString("", inDataSize, outDataSize, outData);
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList: {
            AudioObjectID ids[kOpenFadersMaxDevices];
            uint32_t count = OpenFadersCollectDeviceIDs(ids);
            return OpenFadersWriteObjectIDList(ids, count, inDataSize, outDataSize, outData);
        }
        case kAudioPlugInPropertyTranslateUIDToDevice: {
            if (inQualifierDataSize < sizeof(CFStringRef) || inQualifierData == NULL) {
                return kAudioHardwareIllegalOperationError;
            }
            CFStringRef requestedUID = *((const CFStringRef *)inQualifierData);
            AudioObjectID matchedDevice = kAudioObjectUnknown;
            for (uint32_t index = 0; index < gOpenFadersDriver.deviceCount; ++index) {
                CFStringRef deviceUID = CFStringCreateWithCString(
                    kCFAllocatorDefault, gOpenFadersDriver.devices[index].uid, kCFStringEncodingUTF8);
                if (deviceUID != NULL && CFEqual(deviceUID, requestedUID)) {
                    matchedDevice = gOpenFadersDriver.devices[index].deviceObjectID;
                }
                if (deviceUID != NULL) {
                    CFRelease(deviceUID);
                }
            }
            OPENFADERS_RETURN_SCALAR(AudioObjectID, matchedDevice);
        }
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

#pragma mark - Device object

static Boolean OpenFadersDevice_HasProperty(const AudioObjectPropertyAddress *address) {
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioObjectPropertyControlList:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyStreams:
        case kAudioDevicePropertyStreamConfiguration:
        case kAudioDevicePropertyNominalSampleRate:
        case kAudioDevicePropertyAvailableNominalSampleRates:
        case kAudioDevicePropertyPreferredChannelsForStereo:
        case kAudioDevicePropertyPreferredChannelLayout:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertyZeroTimeStampPeriod:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyRelatedDevices:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsRunningSomewhere:
            return true;
        default:
            return false;
    }
}

// Number of streams a device reports for a given scope: input scope has the
// mirrored input stream, output scope has the output stream, global scope has
// both.
static uint32_t OpenFadersDeviceStreamIDs(const OpenFadersVirtualDevice *device,
                                          AudioObjectPropertyScope scope, AudioObjectID *outIDs) {
    if (scope == kAudioObjectPropertyScopeInput) {
        outIDs[0] = device->inputStreamObjectID;
        return 1;
    }
    if (scope == kAudioObjectPropertyScopeOutput) {
        outIDs[0] = device->outputStreamObjectID;
        return 1;
    }
    outIDs[0] = device->outputStreamObjectID;
    outIDs[1] = device->inputStreamObjectID;
    return 2;
}

static OSStatus OpenFadersDevice_GetPropertyDataSize(const OpenFadersVirtualDevice *device,
                                                     const AudioObjectPropertyAddress *address,
                                                     UInt32 *outDataSize) {
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
            *outDataSize = (UInt32)sizeof(AudioClassID);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:
            *outDataSize = (UInt32)sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
            *outDataSize = (UInt32)sizeof(CFStringRef);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyOwnedObjects: {
            AudioObjectID streamIDs[2];
            uint32_t count = OpenFadersDeviceStreamIDs(device, address->mScope, streamIDs);
            *outDataSize = count * (UInt32)sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioObjectPropertyControlList:
            *outDataSize = 0; // no controls in Phase 1
            return kAudioHardwareNoError;
        case kAudioDevicePropertyStreams: {
            AudioObjectID streamIDs[2];
            uint32_t count = OpenFadersDeviceStreamIDs(device, address->mScope, streamIDs);
            *outDataSize = count * (UInt32)sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertyZeroTimeStampPeriod:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsRunningSomewhere:
            *outDataSize = (UInt32)sizeof(UInt32);
            return kAudioHardwareNoError;
        case kAudioDevicePropertyRelatedDevices:
            *outDataSize = (UInt32)sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        case kAudioDevicePropertyNominalSampleRate:
            *outDataSize = (UInt32)sizeof(Float64);
            return kAudioHardwareNoError;
        case kAudioDevicePropertyAvailableNominalSampleRates:
            *outDataSize = (UInt32)sizeof(AudioValueRange);
            return kAudioHardwareNoError;
        case kAudioDevicePropertyStreamConfiguration:
            *outDataSize = (UInt32)sizeof(AudioBufferList);
            return kAudioHardwareNoError;
        case kAudioDevicePropertyPreferredChannelsForStereo:
            *outDataSize = 2 * (UInt32)sizeof(UInt32);
            return kAudioHardwareNoError;
        case kAudioDevicePropertyPreferredChannelLayout:
            *outDataSize = (UInt32)sizeof(AudioChannelLayout);
            return kAudioHardwareNoError;
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus OpenFadersDevice_GetPropertyData(const OpenFadersVirtualDevice *device,
                                                 const AudioObjectPropertyAddress *address,
                                                 UInt32 inDataSize, UInt32 *outDataSize,
                                                 void *outData) {
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
            OPENFADERS_RETURN_SCALAR(AudioClassID, kAudioObjectClassID);
        case kAudioObjectPropertyClass:
            OPENFADERS_RETURN_SCALAR(AudioClassID, kAudioDeviceClassID);
        case kAudioObjectPropertyOwner:
            OPENFADERS_RETURN_SCALAR(AudioObjectID, kAudioObjectPlugInObject);
        case kAudioObjectPropertyName:
            return OpenFadersWriteCFString(device->name, inDataSize, outDataSize, outData);
        case kAudioObjectPropertyManufacturer:
            return OpenFadersWriteCFString(kOpenFadersManufacturerName, inDataSize, outDataSize, outData);
        case kAudioDevicePropertyDeviceUID:
            return OpenFadersWriteCFString(device->uid, inDataSize, outDataSize, outData);
        case kAudioDevicePropertyModelUID:
            return OpenFadersWriteCFString(kOpenFadersModelUID, inDataSize, outDataSize, outData);
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyStreams: {
            AudioObjectID streamIDs[2];
            uint32_t count = OpenFadersDeviceStreamIDs(device, address->mScope, streamIDs);
            return OpenFadersWriteObjectIDList(streamIDs, count, inDataSize, outDataSize, outData);
        }
        case kAudioObjectPropertyControlList:
            *outDataSize = 0;
            return kAudioHardwareNoError;
        case kAudioDevicePropertyTransportType:
            OPENFADERS_RETURN_SCALAR(UInt32, kAudioDeviceTransportTypeVirtual);
        case kAudioDevicePropertyDeviceIsAlive:
            OPENFADERS_RETURN_SCALAR(UInt32, 1);
        case kAudioDevicePropertyDeviceIsRunning:
            OPENFADERS_RETURN_SCALAR(UInt32, atomic_load_explicit(&device->isRunning,
                                                                  memory_order_acquire) ? 1 : 0);
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
            OPENFADERS_RETURN_SCALAR(UInt32, 1);
        case kAudioDevicePropertySafetyOffset:
            OPENFADERS_RETURN_SCALAR(UInt32, kOpenFadersSafetyOffsetFrames);
        case kAudioDevicePropertyLatency:
            OPENFADERS_RETURN_SCALAR(UInt32, 0);
        case kAudioDevicePropertyZeroTimeStampPeriod:
            OPENFADERS_RETURN_SCALAR(UInt32, kOpenFadersZeroTimeStampPeriodFrames);
        case kAudioDevicePropertyIsHidden:
            OPENFADERS_RETURN_SCALAR(UInt32, 0);
        case kAudioDevicePropertyClockDomain:
            OPENFADERS_RETURN_SCALAR(UInt32, 0);
        case kAudioDevicePropertyDeviceIsRunningSomewhere:
            OPENFADERS_RETURN_SCALAR(UInt32, atomic_load_explicit(&device->isRunning,
                                                                  memory_order_acquire) ? 1 : 0);
        case kAudioDevicePropertyRelatedDevices: {
            AudioObjectID relatedDevices[1] = {device->deviceObjectID};
            return OpenFadersWriteObjectIDList(relatedDevices, 1, inDataSize, outDataSize, outData);
        }
        case kAudioDevicePropertyNominalSampleRate:
            OPENFADERS_RETURN_SCALAR(Float64, kOpenFadersNominalSampleRate);
        case kAudioDevicePropertyAvailableNominalSampleRates: {
            if (inDataSize < sizeof(AudioValueRange)) {
                return kAudioHardwareBadPropertySizeError;
            }
            AudioValueRange range = {kOpenFadersNominalSampleRate, kOpenFadersNominalSampleRate};
            *((AudioValueRange *)outData) = range;
            *outDataSize = (UInt32)sizeof(AudioValueRange);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyStreamConfiguration: {
            if (inDataSize < sizeof(AudioBufferList)) {
                return kAudioHardwareBadPropertySizeError;
            }
            AudioBufferList *bufferList = (AudioBufferList *)outData;
            bufferList->mNumberBuffers = 1;
            bufferList->mBuffers[0].mNumberChannels = kOpenFadersChannelsPerFrame;
            bufferList->mBuffers[0].mDataByteSize = kOpenFadersChannelsPerFrame * (UInt32)sizeof(float);
            bufferList->mBuffers[0].mData = NULL;
            *outDataSize = (UInt32)sizeof(AudioBufferList);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyPreferredChannelsForStereo: {
            if (inDataSize < 2 * sizeof(UInt32)) {
                return kAudioHardwareBadPropertySizeError;
            }
            ((UInt32 *)outData)[0] = 1;
            ((UInt32 *)outData)[1] = 2;
            *outDataSize = 2 * (UInt32)sizeof(UInt32);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyPreferredChannelLayout: {
            if (inDataSize < sizeof(AudioChannelLayout)) {
                return kAudioHardwareBadPropertySizeError;
            }
            AudioChannelLayout *layout = (AudioChannelLayout *)outData;
            memset(layout, 0, sizeof(AudioChannelLayout));
            layout->mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
            *outDataSize = (UInt32)sizeof(AudioChannelLayout);
            return kAudioHardwareNoError;
        }
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

#pragma mark - Stream object

static Boolean OpenFadersStream_HasProperty(const AudioObjectPropertyAddress *address) {
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
        case kAudioStreamPropertyLatency:
            return true;
        default:
            return false;
    }
}

static OSStatus OpenFadersStream_GetPropertyDataSize(const AudioObjectPropertyAddress *address,
                                                     UInt32 *outDataSize) {
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
            *outDataSize = (UInt32)sizeof(AudioClassID);
            return kAudioHardwareNoError;
        case kAudioObjectPropertyOwner:
            *outDataSize = (UInt32)sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyLatency:
            *outDataSize = (UInt32)sizeof(UInt32);
            return kAudioHardwareNoError;
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
            *outDataSize = (UInt32)sizeof(AudioStreamBasicDescription);
            return kAudioHardwareNoError;
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            *outDataSize = (UInt32)sizeof(AudioStreamRangedDescription);
            return kAudioHardwareNoError;
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus OpenFadersStream_GetPropertyData(const OpenFadersVirtualDevice *device,
                                                 bool isInputStream,
                                                 const AudioObjectPropertyAddress *address,
                                                 UInt32 inDataSize, UInt32 *outDataSize,
                                                 void *outData) {
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
            OPENFADERS_RETURN_SCALAR(AudioClassID, kAudioObjectClassID);
        case kAudioObjectPropertyClass:
            OPENFADERS_RETURN_SCALAR(AudioClassID, kAudioStreamClassID);
        case kAudioObjectPropertyOwner:
            OPENFADERS_RETURN_SCALAR(AudioObjectID, device->deviceObjectID);
        case kAudioStreamPropertyDirection:
            // 0 means an output stream, 1 means an input stream.
            OPENFADERS_RETURN_SCALAR(UInt32, isInputStream ? 1 : 0);
        case kAudioStreamPropertyTerminalType:
            OPENFADERS_RETURN_SCALAR(UInt32, isInputStream ? kAudioStreamTerminalTypeMicrophone
                                                           : kAudioStreamTerminalTypeSpeaker);
        case kAudioStreamPropertyStartingChannel:
            OPENFADERS_RETURN_SCALAR(UInt32, 1);
        case kAudioStreamPropertyIsActive:
            OPENFADERS_RETURN_SCALAR(UInt32, 1);
        case kAudioStreamPropertyLatency:
            OPENFADERS_RETURN_SCALAR(UInt32, 0);
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat: {
            if (inDataSize < sizeof(AudioStreamBasicDescription)) {
                return kAudioHardwareBadPropertySizeError;
            }
            *((AudioStreamBasicDescription *)outData) = OpenFadersStreamFormat();
            *outDataSize = (UInt32)sizeof(AudioStreamBasicDescription);
            return kAudioHardwareNoError;
        }
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats: {
            if (inDataSize < sizeof(AudioStreamRangedDescription)) {
                return kAudioHardwareBadPropertySizeError;
            }
            AudioStreamRangedDescription description;
            memset(&description, 0, sizeof(description));
            description.mFormat = OpenFadersStreamFormat();
            description.mSampleRateRange.mMinimum = kOpenFadersNominalSampleRate;
            description.mSampleRateRange.mMaximum = kOpenFadersNominalSampleRate;
            *((AudioStreamRangedDescription *)outData) = description;
            *outDataSize = (UInt32)sizeof(AudioStreamRangedDescription);
            return kAudioHardwareNoError;
        }
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

#pragma mark - Dispatch

Boolean OpenFaders_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                               pid_t inClientProcessID, const AudioObjectPropertyAddress *inAddress) {
    (void)inDriver;
    (void)inClientProcessID;
    uint32_t deviceIndex = 0;
    switch (OpenFadersClassifyObject(inObjectID, &deviceIndex)) {
        case kOpenFadersObjectClass_PlugIn:
            return OpenFadersPlugIn_HasProperty(inAddress);
        case kOpenFadersObjectClass_Device:
            return OpenFadersDevice_HasProperty(inAddress);
        case kOpenFadersObjectClass_OutputStream:
        case kOpenFadersObjectClass_InputStream:
            return OpenFadersStream_HasProperty(inAddress);
        default:
            return false;
    }
}

OSStatus OpenFaders_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                       pid_t inClientProcessID,
                                       const AudioObjectPropertyAddress *inAddress,
                                       Boolean *outIsSettable) {
    (void)inDriver;
    (void)inClientProcessID;
    uint32_t deviceIndex = 0;
    OpenFadersObjectClass objectClass = OpenFadersClassifyObject(inObjectID, &deviceIndex);

    Boolean settable = false;
    if (objectClass == kOpenFadersObjectClass_Device) {
        settable = (inAddress->mSelector == kAudioDevicePropertyNominalSampleRate);
    } else if (objectClass == kOpenFadersObjectClass_OutputStream ||
               objectClass == kOpenFadersObjectClass_InputStream) {
        settable = (inAddress->mSelector == kAudioStreamPropertyVirtualFormat ||
                    inAddress->mSelector == kAudioStreamPropertyPhysicalFormat);
    }
    *outIsSettable = settable;
    return kAudioHardwareNoError;
}

OSStatus OpenFaders_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                        pid_t inClientProcessID,
                                        const AudioObjectPropertyAddress *inAddress,
                                        UInt32 inQualifierDataSize, const void *inQualifierData,
                                        UInt32 *outDataSize) {
    (void)inDriver;
    (void)inClientProcessID;
    (void)inQualifierDataSize;
    (void)inQualifierData;
    uint32_t deviceIndex = 0;
    OSStatus status;
    switch (OpenFadersClassifyObject(inObjectID, &deviceIndex)) {
        case kOpenFadersObjectClass_PlugIn:
            status = OpenFadersPlugIn_GetPropertyDataSize(inAddress, outDataSize);
            break;
        case kOpenFadersObjectClass_Device:
            status = OpenFadersDevice_GetPropertyDataSize(&gOpenFadersDriver.devices[deviceIndex],
                                                          inAddress, outDataSize);
            break;
        case kOpenFadersObjectClass_OutputStream:
        case kOpenFadersObjectClass_InputStream:
            status = OpenFadersStream_GetPropertyDataSize(inAddress, outDataSize);
            break;
        default:
            status = kAudioHardwareBadObjectError;
            break;
    }
    if (status != kAudioHardwareNoError) {
        OPENFADERS_LOG_FAILURE("GetPropertyDataSize", inObjectID, inAddress, status);
    }
    return status;
}

OSStatus OpenFaders_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                    pid_t inClientProcessID,
                                    const AudioObjectPropertyAddress *inAddress,
                                    UInt32 inQualifierDataSize, const void *inQualifierData,
                                    UInt32 inDataSize, UInt32 *outDataSize, void *outData) {
    (void)inDriver;
    (void)inClientProcessID;
    uint32_t deviceIndex = 0;
    OSStatus status;
    switch (OpenFadersClassifyObject(inObjectID, &deviceIndex)) {
        case kOpenFadersObjectClass_PlugIn:
            status = OpenFadersPlugIn_GetPropertyData(inAddress, inQualifierDataSize, inQualifierData,
                                                      inDataSize, outDataSize, outData);
            break;
        case kOpenFadersObjectClass_Device:
            status = OpenFadersDevice_GetPropertyData(&gOpenFadersDriver.devices[deviceIndex],
                                                      inAddress, inDataSize, outDataSize, outData);
            break;
        case kOpenFadersObjectClass_OutputStream:
            status = OpenFadersStream_GetPropertyData(&gOpenFadersDriver.devices[deviceIndex], false,
                                                      inAddress, inDataSize, outDataSize, outData);
            break;
        case kOpenFadersObjectClass_InputStream:
            status = OpenFadersStream_GetPropertyData(&gOpenFadersDriver.devices[deviceIndex], true,
                                                      inAddress, inDataSize, outDataSize, outData);
            break;
        default:
            status = kAudioHardwareBadObjectError;
            break;
    }
    if (status != kAudioHardwareNoError) {
        OPENFADERS_LOG_FAILURE("GetPropertyData", inObjectID, inAddress, status);
    }
    return status;
}

OSStatus OpenFaders_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                    pid_t inClientProcessID,
                                    const AudioObjectPropertyAddress *inAddress,
                                    UInt32 inQualifierDataSize, const void *inQualifierData,
                                    UInt32 inDataSize, const void *inData) {
    (void)inDriver;
    (void)inClientProcessID;
    (void)inQualifierDataSize;
    (void)inQualifierData;
    uint32_t deviceIndex = 0;
    OpenFadersObjectClass objectClass = OpenFadersClassifyObject(inObjectID, &deviceIndex);

    // The device advertises a single sample rate and each stream a single
    // format, so the only settable requests we expect are no-op confirmations of
    // those fixed values. Accept the canonical value, reject anything else.
    if (objectClass == kOpenFadersObjectClass_Device &&
        inAddress->mSelector == kAudioDevicePropertyNominalSampleRate) {
        if (inDataSize < sizeof(Float64)) {
            return kAudioHardwareBadPropertySizeError;
        }
        Float64 requestedRate = *((const Float64 *)inData);
        return requestedRate == kOpenFadersNominalSampleRate ? kAudioHardwareNoError
                                                             : kAudioHardwareIllegalOperationError;
    }

    if ((objectClass == kOpenFadersObjectClass_OutputStream ||
         objectClass == kOpenFadersObjectClass_InputStream) &&
        (inAddress->mSelector == kAudioStreamPropertyVirtualFormat ||
         inAddress->mSelector == kAudioStreamPropertyPhysicalFormat)) {
        if (inDataSize < sizeof(AudioStreamBasicDescription)) {
            return kAudioHardwareBadPropertySizeError;
        }
        const AudioStreamBasicDescription *requestedFormat = (const AudioStreamBasicDescription *)inData;
        AudioStreamBasicDescription canonicalFormat = OpenFadersStreamFormat();
        bool matchesCanonical = requestedFormat->mSampleRate == canonicalFormat.mSampleRate &&
                                requestedFormat->mFormatID == canonicalFormat.mFormatID &&
                                requestedFormat->mChannelsPerFrame == canonicalFormat.mChannelsPerFrame &&
                                requestedFormat->mBitsPerChannel == canonicalFormat.mBitsPerChannel;
        return matchesCanonical ? kAudioHardwareNoError : kAudioHardwareIllegalOperationError;
    }

    return kAudioHardwareUnknownPropertyError;
}
