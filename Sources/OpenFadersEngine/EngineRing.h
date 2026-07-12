// Cross-clock single-producer/single-consumer ring for the routing engine.
//
// Unlike the driver's loopback ring (indexed by a single device timeline), this
// ring genuinely spans two independent clocks: the producer is a virtual
// device's input callback (driven by the virtual device's clock) and the
// consumer is the hardware output callback (driven by the RODECaster's clock).
// The slow fill drift between those clocks is what the adaptive sync mode
// cancels by nudging the virtual device's rate; this ring carries the buffered
// audio and exposes its fill level as the control loop's error signal.
//
// Both operations are wait-free: monotonically increasing 64-bit indices masked
// on access, published and observed with release/acquire ordering. Overruns drop
// the newest frames; underruns emit silence, so a callback never blocks. Dropped
// and padded frames are counted so the control thread can surface xruns.

#ifndef OPENFADERS_ENGINE_RING_H
#define OPENFADERS_ENGINE_RING_H

#include <stdatomic.h>
#include <stdint.h>

enum {
    kEngineRingChannels = 2,
    kEngineRingCapacityFrames = 1u << 13, // 8192 frames, ~170 ms of headroom at 48 kHz
    kEngineRingCapacityMask = kEngineRingCapacityFrames - 1u,
};

typedef struct {
    float samples[kEngineRingCapacityFrames * kEngineRingChannels]; // interleaved L,R
    _Atomic uint64_t writeIndexFrames;
    _Atomic uint64_t readIndexFrames;
    _Atomic uint64_t overrunFrames;  // producer frames dropped because the ring was full
    _Atomic uint64_t underrunFrames; // consumer frames padded with silence on empty
} EngineRing;

static inline void EngineRingReset(EngineRing *ring) {
    for (uint32_t index = 0; index < kEngineRingCapacityFrames * kEngineRingChannels; ++index) {
        ring->samples[index] = 0.0f;
    }
    atomic_store_explicit(&ring->writeIndexFrames, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->readIndexFrames, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->overrunFrames, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->underrunFrames, 0, memory_order_relaxed);
}

// Current number of buffered frames. Safe to call from either thread; this is the
// integrated phase error between the two clocks that the control loop drives to a
// constant target.
static inline uint64_t EngineRingFillFrames(const EngineRing *ring) {
    uint64_t writeIndex = atomic_load_explicit(&ring->writeIndexFrames, memory_order_acquire);
    uint64_t readIndex = atomic_load_explicit(&ring->readIndexFrames, memory_order_acquire);
    return writeIndex - readIndex;
}

// Producer: writes interleaved stereo frames, dropping the newest on overrun.
static inline void EngineRingWrite(EngineRing *ring, const float *sourceInterleaved,
                                   uint32_t frameCount) {
    uint64_t writeIndex = atomic_load_explicit(&ring->writeIndexFrames, memory_order_relaxed);
    uint64_t readIndex = atomic_load_explicit(&ring->readIndexFrames, memory_order_acquire);
    uint64_t freeFrames = kEngineRingCapacityFrames - (writeIndex - readIndex);
    uint32_t framesToWrite = frameCount <= freeFrames ? frameCount : (uint32_t)freeFrames;

    for (uint32_t frame = 0; frame < framesToWrite; ++frame) {
        uint64_t slot = (writeIndex + frame) & kEngineRingCapacityMask;
        ring->samples[slot * kEngineRingChannels + 0] = sourceInterleaved[frame * kEngineRingChannels + 0];
        ring->samples[slot * kEngineRingChannels + 1] = sourceInterleaved[frame * kEngineRingChannels + 1];
    }
    atomic_store_explicit(&ring->writeIndexFrames, writeIndex + framesToWrite, memory_order_release);

    if (framesToWrite < frameCount) {
        atomic_fetch_add_explicit(&ring->overrunFrames, frameCount - framesToWrite, memory_order_relaxed);
    }
}

// Consumer: reads interleaved stereo frames, padding with silence on underrun so
// the caller always receives a full buffer.
static inline void EngineRingRead(EngineRing *ring, float *destinationInterleaved,
                                  uint32_t frameCount) {
    uint64_t readIndex = atomic_load_explicit(&ring->readIndexFrames, memory_order_relaxed);
    uint64_t writeIndex = atomic_load_explicit(&ring->writeIndexFrames, memory_order_acquire);
    uint64_t availableFrames = writeIndex - readIndex;
    uint32_t framesToRead = frameCount <= availableFrames ? frameCount : (uint32_t)availableFrames;

    for (uint32_t frame = 0; frame < framesToRead; ++frame) {
        uint64_t slot = (readIndex + frame) & kEngineRingCapacityMask;
        destinationInterleaved[frame * kEngineRingChannels + 0] = ring->samples[slot * kEngineRingChannels + 0];
        destinationInterleaved[frame * kEngineRingChannels + 1] = ring->samples[slot * kEngineRingChannels + 1];
    }
    for (uint32_t frame = framesToRead; frame < frameCount; ++frame) {
        destinationInterleaved[frame * kEngineRingChannels + 0] = 0.0f;
        destinationInterleaved[frame * kEngineRingChannels + 1] = 0.0f;
    }
    atomic_store_explicit(&ring->readIndexFrames, readIndex + framesToRead, memory_order_release);

    if (framesToRead < frameCount) {
        atomic_fetch_add_explicit(&ring->underrunFrames, frameCount - framesToRead, memory_order_relaxed);
    }
}

#endif // OPENFADERS_ENGINE_RING_H
