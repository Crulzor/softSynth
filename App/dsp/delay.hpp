// SPDX-License-Identifier: MIT
//
// Stereo delay: mono in, stereo wet out.
//
//   MONO  one line: in + feedback -> line -> out, both channels.
//   PING  two lines crossing over: the input goes into the left line, the
//         left line feeds the right and the right the left, so the repeats
//         bounce L, R, L, R ...
//
// In the feedback path: a one-pole low-pass (TONE: darker repeats, like a
// tape or BBD echo) and a soft clip, so high feedback sings and saturates
// instead of running away. The delay time glides to a new value (~50 ms),
// so turning TIME bends the repeats' pitch like tape instead of clicking.
// The read is linearly interpolated, so the glide is smooth.
//
// The lines are 16-bit (+-2 full scale, ~-90 dB noise) and live outside
// this object: on the board 2 x 1 s in RAM_D2 (192 KB, too big for DTCM),
// on the host anywhere. init() clears them.
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

#include "dsp/dsp_config.hpp"

namespace dsp
{

// SYNC lengths, shortest first, in 16th-note steps (1/32 .. 1/2, with
// triplet and dotted values).
inline constexpr int kDelayDivCount = 11;
inline constexpr float kDelayDivSteps[kDelayDivCount] = {
    0.5f,         // 1/32
    2.0f / 3.0f,  // 1/16 triplet
    1.0f,         // 1/16
    4.0f / 3.0f,  // 1/8 triplet
    1.5f,         // 1/16 dotted
    2.0f,         // 1/8
    8.0f / 3.0f,  // 1/4 triplet
    3.0f,         // 1/8 dotted
    4.0f,         // 1/4
    6.0f,         // 1/4 dotted
    8.0f,         // 1/2
};

struct DelayParams
{
	float seconds = 0.3f;   // delay time, clamped to the line length
	float feedback = 0.4f;  // 0..1 (1 = endless, soft-limited)
	float tone = 0.7f;      // 0..1: feedback low-pass 300 Hz .. 20 kHz
	bool ping = false;      // ping-pong (stereo) instead of mono
};

class Delay
{
   public:
	// `left` / `right`: `length` samples each (the longest delay).
	void init(std::int16_t* left, std::int16_t* right, int length);

	// Mono `in`, wet `out_l` / `out_r`; `n` samples.
	void process(const DelayParams& p, const Sample* in, Sample* out_l,
	             Sample* out_r, int n, float sample_rate = kSampleRate);

	int length() const { return length_; }

	// Silence the lines (an effect switched back on starts clean).
	void clear() { init(line_[0], line_[1], length_); }

   private:
	float read(const std::int16_t* line, float delay) const;

	std::int16_t* line_[2] = {nullptr, nullptr};
	int length_ = 0;
	int write_ = 0;
	// All members default to zero: a zero-initialised object goes to .bss
	// instead of carrying an image in flash (dsp::Effects is ~70 KB).
	float delay_ = 0.0f;   // current (gliding) delay in samples
	bool primed_ = false;  // delay_ holds a value
	float lp_[2] = {0.0f, 0.0f};
};

}  // namespace dsp
