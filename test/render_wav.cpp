// SPDX-License-Identifier: MIT
//
// Audition the synth on the desktop: render the real engine (additive
// oscillators -> ladder -> envelope, six voices) to a .wav, so changes can
// be *heard* on a PC before flashing. For a synth, listening is the real
// regression test.
//
//   ./render_wav [out.wav]
//
// The tour, ~22 s:
//   1. the four basic waves, one note each, filter open
//   2. a saw morphing into a random spectrum and back (SHP sweep)
//   3. random <-> random morph
//   4. a resonant filter sweep over a saw chord
//   5. the same chord with the drive pushed up
//   6. a sequenced chord progression with a tie, through the real clock and
//      player (C maj7, A min7, F maj9 tied over two steps, G 7)
// Default output: softsynth.wav in the working directory.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dsp/engine.hpp"
#include "dsp/sine_table.hpp"
#include "dsp/spectrum.hpp"
#include "sequencer/chords.hpp"
#include "sequencer/player.hpp"
#include "sequencer/sequencer.hpp"
#include "wav_writer.hpp"

namespace sp = dsp::spectrum;

namespace
{

constexpr float kSr = dsp::kSampleRate;

struct Rig
{
	float spec[dsp::kOscs][dsp::kSlots][sp::kHarmonics];
	dsp::Patch patch;
	dsp::Engine engine;
	std::vector<float> out;

	Rig()
	{
		for(int o = 0; o < dsp::kOscs; ++o)
		{
			for(int s = 0; s < dsp::kSlots; ++s)
			{
				sp::make_basic(sp::Basic::Saw, spec[o][s]);
				patch.spectra[o][s] = spec[o][s];
			}
		}
		patch.mix = 0.0f;
		patch.cutoff = 1.0f;
		patch.resonance = 0.0f;
		patch.drive = 0.2f;
		patch.attack = 0.1f;
		patch.decay = 0.5f;
		patch.sustain = 0.8f;
		patch.release = 0.35f;
	}

	void set_slot(int osc, int slot, const float* c)
	{
		for(int h = 0; h < sp::kHarmonics; ++h)
		{
			spec[osc][slot][h] = c[h];
		}
	}

	// Render `seconds`, calling `each_block(t)` (t = 0..1 through the span)
	// before every block.
	template <typename F>
	void run(float seconds, F each_block)
	{
		const int frames = static_cast<int>(seconds * kSr);
		float buf[dsp::kBlockSize];
		for(int i = 0; i < frames; i += dsp::kBlockSize)
		{
			each_block(static_cast<float>(i) / static_cast<float>(frames));
			engine.render(patch, buf, dsp::kBlockSize);
			for(float v : buf)
			{
				out.push_back(std::tanh(v));  // the firmware's master clip
			}
		}
	}

	void run(float seconds)
	{
		run(seconds, [](float) {});
	}
};

}  // namespace

