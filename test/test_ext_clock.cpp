// SPDX-License-Identifier: MIT
//
// Unit tests for the external clock input: the edge detector on realistic
// (AC-coupled, noisy, weak or clipped, inverted) pulse trains, and the
// external clock that turns pulses into sequencer steps, gates and tempo.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "sequencer/ext_clock.hpp"

using sequencer::ClockDiv;
using sequencer::EdgeDetector;
using sequencer::ExtClock;
using sequencer::Sequencer;

namespace
{

constexpr float kSr = 48000.0f;
constexpr int kBlock = 64;

// A pulse train as it reaches the ADC: 0/`amp` gates of `width` frames every
// `period` frames (first rising edge at `first`), through a one-pole
// high-pass at `hpf_hz` (the input's coupling cap + the codec's filter),
// plus a little noise, clipped to 16 bits. Interleaved stereo with the
// pulses on the left and `right` (scaled) on the right.
std::vector<std::int16_t> pulses(int frames, int first, int period, int width,
                                 float amp, float hpf_hz = 4.0f,
                                 float right = 0.0f)
{
	std::vector<std::int16_t> out(static_cast<std::size_t>(2 * frames));
	const float a = std::exp(-2.0f * 3.14159265f * hpf_hz / kSr);
	float prev_in = 0.0f;
	float y = 0.0f;
	std::uint32_t rng = 12345u;
	for(int i = 0; i < frames; ++i)
	{
		const bool high = i >= first && ((i - first) % period) < width;
		const float x = high ? amp : 0.0f;
		y = a * (y + x - prev_in);
		prev_in = x;
		rng = rng * 1664525u + 1013904223u;
		const float noise =
		    (static_cast<float>(rng >> 9) / 8388608.0f - 0.5f) * 0.004f;
		auto q = [](float v)
		{
			v = v > 0.9999f ? 0.9999f : (v < -1.0f ? -1.0f : v);
			return static_cast<std::int16_t>(v * 32767.0f);
		};
		out[2 * i] = q(y + noise);
		out[2 * i + 1] = q(right * y + noise);
	}
	return out;
}

// Run the detector over the whole buffer in blocks; absolute edge frames.
std::vector<int> detect(const std::vector<std::int16_t>& buf, int channel,
                        bool invert = false)
{
	EdgeDetector d;
	std::vector<int> all;
	const int frames = static_cast<int>(buf.size() / 2);
	int e[EdgeDetector::kMaxEdges];
	for(int f = 0; f < frames; f += kBlock)
	{
		const int n = d.process(&buf[2 * f + channel], 2, kBlock, invert, e);
		for(int k = 0; k < n; ++k)
		{
			all.push_back(f + e[k]);
		}
	}
	return all;
}

}  // namespace

TEST_CASE("the detector finds every rising edge of a 50% gate clock")
{
	// 16ths at 120 BPM: 6000 frames per pulse, half of it high.
	const auto buf = pulses(48000 * 2, 1000, 6000, 3000, 0.8f);
	const auto edges = detect(buf, 0);
	REQUIRE(edges.size() == 16);
	for(std::size_t k = 0; k < edges.size(); ++k)
	{
		CAPTURE(k);
		const int want = 1000 + static_cast<int>(k) * 6000;
		CHECK(edges[k] >= want);
		CHECK(edges[k] <= want + 2);  // sample-accurate
	}
}

TEST_CASE("short triggers, weak and clipped levels all detect")
{
	for(float amp : {0.08f, 0.5f, 3.0f})  // weak line .. heavily clipped
	{
		CAPTURE(amp);
		const auto buf = pulses(48000, 500, 4000, 240, amp);  // 5 ms triggers
		const auto edges = detect(buf, 0);
		REQUIRE(edges.size() == 12);
		CHECK(edges[0] - 500 <= 3);
		CHECK(edges[11] - (500 + 11 * 4000) <= 3);
	}
}

TEST_CASE("noise alone and a grounded channel never trigger")
{
	const auto buf = pulses(48000, 0, 4000, 200, 0.0f);  // noise only
	CHECK(detect(buf, 0).empty());
	const auto clk = pulses(48000, 0, 4000, 200, 0.7f, 4.0f, 0.0f);
	CHECK(detect(clk, 1).empty());  // right channel: nothing but noise
}

