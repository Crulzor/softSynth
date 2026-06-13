// On-board WM8994 audio codec control (over I2C4).
//
// The codec is a separate chip from the SAI: SAI carries the audio samples
// (I2S-style data lines), while I2C is the *control* bus used to power up the
// codec, route the DAC to the headphone output and set volume. Bring the SAI
// up first (so MCLK is running), then init the codec, then `play()`.
#pragma once

#include <cstdint>

namespace codec {

// Probe + initialise the WM8994 for output at `sample_rate` Hz, 16-bit stereo,
// routed to the headphone jack. Requires the SAI MCLK to already be running.
// Returns false if the codec doesn't answer on I2C or init fails.
bool init(uint32_t sample_rate = 48000, uint8_t volume = 80);

// Un-mute / start the output path. Call once after init and after the SAI DMA
// stream has been started.
void play();

// Master output volume, 0..100.
void set_volume(uint8_t volume);

}  // namespace codec
