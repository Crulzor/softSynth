// SPDX-License-Identifier: MIT
//
// Audition the DSP on the desktop: render the oscillator to a .wav so synth
// changes can be *heard* on a PC before flashing the board. For a synth,
// listening is the real regression test.
//
//   ./render_wav [out.wav]
//
// Plays each waveform for one second at A3 (220 Hz). Default output:
// softsynth.wav in the working directory.
#include "dsp/dsp_config.hpp"
#include "dsp/oscillator.hpp"
#include "wav_writer.hpp"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
	const std::string path = (argc > 1) ? argv[1] : "softsynth.wav";

	constexpr float kSr = dsp::kSampleRate;
	constexpr float kNoteHz = 220.0f;     // A3
	constexpr float kSeconds = 1.0f;      // per waveform
	constexpr float kGain = 0.5f;         // headroom
	const int per_wave = static_cast<int>(kSr * kSeconds);

	const dsp::Oscillator::Waveform order[] = {
	    dsp::Oscillator::Waveform::Sine,
	    dsp::Oscillator::Waveform::Saw,
	    dsp::Oscillator::Waveform::Square,
	    dsp::Oscillator::Waveform::Triangle,
	};

	std::vector<float> mono;
	mono.reserve(static_cast<std::size_t>(per_wave) * 4);

	for(dsp::Oscillator::Waveform w : order)
	{
		dsp::Oscillator osc;
		osc.set_sample_rate(kSr);
		osc.set_waveform(w);
		osc.set_frequency(kNoteHz);
		osc.reset();
		for(int i = 0; i < per_wave; ++i)
		{
			mono.push_back(kGain * osc.process());
		}
	}

	if(!wav::write(path, mono, /*channels=*/1, static_cast<int>(kSr)))
	{
		std::fprintf(stderr, "render_wav: could not write %s\n", path.c_str());
		return 1;
	}

	std::printf("render_wav: wrote %s (%zu samples, %.1f s)\n", path.c_str(),
	            mono.size(), mono.size() / kSr);
	return 0;
}