TEST_CASE("an inverted input needs EXT- to be seen at its leading edge")
{
	// Negative-going 10 ms triggers. After the AC coupling the release only
	// brings the signal back to about zero - there is no positive edge to
	// find - so only the inverted detector (EXT-) clocks, on the leading
	// edge.
	const auto buf = pulses(48000, 1000, 6000, 480, -0.6f);
	const auto inv = detect(buf, 0, true);
	REQUIRE(inv.size() == 8);
	CHECK(inv[0] - 1000 <= 2);
	const auto raw = detect(buf, 0, false);
	CHECK((raw.empty() || raw[0] >= 1000 + 480));
}

TEST_CASE("the hold-off ignores ringing right after an edge")
{
	// Two edges 1 ms apart count once; 10 ms apart count twice.
	std::vector<std::int16_t> buf(2 * 4800, 0);
	auto spike = [&](int at)
	{
		for(int i = at; i < at + 20; ++i)
		{
			buf[2 * i] = 20000;
		}
	};
	spike(100);
	spike(148);   // 1 ms later
	spike(1000);  // well after the hold-off
	CHECK(detect(buf, 0).size() == 2);
}

// --- ExtClock
// -----------------------------------------------------------------

namespace
{

struct Run
{
	ExtClock clk{kSr};
	int frame = 0;
	std::vector<std::pair<int, int>> steps;  // (absolute frame, step)
	std::vector<int> gates;                  // absolute frames

	// Advance `frames`, with clock edges at the absolute frames in `at`.
	void go(int frames, const std::vector<int>& at, ClockDiv div,
	        bool run = true, const std::vector<int>& resets = {})
	{
		for(int f = 0; f < frames; f += kBlock)
		{
			int c[8];
			int nc = 0;
			int r[8];
			int nr = 0;
			for(int t : at)
			{
				if(t >= frame && t < frame + kBlock)
				{
					c[nc++] = t - frame;
				}
			}
			for(int t : resets)
			{
				if(t >= frame && t < frame + kBlock)
				{
					r[nr++] = t - frame;
				}
			}
			const Sequencer::Event e =
			    clk.process(run, c, nc, r, nr, div, kBlock);
			if(e.step >= 0)
			{
				steps.emplace_back(frame + e.offset, e.step);
			}
			if(e.gate_off >= 0)
			{
				gates.push_back(frame + e.gate_off);
			}
			frame += kBlock;
		}
	}
};

std::vector<int> every(int first, int period, int count)
{
	std::vector<int> v;
	for(int k = 0; k < count; ++k)
	{
		v.push_back(first + k * period);
	}
	return v;
}

}  // namespace

TEST_CASE("1/16: each pulse is the next step, at the pulse's frame")
{
	Run r;
	r.go(48000, every(1000, 6000, 8), ClockDiv::Sixteenth);
	REQUIRE(r.steps.size() == 8);
	for(int k = 0; k < 8; ++k)
	{
		CHECK(r.steps[k].first == 1000 + k * 6000);
		CHECK(r.steps[k].second == k);
	}
	// Gates close half a measured step after each step (once known).
	REQUIRE(r.gates.size() >= 6);
	CHECK(r.gates[0] == doctest::Approx(1000 + 6000 + 3000).epsilon(0.001));
	CHECK(r.clk.bpm(ClockDiv::Sixteenth) == doctest::Approx(120.0f));
}

TEST_CASE("1/4: a pulse per beat plays four steps spread over the beat")
{
	Run r;
	r.go(48000 * 2, every(0, 24000, 4), ClockDiv::Quarter);  // 120 BPM
	// Pulse 0 has no interval yet: step 0 only. From pulse 1 on, 4 steps
	// per pulse, 6000 frames apart.
	REQUIRE(r.steps.size() >= 9);
	CHECK(r.steps[0] == std::make_pair(0, 0));
	CHECK(r.steps[1] == std::make_pair(24000, 4));
	CHECK(r.steps[2] == std::make_pair(30000, 5));
	CHECK(r.steps[3] == std::make_pair(36000, 6));
	CHECK(r.steps[4] == std::make_pair(42000, 7));
	CHECK(r.steps[5] == std::make_pair(48000, 8));
	CHECK(r.clk.bpm(ClockDiv::Quarter) == doctest::Approx(120.0f));
}

