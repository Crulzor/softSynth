// SPDX-License-Identifier: MIT
//
// Audio-interrupt load meter, using the Cortex-M7 DWT cycle counter.
//
// The audio interrupt brackets its work with begin() / end(); the foreground
// loop calls take() now and then for the stats since the previous take().
// Load is the fraction of the block's time budget (frames / sample_rate) the
// work used: 1.0 means the interrupt only just made its deadline.
//
// It measures our render work, not the HAL's IRQ dispatch around it (a few
// hundred cycles), so it reads slightly low.
#pragma once

#include <cstdint>

namespace cpu_load
{

struct Stats
{
	float average;  // mean load over the window
	float peak;     // worst single block in the window
};

// Enable the cycle counter and fix the per-block budget for the current CPU
// clock. Call after the clock is final.
void init(std::uint32_t block_frames, std::uint32_t sample_rate);

// Audio interrupt only.
std::uint32_t begin();
void end(std::uint32_t start);

// Foreground only: stats since the last call, then reset the window.
Stats take();

}  // namespace cpu_load