int main(int argc, char** argv)
{
	const std::string path = (argc > 1) ? argv[1] : "softsynth.wav";
	dsp::sine_table::init();
	Rig r;

	// 1. The basic waves.
	for(int b = 0; b < static_cast<int>(sp::Basic::Count); ++b)
	{
		float c[sp::kHarmonics];
		sp::make_basic(static_cast<sp::Basic>(b), c);
		r.set_slot(0, 0, c);
		r.set_slot(0, 1, c);
		r.engine.note_on(57, 110, 0);  // A3
		r.run(0.8f);
		r.engine.note_off(57, 0);
		r.run(0.3f);
	}

	// 2. Saw -> random -> saw.
	float saw[sp::kHarmonics];
	float rnd[sp::kHarmonics];
	sp::make_basic(sp::Basic::Saw, saw);
	sp::make_random(0xC0FFEEu, rnd);
	r.set_slot(0, 0, saw);
	r.set_slot(0, 1, rnd);
	r.engine.note_on(45, 110, 0);  // A2
	r.run(3.0f, [&](float t)
	      { r.patch.shape[0] = t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t; });
	r.engine.note_off(45, 0);
	r.run(0.4f);

	// 3. Random <-> random.
	float rnd2[sp::kHarmonics];
	sp::make_random(0xBADA55u, rnd2);
	r.set_slot(0, 0, rnd);
	r.set_slot(0, 1, rnd2);
	r.engine.note_on(52, 110, 0);  // E3
	r.run(2.0f, [&](float t) { r.patch.shape[0] = t; });
	r.engine.note_off(52, 0);
	r.run(0.4f);

	// 4. Resonant filter sweep over a saw / square chord, osc 2 a fifth up.
	float sq[sp::kHarmonics];
	sp::make_basic(sp::Basic::Square, sq);
	r.set_slot(0, 0, saw);
	r.set_slot(0, 1, saw);
	r.set_slot(1, 0, sq);
	r.set_slot(1, 1, sq);
	r.patch.shape[0] = 0.0f;
	r.patch.mix = 0.4f;
	r.patch.osc_semis[1] = 7.0f;
	r.patch.resonance = 0.8f;
	const std::uint8_t chord[] = {45, 52, 57, 60};
	for(std::uint8_t n : chord)
	{
		r.engine.note_on(n, 100, 0);
	}
	r.run(4.0f, [&](float t)
	      { r.patch.cutoff = 0.25f + 0.6f * (t < 0.5f ? 2 * t : 2 - 2 * t); });

	// 5. Same chord, drive up.
	r.patch.cutoff = 0.6f;
	r.patch.resonance = 0.5f;
	r.run(3.0f, [&](float t) { r.patch.drive = t; });
	r.engine.release_all();
	r.run(0.8f);

	// 6. A sequenced progression through the real clock and player.
	r.patch.drive = 0.35f;
	r.patch.cutoff = 0.55f;
	r.patch.resonance = 0.3f;
	r.patch.attack = 0.15f;
	r.patch.release = 0.45f;
	r.patch.osc_semis[1] = 12.0f;
	r.patch.routes[0] = {static_cast<std::uint8_t>(dsp::ModSrc::AmpEnv),
	                     static_cast<std::uint8_t>(dsp::ModDst::Cutoff), 0.3f};
	sequencer::Pattern pat;
	using sequencer::Chord;
	auto set = [&](int i, std::uint8_t note, Chord c)
	{
		pat.steps[i].on = true;
		pat.steps[i].note = note;
		pat.steps[i].chord = static_cast<std::uint8_t>(c);
	};
	set(0, 48, Chord::Maj7);
	set(4, 45, Chord::Min7);
	set(8, 41, Chord::Maj9);
	pat.steps[9].tie = true;
	pat.steps[10].tie = true;
	set(12, 43, Chord::Dom7);
	set(14, 43, Chord::Sus4);
	sequencer::Sequencer clock(kSr);
	sequencer::Player player;
	sequencer::NoteEvent ev[sequencer::Player::kMaxEvents];
	r.run(6.0f, [&](float)
	      {
		      const sequencer::Sequencer::Event e =
		          clock.process(true, 100.0f, dsp::kBlockSize);
		      const int n = player.on_event(pat, e, ev);
		      for(int i = 0; i < n; ++i)
		      {
			      if(ev[i].on)
			      {
				      r.engine.note_on(ev[i].note, ev[i].velocity, 1);
			      }
			      else
			      {
				      r.engine.note_off(ev[i].note, 1);
			      }
		      }
	      });
	const int n = player.stop(ev);
	for(int i = 0; i < n; ++i)
	{
		r.engine.note_off(ev[i].note, 1);
	}
	r.run(1.0f);

	if(!wav::write(path, r.out, /*channels=*/1, static_cast<int>(kSr)))
	{
		std::fprintf(stderr, "render_wav: could not write %s\n", path.c_str());
		return 1;
	}
	std::printf("render_wav: wrote %s (%zu samples, %.1f s)\n", path.c_str(),
	            r.out.size(), r.out.size() / kSr);
	return 0;
}