TEST_CASE("24 PPQN: every sixth pulse is a step")
{
	Run r;
	// 120 BPM at 24 PPQN: 1000 frames per pulse.
	r.go(24000, every(0, 1000, 24), ClockDiv::Ppqn24);
	REQUIRE(r.steps.size() == 4);
	CHECK(r.steps[1] == std::make_pair(6000, 1));
	CHECK(r.steps[3] == std::make_pair(18000, 3));
	CHECK(r.clk.bpm(ClockDiv::Ppqn24) == doctest::Approx(120.0f));
}

TEST_CASE("reset makes the next pulse step 0; play arms on the next pulse")
{
	Run r;
	r.go(6000 * 5, every(100, 6000, 5), ClockDiv::Sixteenth);
	REQUIRE(r.steps.back().second == 4);
	// A reset just before the next pulse.
	int t = r.frame + 100;
	r.go(6000 * 2, every(t, 6000, 2), ClockDiv::Sixteenth, true, {t - 50});
	REQUIRE(r.steps.size() == 7);
	CHECK(r.steps[5].second == 0);
	CHECK(r.steps[6].second == 1);

	// Stopped: pulses are measured but play nothing.
	t = r.frame + 100;
	r.go(6000 * 2, every(t, 6000, 2), ClockDiv::Sixteenth, false);
	CHECK(r.steps.size() == 7);
	CHECK(r.clk.step() == -1);
	// (the gap from the previous run's last pulse is averaged in a little)
	CHECK(r.clk.bpm(ClockDiv::Sixteenth) ==
	      doctest::Approx(120.0f).epsilon(0.01));
	// Play: armed, nothing until the next pulse - which is step 0.
	r.go(3008, {}, ClockDiv::Sixteenth, true);
	CHECK(r.clk.step() == -1);
	t = r.frame + 100;
	r.go(6000, {t}, ClockDiv::Sixteenth, true);
	REQUIRE(r.steps.size() == 8);
	CHECK(r.steps.back() == std::make_pair(t, 0));
}

TEST_CASE("a clock that stops holds the playhead and reads as no clock")
{
	Run r;
	r.go(30000, every(0, 6000, 5), ClockDiv::Sixteenth);
	const int last = r.clk.step();
	r.go(48000 * 3, {}, ClockDiv::Sixteenth);
	CHECK(r.clk.step() == last);
	CHECK(r.clk.bpm(ClockDiv::Sixteenth) == 0.0f);
}

TEST_CASE("a tempo change is followed straight away")
{
	Run r;
	r.go(24000, every(0, 6000, 4), ClockDiv::Sixteenth);             // 120 BPM
	r.go(24000, every(24000 + 4000, 4000, 5), ClockDiv::Sixteenth);  // 180
	CHECK(r.clk.bpm(ClockDiv::Sixteenth) == doctest::Approx(180.0f));
}

TEST_CASE("detector + clock end to end: a gate clock drives the steps")
{
	const auto buf = pulses(48000, 2000, 6000, 3000, 0.7f);
	EdgeDetector det;
	ExtClock clk(kSr);
	std::vector<int> steps;
	int e[EdgeDetector::kMaxEdges];
	for(int f = 0; f < 48000; f += kBlock)
	{
		const int n = det.process(&buf[2 * f], 2, kBlock, false, e);
		const Sequencer::Event ev =
		    clk.process(true, e, n, nullptr, 0, ClockDiv::Sixteenth, kBlock);
		if(ev.step >= 0)
		{
			steps.push_back(f + ev.offset);
		}
	}
	REQUIRE(steps.size() == 8);
	CHECK(steps[0] - 2000 <= 2);
	CHECK(steps[7] - (2000 + 7 * 6000) <= 2);
}

TEST_CASE("the external clock's song position follows the pulses")
{
	Run r;
	CHECK(r.clk.position() < 0.0f);
	r.go(6000 * 20 + 3000, every(0, 6000, 21), ClockDiv::Sixteenth);
	// 20.5 steps after the first pulse; the count runs past 16.
	CHECK(r.clk.position() == doctest::Approx(20.5f).epsilon(0.01));
	// A reset restarts it.
	const int t = r.frame + 100;
	r.go(6000, {t}, ClockDiv::Sixteenth, true, {t - 10});
	CHECK(r.clk.position() < 1.0f);
}
