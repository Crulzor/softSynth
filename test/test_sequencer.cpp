// SPDX-License-Identifier: MIT
//
// Unit tests for the sequencer clock: step timing at a given tempo, start /
// stop behaviour, and tempo changes mid-step.
#include <doctest/doctest.h>

#include "sequencer/sequencer.hpp"

using sequencer::Sequencer;

namespace
{

constexpr float kSr = 48000.0f;
constexpr int kBlock = 64;

// 120 BPM, 16th notes: 48000 * 60 / 120 / 4 = 6000 frames per step.
constexpr int kFramesPerStep120 = 6000;

}  // namespace

TEST_CASE("starting plays step 0 at the top of the first block")
{
	Sequencer seq(kSr);
	CHECK(seq.step() == -1);
	const Sequencer::Event e = seq.process(true, 120.0f, kBlock);
	CHECK(e.step == 0);
	CHECK(e.offset == 0);
	CHECK(seq.step() == 0);
}

TEST_CASE("steps land every 6000 frames at 120 BPM")
{
	Sequencer seq(kSr);
	seq.process(true, 120.0f, kBlock);  // step 0 at frame 0

	int frame = kBlock;
	int last_step = 0;
	int last_start = 0;
	for(int block = 0; block < 2000; ++block, frame += kBlock)
	{
		const Sequencer::Event e = seq.process(true, 120.0f, kBlock);
		if(e.step < 0)
		{
			continue;
		}
		CHECK(e.step == (last_step + 1) % sequencer::kSteps);
		CHECK(e.offset >= 0);
		CHECK(e.offset < kBlock);
		const int start = frame + e.offset;
		CHECK(start - last_start ==
		      doctest::Approx(kFramesPerStep120).epsilon(0.001));
		last_step = e.step;
		last_start = start;
	}
	CHECK(last_step != 0);  // it actually advanced
}

TEST_CASE("the pattern wraps after 16 steps")
{
	Sequencer seq(kSr);
	seq.process(true, 300.0f, kBlock);
	int wraps = 0;
	int prev = 0;
	for(int block = 0; block < 1000; ++block)
	{
		seq.process(true, 300.0f, kBlock);
		if(seq.step() < prev)
		{
			++wraps;
			CHECK(seq.step() == 0);
		}
		prev = seq.step();
	}
	CHECK(wraps > 0);
}

TEST_CASE("stop clears the step; play again restarts from step 0")
{
	Sequencer seq(kSr);
	seq.process(true, 300.0f, kBlock);
	for(int block = 0; block < 100; ++block)
	{
		seq.process(true, 300.0f, kBlock);
	}
	CHECK(seq.step() > 0);

	const Sequencer::Event stopped = seq.process(false, 300.0f, kBlock);
	CHECK(stopped.step == -1);
	CHECK(seq.step() == -1);

	const Sequencer::Event restarted = seq.process(true, 300.0f, kBlock);
	CHECK(restarted.step == 0);
}

TEST_CASE("a tempo change keeps the progress through the current step")
{
	// Half way through a step at 120 BPM, doubling the tempo should leave
	// half of a (now 3000-frame) step: 1500 frames to the next step.
	Sequencer seq(kSr);
	seq.process(true, 120.0f, 3000);  // step 0, half done
	int frames_to_next = 0;
	for(int i = 0; i < 100; ++i)
	{
		const Sequencer::Event e = seq.process(true, 240.0f, 100);
		if(e.step >= 0)
		{
			frames_to_next += e.offset;
			break;
		}
		frames_to_next += 100;
	}
	CHECK(frames_to_next == doctest::Approx(1500).epsilon(0.01));
}
