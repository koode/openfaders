// Real-time loopback ring operations.
//
// The ring is a plain interleaved-stereo float buffer indexed by absolute IO
// cycle sample time modulo its capacity. The output IO thread writes at the
// cycle's output sample time; the input IO thread reads at the cycle's input
// sample time. The HAL schedules input to lag output by the device's reported
// latency and safety offset, so a read for a given sample time always sees data
// a write already placed there. This is the timing model the design review
// verified against BlackHole, and it replaces any decoupled read/write pointers.
//
// Both functions are wait-free: no allocation, no locks, no logging. They are
// the only code that touches the ring on the real-time threads.

#ifndef OPENFADERS_LOOPBACK_RING_H
#define OPENFADERS_LOOPBACK_RING_H

#include "OpenFadersDriver.h"

// Writes frameCount interleaved-stereo frames into the ring, overwriting the
// slots for [startSampleFrame, startSampleFrame + frameCount). Overwriting (not
// mixing) is correct because the HAL already sums every application playing to
// the device into this single stereo buffer before calling us.
static inline void OpenFadersRingWrite(OpenFadersVirtualDevice *device,
                                       const float *sourceInterleaved,
                                       uint64_t startSampleFrame, uint32_t frameCount) {
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        uint64_t slot = (startSampleFrame + frame) & kOpenFadersRingCapacityMask;
        device->ringSamples[slot * kOpenFadersChannelsPerFrame + 0] =
            sourceInterleaved[frame * kOpenFadersChannelsPerFrame + 0];
        device->ringSamples[slot * kOpenFadersChannelsPerFrame + 1] =
            sourceInterleaved[frame * kOpenFadersChannelsPerFrame + 1];
    }
    // Publish the written samples to the consumer thread.
    atomic_thread_fence(memory_order_release);
}

// Reads frameCount interleaved-stereo frames from the ring for
// [startSampleFrame, startSampleFrame + frameCount) into destInterleaved. When
// OPENFADERS_CLEAR_RING_ON_READ is set, the consumed slots are zeroed so that a
// stopped output produces silence instead of replaying stale audio.
static inline void OpenFadersRingRead(OpenFadersVirtualDevice *device, float *destInterleaved,
                                      uint64_t startSampleFrame, uint32_t frameCount) {
    // Observe the producer thread's most recent writes.
    atomic_thread_fence(memory_order_acquire);
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        uint64_t slot = (startSampleFrame + frame) & kOpenFadersRingCapacityMask;
        destInterleaved[frame * kOpenFadersChannelsPerFrame + 0] =
            device->ringSamples[slot * kOpenFadersChannelsPerFrame + 0];
        destInterleaved[frame * kOpenFadersChannelsPerFrame + 1] =
            device->ringSamples[slot * kOpenFadersChannelsPerFrame + 1];
#if OPENFADERS_CLEAR_RING_ON_READ
        device->ringSamples[slot * kOpenFadersChannelsPerFrame + 0] = 0.0f;
        device->ringSamples[slot * kOpenFadersChannelsPerFrame + 1] = 0.0f;
#endif
    }
}

#endif // OPENFADERS_LOOPBACK_RING_H
