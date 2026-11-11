// SAI2 audio: Block A is the master transmitter (MCLK + I2S data to the
// on-board codec), Block B a synchronous slave receiver (the codec's ADC:
// the analogue line / mic input, used as the modular clock input). Both run
// off one DMA double buffer each and share the same frame clock, so every
// render callback gets the input block that was captured alongside.
//
// The DMA cycles continuously over a buffer split into two halves. When the
// DMA finishes the first half (and is now reading the second), it interrupts
// us to refill the first; at end-of-buffer it wraps and interrupts us to
// refill the second. So at ~48 kHz we get a steady stream of "render the next
// `kBlockSize` frames" callbacks with no gaps and no allocation.
#pragma once

#include <cstdint>

namespace audio_out
{

inline constexpr uint32_t kSampleRate = 48000;  // Hz
inline constexpr uint32_t kBlockSize = 64;      // stereo frames per render call

// Fill `frames` interleaved stereo frames (L,R,L,R,...) of signed 16-bit PCM
// into `out`; `in` holds the same number of input frames (the most recent
// complete capture block, at most one block old). Runs in DMA interrupt
// context: no blocking, no allocation. Normally one block (kBlockSize
// frames), but start() pre-fills the whole buffer in one call.
using RenderFn = void (*)(int16_t* out, const int16_t* in, uint32_t frames);

// Configure PLL2 for an accurate 48 kHz clock, bring up SAI2 Block A as a TX
// master (MCLK on PI4) and Block B as its synchronous RX slave (data in on
// PG10). After this returns MCLK is running, which the codec needs before it
// can be initialised. Returns false on HAL failure.
bool init();

// Start streaming in and out. `render` is invoked once per half-buffer from
// the TX DMA ISR.
void start(RenderFn render);

// Stop both DMA streams.
void stop();

// Render callbacks since start (the real sample rate is blocks() *
// kBlockSize per second).
uint32_t blocks();

}  // namespace audio_out
