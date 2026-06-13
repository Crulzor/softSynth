// SPDX-License-Identifier: MIT
//
// Unit tests for the band-limited oscillator. These assert on properties we can
// measure cheaply and that actually matter for a synth: the pitch is correct
// (rising zero-crossing rate == frequency), the level is sane (bounded, no DC
// offset, expected RMS), and nothing produces NaN/Inf.
#include "dsp/oscillator.hpp"

#include <cmath>
#include <vector>

#include <doctest/doctest.h>

using dsp::Oscillator;
using Waveform = dsp::Oscillator::Waveform;

namespace
{

constexpr float kSr = 48000.0f;

std::vector<float> render(Oscillator& osc, int n)
{
	std::vector<float> out(static_cast<std::size_t>(n));
	for(int i = 0; i < n; ++i)
	{
		out[static_cast<std::size_t>(i)] = osc.process();
	}
	return out;
}

// Count rising zero crossings (— to +). For all our waveforms that happens
// exactly once per cycle, so the count over T seconds == frequency * T.
int rising_zero_crossings(const std::vector<float>& x)
{
	int count = 0;
	for(std::size_t i = 1; i < x.size(); ++i)
	{
		if(x[i - 1] < 0.0f && x[i] >= 0.0f)
		{
			++count;
		}
	}
	return count;
}

float rms(const std::vector<float>& x)
{
	double acc = 0.0;
	for(float v : x)
	{
		acc += static_cast<double>(v) * v;
	}
	return static_cast<float>(std::sqrt(acc / x.size()));
}

float mean(const std::vector<float>& x)
{
	double acc = 0.0;
	for(float v : x)
	{
		acc += v;
	}
	return static_cast<float>(acc / x.size());
}

float peak(const std::vector<float>& x)
{
	float p = 0.0f;
	for(float v : x)
	{
		p = std::max(p, std::fabs(v));
	}
	return p;
}

bool all_finite(const std::vector<float>& x)
{
	for(float v : x)
	{
		if(!std::isfinite(v))
		{
			return false;
		}
	}
	return true;
}

Oscillator make(Waveform w, float hz)
{
	Oscillator osc;
	osc.set_sample_rate(kSr);
	osc.set_waveform(w);
	osc.set_frequency(hz);
	osc.reset();
	return osc;
}

}  // namespace

TEST_CASE("sine matches reference frequency, level and purity")
{
	Oscillator osc = make(Waveform::Sine, 440.0f);
	const std::vector<float> x = render(osc, static_cast<int>(kSr));  // 1 s

	CHECK(all_finite(x));
	CHECK(rising_zero_crossings(x) == doctest::Approx(440).epsilon(0.01));
	CHECK(rms(x) == doctest::Approx(0.7071f).epsilon(0.02));  // sine RMS
	CHECK(peak(x) == doctest::Approx(1.0f).epsilon(0.01));
	CHECK(mean(x) == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("saw has correct pitch, no DC, and stays bounded")
{
	Oscillator osc = make(Waveform::Saw, 220.0f);
	const std::vector<float> x = render(osc, static_cast<int>(kSr));

	CHECK(all_finite(x));
	CHECK(rising_zero_crossings(x) == doctest::Approx(220).epsilon(0.01));
	CHECK(mean(x) == doctest::Approx(0.0f).epsilon(0.02));
	// PolyBLEP can overshoot the ideal +/-1 by ~1% near the wrap.
	CHECK(peak(x) < 1.05f);
}

TEST_CASE("square has correct pitch, ~50% duty and stays bounded")
{
	Oscillator osc = make(Waveform::Square, 330.0f);
	const std::vector<float> x = render(osc, static_cast<int>(kSr));

	CHECK(all_finite(x));
	CHECK(rising_zero_crossings(x) == doctest::Approx(330).epsilon(0.02));
	CHECK(mean(x) == doctest::Approx(0.0f).epsilon(0.02));  // 50% duty -> ~0 DC
	CHECK(peak(x) < 1.10f);
}

TEST_CASE("triangle has correct pitch and stays bounded")
{
	Oscillator osc = make(Waveform::Triangle, 110.0f);
	// Skip the integrator's start-up transient before measuring.
	render(osc, 2048);
	const std::vector<float> x = render(osc, static_cast<int>(kSr));

	CHECK(all_finite(x));
	CHECK(rising_zero_crossings(x) == doctest::Approx(110).epsilon(0.03));
	CHECK(mean(x) == doctest::Approx(0.0f).epsilon(0.05));
	CHECK(peak(x) < 1.10f);
}

TEST_CASE("set_sample_rate preserves frequency")
{
	Oscillator osc = make(Waveform::Sine, 1000.0f);
	osc.set_sample_rate(96000.0f);  // change rate, frequency must hold
	osc.reset();
	const std::vector<float> x = render(osc, 96000);  // 1 s at the new rate

	CHECK(rising_zero_crossings(x) == doctest::Approx(1000).epsilon(0.01));
}

TEST_CASE("a very high note never aliases into garbage (stays finite/bounded)")
{
	// Near Nyquist the naive waveforms would fold into junk; PolyBLEP should
	// keep them finite and bounded.
	for(Waveform w : {Waveform::Saw, Waveform::Square, Waveform::Triangle})
	{
		Oscillator osc = make(w, 12000.0f);
		const std::vector<float> x = render(osc, 4096);
		CHECK(all_finite(x));
		CHECK(peak(x) < 1.5f);
	}
}
