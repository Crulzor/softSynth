// SPDX-License-Identifier: MIT
#include "sequencer/ext_clock.hpp"

namespace sequencer
{
namespace
{

// Peak follower decay per sample: ~1 s time constant at 48 kHz.
constexpr float kDecay = 0.99998f;

int pulses_per_step(ClockDiv div)
{
	switch(div)
	{
		case ClockDiv::Ppqn24:
			return 6;
		case ClockDiv::Ppqn48:
			return 12;
		default:
			return 1;
	}
}

int steps_per_pulse(ClockDiv div)
{
	switch(div)
	{
		case ClockDiv::Eighth:
			return 2;
		case ClockDiv::Quarter:
			return 4;
		default:
			return 1;
	}
}

int pulses_per_quarter(ClockDiv div)
{
	switch(div)
	{
		case ClockDiv::Sixteenth:
			return 4;
		case ClockDiv::Eighth:
			return 2;
		case ClockDiv::Quarter:
			return 1;
		case ClockDiv::Ppqn24:
			return 24;
		case ClockDiv::Ppqn48:
			return 48;
		case ClockDiv::Count:
			break;
	}
	return 4;
}

}  // namespace

// --- EdgeDetector
// -------------------------------------------------------------

int EdgeDetector::process(const std::int16_t* in, int stride, int n,
                          bool invert, int* edges)
{
	constexpr float kScale = 1.0f / 32768.0f;
	int count = 0;
	float peak = 0.0f;
	for(int i = 0; i < n; ++i)
	{
		float x = static_cast<float>(in[i * stride]) * kScale;
		if(invert)
		{
			x = -x;
		}
		const float a = x < 0.0f ? -x : x;
		peak = a > peak ? a : peak;
		env_ = a > env_ ? a : env_ * kDecay;
		const float t = kRatio * env_;
		high_ = t > kFloor ? t : kFloor;
		if(holdoff_ > 0)
		{
			--holdoff_;
		}
		if(!armed_)
		{
			if(x < 0.5f * high_)
			{
				armed_ = true;
			}
		}
		else if(x > high_ && holdoff_ == 0)
		{
			if(count < kMaxEdges)
			{
				edges[count++] = i;
			}
			armed_ = false;
			holdoff_ = kHoldoff;
		}
	}
	level_ = peak;
	return count;
}

// --- ExtClock
// -------------------------------------------------------------------

float ExtClock::bpm(ClockDiv div) const
{
	if(period_ <= 0)
	{
		return 0.0f;
	}
	return 60.0f * sample_rate_ /
	       (static_cast<float>(period_) *
	        static_cast<float>(pulses_per_quarter(div)));
}

float ExtClock::position() const
{
	if(!running_ || step_ < 0)
	{
		return -1.0f;
	}
	float frac = 0.0f;
	if(step_len_ > 0)
	{
		frac = static_cast<float>(since_step_) / static_cast<float>(step_len_);
		frac = frac > 0.999f ? 0.999f : frac;  // hold at the step's end
	}
	return static_cast<float>(total_) + frac;
}

void ExtClock::emit_step(Sequencer::Event& e, int step, int offset,
                         int step_len)
{
	if(restart_)
	{
		restart_ = false;
		total_ = 0;
	}
	else
	{
		++total_;
	}
	since_step_ = 0;
	step_len_ = step_len;
	step_ = step;
	e.step = step;
	e.offset = offset;
	e.gate_off = -1;  // a new step releases the old notes anyway
	gate_timer_ = step_len > 1 ? step_len / 2 : 0;
}

Sequencer::Event ExtClock::process(bool run, const int* clocks, int n_clocks,
                                   const int* resets, int n_resets,
                                   ClockDiv div, int frames)
{
	Sequencer::Event e{-1, 0, -1};
	if(!run)
	{
		running_ = false;
		sub_left_ = 0;
		gate_timer_ = 0;
	}
	else if(!running_)
	{
		// Armed: the next pulse plays step 0.
		running_ = true;
		restart_ = true;
		pulses_ = 0;
		step_ = -1;
		sub_left_ = 0;
		gate_timer_ = 0;
	}

	const int pps = pulses_per_step(div);
	const int spp = steps_per_pulse(div);
	const int timeout = static_cast<int>(2.0f * sample_rate_);
	int ci = 0;
	int ri = 0;
	for(int i = 0; i < frames; ++i)
	{
		if(since_step_ < (1 << 30))
		{
			++since_step_;
		}
		if(since_pulse_ >= 0 && ++since_pulse_ > timeout)
		{
			// Two seconds without a pulse: no clock.
			period_ = 0;
			since_pulse_ = -1;
			sub_left_ = 0;
		}

		// Timers first (a step / gate set up on frame t fires on t + len
		// exactly); a pulse on the same frame then overrides them.
		if(running_ && sub_left_ > 0 && --sub_timer_ <= 0)
		{
			emit_step(e, (step_ + 1) % kSteps, i, sub_len_);
			--sub_left_;
			sub_timer_ = sub_len_;
		}
		if(gate_timer_ > 0 && --gate_timer_ == 0 && e.step < 0)
		{
			e.gate_off = i;
		}

		// A reset first, so a reset and a clock on the same frame play 0.
		while(ri < n_resets && resets[ri] <= i)
		{
			++ri;
			pulses_ = 0;
			sub_left_ = 0;
			restart_ = true;
		}

		while(ci < n_clocks && clocks[ci] <= i)
		{
			++ci;
			if(since_pulse_ > 0)
			{
				// Light smoothing while the tempo is steady, a straight jump
				// when it changes a lot.
				const int m = since_pulse_;
				const int diff = m > period_ ? m - period_ : period_ - m;
				period_ =
				    (period_ > 0 && diff < period_ / 4) ? (period_ + m) / 2 : m;
			}
			since_pulse_ = 0;
			if(!running_)
			{
				continue;  // measure only
			}
			if(pps > 1)
			{
				if(pulses_ % static_cast<std::uint32_t>(pps) == 0)
				{
					emit_step(e, static_cast<int>((pulses_ / pps) % kSteps), i,
					          period_ * pps);
				}
			}
			else
			{
				sub_len_ = period_ / spp;
				emit_step(e, static_cast<int>((pulses_ * spp) % kSteps), i,
				          period_ > 0 ? sub_len_ : 0);
				sub_left_ = sub_len_ > 0 ? spp - 1 : 0;
				sub_timer_ = sub_len_;
			}
			++pulses_;
		}
	}
	return e;
}

}  // namespace sequencer
