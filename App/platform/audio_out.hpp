// SAI2 audio output: master clock + I2S data stream to the on-board codec,
// fed by a DMA double buffer.
//
// The DMA cycles continuously over a buffer split into two halves. When the
// DMA finishes the first half (and is now reading the second), it interrupts
// us to refill the first; at end-of-buffer it wraps and interrupts us to
// refill the second. So at ~48 kHz we get a steady stream of "render the next
// `kBlockSize` frames" callbacks with no gaps and no allocation.
#pragma once

#include <cstdint>

namespace audio_out {

inline constexpr uint32_t kSampleRate = 48000;  // Hz
inline constexpr uint32_t kBlockSize = 64;      // stereo frames per render call

// Fill `frames` interleaved stereo frames (L,R,L,R,...) of signed 16-bit PCM
// into `out`. Runs in DMA interrupt context: no blocking, no allocation.
using RenderFn = void (*)(int16_t* out, uint32_t frames);

// Configure PLL2 for an accurate 48 kHz clock and bring up SAI2 Block A as a
// TX master (MCLK on PI4). After this returns MCLK is running, which the codec
// needs before it can be initialised. Returns false on HAL failure.
bool init();

// Start streaming. `render` is invoked once per half-buffer from the DMA ISR.
void start(RenderFn render);

// Stop the DMA stream.
void stop();

}  // namespace audio_out
