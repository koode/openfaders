// Cross-clock single-producer/single-consumer ring for the routing engine.
//
// Unlike the driver's loopback ring (indexed by a single device timeline), this
// ring genuinely spans two independent clocks: the producer is a virtual
// device's input callback (driven by the virtual device's clock) and the
// consumer is the hardware output callback (driven by the RODECaster's clock).
// The slow fill drift between those clocks is what Phase 2's clock sync will
// cancel; Phase 1 simply carries enough headroom to prove audio flows.
//
// Both operations are wait-free: monotonically increasing 64-bit indices masked
// on access, published and observed with release/acquire ordering. Overruns drop
// the newest frames; underruns emit silence, so a callback never blocks.

#ifndef OPENFADERS_ROUTE_RING_H
#define OPENFADERS_ROUTE_RING_H

#include <stdatomic.h>
#include <stdint.h>

enum {
    kRouteRingChannels = 2,
    kRouteRingCapacityFrames = 1u << 13, // 8192 frames, ~170 ms of headroom
    kRouteRingCapacityMask = kRouteRingCapacityFrames - 1u,
};

typedef struct {
    float samples[kRouteRingCapacityFrames * kRouteRingChannels]; // interleaved L,R
    _Atomic uint64_t writeIndexFrames;
    _Atomic uint64_t readIndexFrames;
} RouteRing;

static inline void RouteRingReset(RouteRing *ring) {
    for (uint32_t index = 0; index < kRouteRingCapacityFrames * kRouteRingChannels; ++index) {
        ring->samples[index] = 0.0f;
    }
    atomic_store_explicit(&ring->writeIndexFrames, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->readIndexFrames, 0, memory_order_relaxed);
}

// Current number of buffered frames. Safe to call from either thread.
static inline uint64_t RouteRingFillFrames(const RouteRing *ring) {
    uint64_t writeIndex = atomic_load_explicit(&ring->writeIndexFrames, memory_order_acquire);
    uint64_t readIndex = atomic_load_explicit(&ring->readIndexFrames, memory_order_acquire);
    return writeIndex - readIndex;
}

// Producer: writes interleaved stereo frames, dropping the newest on overrun.
static inline void RouteRingWrite(RouteRing *ring, const float *sourceInterleaved,
                                  uint32_t frameCount) {
    uint64_t writeIndex = atomic_load_explicit(&ring->writeIndexFrames, memory_order_relaxed);
    uint64_t readIndex = atomic_load_explicit(&ring->readIndexFrames, memory_order_acquire);
    uint64_t freeFrames = kRouteRingCapacityFrames - (writeIndex - readIndex);
    uint32_t framesToWrite = frameCount <= freeFrames ? frameCount : (uint32_t)freeFrames;

    for (uint32_t frame = 0; frame < framesToWrite; ++frame) {
        uint64_t slot = (writeIndex + frame) & kRouteRingCapacityMask;
        ring->samples[slot * kRouteRingChannels + 0] = sourceInterleaved[frame * kRouteRingChannels + 0];
        ring->samples[slot * kRouteRingChannels + 1] = sourceInterleaved[frame * kRouteRingChannels + 1];
    }
    atomic_store_explicit(&ring->writeIndexFrames, writeIndex + framesToWrite, memory_order_release);
}

// Consumer: reads interleaved stereo frames, padding with silence on underrun so
// the caller always receives a full buffer.
static inline void RouteRingRead(RouteRing *ring, float *destinationInterleaved,
                                 uint32_t frameCount) {
    uint64_t readIndex = atomic_load_explicit(&ring->readIndexFrames, memory_order_relaxed);
    uint64_t writeIndex = atomic_load_explicit(&ring->writeIndexFrames, memory_order_acquire);
    uint64_t availableFrames = writeIndex - readIndex;
    uint32_t framesToRead = frameCount <= availableFrames ? frameCount : (uint32_t)availableFrames;

    for (uint32_t frame = 0; frame < framesToRead; ++frame) {
        uint64_t slot = (readIndex + frame) & kRouteRingCapacityMask;
        destinationInterleaved[frame * kRouteRingChannels + 0] = ring->samples[slot * kRouteRingChannels + 0];
        destinationInterleaved[frame * kRouteRingChannels + 1] = ring->samples[slot * kRouteRingChannels + 1];
    }
    for (uint32_t frame = framesToRead; frame < frameCount; ++frame) {
        destinationInterleaved[frame * kRouteRingChannels + 0] = 0.0f;
        destinationInterleaved[frame * kRouteRingChannels + 1] = 0.0f;
    }
    atomic_store_explicit(&ring->readIndexFrames, readIndex + framesToRead, memory_order_release);
}

#endif // OPENFADERS_ROUTE_RING_H
