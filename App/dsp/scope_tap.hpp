// SPDX-License-Identifier: MIT
//
// A triggered capture of the audio for the UI's scope - like a hardware
// scope in normal trigger mode.
//
// The audio interrupt feeds it the engine's output (after the filter and
// the envelopes, before the FX). When armed it waits for a rising zero
// crossing, records kSize samples, then marks the capture ready; the UI
// copies it out and re-arms. Without a trigger for a while (a bass note,
// silence, noise) it captures anyway, so the display never freezes.
//
// Hand-over: `state_` has one writer per transition - the interrupt moves
// Armed -> Capturing -> Ready, the UI moves Ready -> Armed - and the UI only
// reads the samples while the state is Ready, when the interrupt leaves
// them alone. No locks.
//
// Pure: no HAL, host-tested.
#pragma once

#include <cstdint>

namespace dsp
{

class ScopeTap
{
   public:
	static constexpr int kSize = 1024;  // ~21 ms at 48 kHz
	static constexpr int kMaxWait = 2048;

	enum State : std::uint32_t
	{
		Armed = 0,
		Capturing = 1,
		Ready = 2,
	};

	// Audio side: offer `n` samples. `period` is the newest note's period in
	// samples (0 = none sounding); it is stored with the capture.
	void feed(const float* x, int n, float period)
	{
		for(int i = 0; i < n; ++i)
		{
			const float v = x[i];
			if(state_ == Armed)
			{
				const bool rising = prev_ < 0.0f && v >= 0.0f;
				if(rising || ++waited_ >= kMaxWait)
				{
					state_ = Capturing;
					fill_ = 0;
					waited_ = 0;
				}
			}
			if(state_ == Capturing)
			{
				float s = v * 16384.0f;  // +-2 full scale
				s = s > 32767.0f ? 32767.0f : (s < -32768.0f ? -32768.0f : s);
				data_[fill_++] = static_cast<std::int16_t>(s);
				if(fill_ == kSize)
				{
					period_ = period;
					++count_;
					state_ = Ready;
				}
			}
			prev_ = v;
		}
	}

	// UI side ------------------------------------------------------------

	bool ready() const { return state_ == Ready; }

	// Copy the capture out (kSize samples, -2..2) and re-arm. Only valid
	// while ready().
	void take(float* out)
	{
		for(int i = 0; i < kSize; ++i)
		{
			out[i] = static_cast<float>(data_[i]) * (1.0f / 16384.0f);
		}
		state_ = Armed;
	}

	float period() const { return period_; }        // of the ready capture
	std::uint32_t count() const { return count_; }  // captures so far

   private:
	// All-zero defaults: a global holding this stays in .bss (no flash
	// image), and state 0 is Armed.
	std::int16_t data_[kSize] = {};
	volatile std::uint32_t state_ = Armed;
	volatile std::uint32_t count_ = 0;
	volatile float period_ = 0.0f;
	int fill_ = 0;
	int waited_ = 0;
	float prev_ = 0.0f;
};

}  // namespace dsp
