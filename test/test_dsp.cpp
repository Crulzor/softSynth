// SPDX-License-Identifier: MIT
//
// Unit tests for the synth DSP: sine table, spectra, the additive oscillator,
// the ladder filter, the envelope and the 6-voice engine. They assert on
// things that can be measured cheaply and that matter for a synth: pitch,
// level, spectral content (Goertzel at chosen frequencies), boundedness, and
// the voice-allocation rules.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "dsp/additive_osc.hpp"
#include "dsp/engine.hpp"
#include "dsp/envelope.hpp"
#include "dsp/ladder.hpp"
#include "dsp/lfo.hpp"
#include "dsp/sine_table.hpp"
#include "params/parameters.hpp"
#include "dsp/spectrum.hpp"

using namespace dsp;
namespace sp = dsp::spectrum;

namespace
{

constexpr float kSr = kSampleRate;
constexpr double kPiD = 3.14159265358979323846;

void init_table() { sine_table::init(); }

// Amplitude of the `hz` component of `x` (Goertzel), 1.0 for a full-scale
// sine at exactly that frequency.
double tone(const std::vector<float>& x, double hz)
{
	const double w = 2.0 * kPiD * hz / kSr;
	const double c = 2.0 * std::cos(w);
	double s1 = 0.0;
	double s2 = 0.0;
	for(float v : x)
	{
		const double s0 = v + c * s1 - s2;
		s2 = s1;
		s1 = s0;
	}
	const double re = s1 - s2 * std::cos(w);
	const double im = s2 * std::sin(w);
	return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(x.size());
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
	return std::all_of(x.begin(), x.end(),
	                   [](float v) { return std::isfinite(v); });
}

double rms(const std::vector<float>& x)
{
	double acc = 0.0;
	for(float v : x)
	{
		acc += static_cast<double>(v) * v;
	}
	return std::sqrt(acc / static_cast<double>(x.size()));
}

// Render the oscillator at `hz` with spectra a -> b at a fixed shape.
std::vector<float> render_osc(const float* a, const float* b, float shape,
                              float hz, int n)
{
	float ga[sp::kHarmonics];
	float gd[sp::kHarmonics];
	const float inc = hz / kSr;
	int harmonics = 1;
	for(int h = 0; h < sp::kHarmonics; ++h)
	{
		const float g = harmonic_gain(h + 1, inc);
		ga[h] = a[h] * g;
		gd[h] = (b[h] - a[h]) * g;
		if(g > 0.0f)
		{
			harmonics = h + 1;
		}
	}
	AdditiveOsc osc;
	std::vector<float> out(static_cast<std::size_t>(n));
	for(int i = 0; i < n; i += kBlockSize)
	{
		const int len = std::min(kBlockSize, n - i);
		osc.render(ga, gd, harmonics, shape, shape, inc, &out[i], len);
	}
	return out;
}

// A patch with every slot set to `shape`, filter wide open, fast envelope.
struct TestPatch
{
	float spec[kOscs][kSlots][sp::kHarmonics];
	Patch p;

	explicit TestPatch(sp::Basic shape = sp::Basic::Saw)
	{
		for(auto& osc : spec)
		{
			for(auto& slot : osc)
			{
				sp::make_basic(shape, slot);
			}
		}
		for(int o = 0; o < kOscs; ++o)
		{
			for(int s = 0; s < kSlots; ++s)
			{
				p.spectra[o][s] = spec[o][s];
			}
		}
		p.attack = 0.0f;
		p.decay = 0.3f;
		p.sustain = 1.0f;
		p.release = 0.0f;
		p.cutoff = 1.0f;
		p.resonance = 0.0f;
		p.drive = 0.0f;
	}
};

std::vector<float> render_engine(Engine& e, const Patch& p, int n)
{
	std::vector<float> out(static_cast<std::size_t>(n));
	for(int i = 0; i < n; i += kBlockSize)
	{
		const int len = std::min(kBlockSize, n - i);
		e.render(p, &out[i], len);
	}
	return out;
}

}  // namespace

// --- Sine table ------------------------------------------------------------

TEST_CASE("the sine table matches sin() to better than -100 dB")
{
	init_table();
	double worst = 0.0;
	for(int i = 0; i < 10000; ++i)
	{
		const float ph = static_cast<float>(i) / 10000.0f;
		worst = std::max(worst, std::fabs(sine_table::lookup(ph) -
		                                  std::sin(2.0 * kPiD * ph)));
		worst = std::max(worst, std::fabs(sine_table::lookup_cos(ph) -
		                                  std::cos(2.0 * kPiD * ph)));
	}
	CHECK(worst < 1e-5);
}

// --- Spectra ---------------------------------------------------------------

TEST_CASE("the basic spectra are the textbook Fourier series")
{
	float saw[sp::kHarmonics];
	float sq[sp::kHarmonics];
	float tri[sp::kHarmonics];
	float sine[sp::kHarmonics];
	sp::make_basic(sp::Basic::Saw, saw);
	sp::make_basic(sp::Basic::Square, sq);
	sp::make_basic(sp::Basic::Triangle, tri);
	sp::make_basic(sp::Basic::Sine, sine);

	// Ratios to the fundamental (normalisation scales them all alike).
	CHECK(saw[1] / saw[0] == doctest::Approx(0.5f));
	CHECK(saw[9] / saw[0] == doctest::Approx(0.1f));
	CHECK(sq[1] == 0.0f);
	CHECK(sq[2] / sq[0] == doctest::Approx(1.0f / 3.0f));
	CHECK(tri[2] / tri[0] == doctest::Approx(-1.0f / 9.0f));
	CHECK(tri[4] / tri[0] == doctest::Approx(1.0f / 25.0f));
	CHECK(sine[0] == doctest::Approx(1.0f));
	CHECK(sine[1] == 0.0f);

	// All peak-normalised.
	for(const float* s : {saw, sq, tri, sine})
	{
		CHECK(sp::peak(s) == doctest::Approx(1.0f).epsilon(0.001));
	}
}

TEST_CASE("random spectra are repeatable per seed, varied across seeds")
{
	float a[sp::kHarmonics];
	float b[sp::kHarmonics];
	float c[sp::kHarmonics];
	sp::make_random(1234u, a);
	sp::make_random(1234u, b);
	sp::make_random(1235u, c);
	CHECK(std::equal(a, a + sp::kHarmonics, b));
	CHECK_FALSE(std::equal(a, a + sp::kHarmonics, c));

	for(std::uint32_t seed = 1; seed < 200; ++seed)
	{
		sp::make_random(seed * 2654435761u, a);
		CAPTURE(seed);
		CHECK(a[0] > 0.0f);  // the fundamental is always there
		CHECK(sp::peak(a) == doctest::Approx(1.0f).epsilon(0.001));
		int live = 0;
		for(float v : a)
		{
			CHECK(std::isfinite(v));
			live += v != 0.0f ? 1 : 0;
		}
		CHECK(live >= 2);  // more than a bare sine
	}
}

// --- Additive oscillator ---------------------------------------------------

TEST_CASE("the oscillator's harmonic recurrence matches the direct sum")
{
	init_table();
	for(std::uint32_t seed : {7u, 99u, 4242u})
	{
		float a[sp::kHarmonics];
		sp::make_random(seed, a);
		// One cycle at exactly 48000 / 480 = 100 Hz: 480 samples.
		const std::vector<float> got = render_osc(a, a, 0.0f, 100.0f, 480);
		std::vector<float> ref(480);
		sp::render_cycle(a, ref.data(), 480);
		double worst = 0.0;
		for(int i = 0; i < 480; ++i)
		{
			worst = std::max(worst,
			                 std::fabs(static_cast<double>(got[i]) - ref[i]));
		}
		CAPTURE(seed);
		CHECK(worst < 3e-4);  // about -70 dB of a full-scale wave
	}
}

TEST_CASE("a sine spectrum plays a clean sine at the right pitch")
{
	init_table();
	float s[sp::kHarmonics];
	sp::make_basic(sp::Basic::Sine, s);
	const std::vector<float> x = render_osc(s, s, 0.0f, 440.0f, 48000);
	CHECK(tone(x, 440.0) == doctest::Approx(1.0).epsilon(0.01));
	CHECK(tone(x, 880.0) < 1e-4);
	CHECK(peak(x) <= 1.0001f);
}

TEST_CASE("the shape knob morphs linearly between the two spectra")
{
	init_table();
	float saw[sp::kHarmonics];
	float sq[sp::kHarmonics];
	sp::make_basic(sp::Basic::Saw, saw);
	sp::make_basic(sp::Basic::Square, sq);
	const float f = 200.0f;
	const std::vector<float> x0 = render_osc(saw, sq, 0.0f, f, 48000);
	const std::vector<float> x1 = render_osc(saw, sq, 1.0f, f, 48000);
	const std::vector<float> xh = render_osc(saw, sq, 0.5f, f, 48000);
	// 2nd harmonic: saw has it, square doesn't; halfway has half.
	CHECK(tone(x0, 2 * f) == doctest::Approx(saw[1]).epsilon(0.01));
	CHECK(tone(x1, 2 * f) < 1e-4);
	CHECK(tone(xh, 2 * f) == doctest::Approx(0.5 * saw[1]).epsilon(0.01));
	// A blend of two peak-1 waves never peaks above 1.
	CHECK(peak(xh) <= 1.0001f);
}

TEST_CASE("high notes drop the harmonics above Nyquist instead of aliasing")
{
	init_table();
	float saw[sp::kHarmonics];
	sp::make_basic(sp::Basic::Saw, saw);
	const float f = 2900.0f;  // harmonics 8+ would sit above 21.6 kHz
	const std::vector<float> x = render_osc(saw, saw, 0.0f, f, 48000);
	CHECK(tone(x, 3 * f) > 0.1);  // 8.7 kHz, well inside: present
	// Harmonic 9 (26.1 kHz) would alias to 21.9 kHz, harmonic 10 (29 kHz)
	// to 19 kHz; neither is a real harmonic of 2.9 kHz.
	CHECK(tone(x, 21900.0) < 1e-4);
	CHECK(tone(x, 19000.0) < 1e-4);
	CHECK(harmonic_gain(1, 0.5f) == 0.0f);
	CHECK(harmonic_gain(1, 0.01f) == 1.0f);
}

// --- Ladder filter -----------------------------------------------------------

namespace
{

std::vector<float> filter_sine(float hz, float cutoff, float res, float drive,
                               float amp = 0.5f, int n = 48000)
{
	Ladder l;
	std::vector<float> x(static_cast<std::size_t>(n));
	for(int i = 0; i < n; ++i)
	{
		x[i] = amp * static_cast<float>(std::sin(2.0 * kPiD * hz * i / kSr));
	}
	const float g = Ladder::coefficient(cutoff);
	for(int i = 0; i < n; i += kBlockSize)
	{
		l.process(&x[i], std::min(kBlockSize, n - i), g, g,
		          Ladder::feedback(res, g), drive, 1.0f);
	}
	return x;
}

}  // namespace

TEST_CASE("the ladder passes the passband and cuts 24 dB per octave above")
{
	// Small signal, so it is close to linear.
	const double pass =
	    tone(filter_sine(100.0f, 1000.0f, 0.0f, 0.2f), 100.0) / 0.1;
	const double one_oct =
	    tone(filter_sine(4000.0f, 1000.0f, 0.0f, 0.2f), 4000.0) / 0.1;
	const double two_oct =
	    tone(filter_sine(8000.0f, 1000.0f, 0.0f, 0.2f), 8000.0) / 0.1;
	CHECK(pass > 0.9);
	// Two octaves above cutoff ~ -48 dB, three ~ -72 dB, give or take.
	CHECK(20.0 * std::log10(one_oct) < -40.0);
	CHECK(20.0 * std::log10(two_oct) - 20.0 * std::log10(one_oct) <
	      -18.0);  // steep: roughly 24 dB per further octave
}

TEST_CASE("resonance peaks at the cutoff and keeps the bass")
{
	const double flat = tone(filter_sine(1000.0f, 1000.0f, 0.0f, 0.2f), 1000.0);
	const double peaked =
	    tone(filter_sine(1000.0f, 1000.0f, 0.85f, 0.2f), 1000.0);
	CHECK(peaked > 3.0 * flat);
	// Passband compensation: high resonance still passes a good part of the
	// bass (an uncompensated ladder would be down to ~1/(1+k) = 0.2).
	const double bass =
	    tone(filter_sine(60.0f, 1000.0f, 0.85f, 0.2f), 60.0) / 0.1;
	CHECK(bass > 0.4);
}

TEST_CASE("full resonance self-oscillates at the cutoff, at any cutoff")
{
	for(float fc : {100.0f, 800.0f, 3000.0f, 10000.0f})
	{
		CAPTURE(fc);
		Ladder l;
		std::vector<float> x(48000, 0.0f);
		x[0] = 0.01f;  // a small kick to start it
		const float g = Ladder::coefficient(fc);
		for(int i = 0; i < 48000; i += kBlockSize)
		{
			l.process(&x[i], kBlockSize, g, g, Ladder::feedback(1.0f, g), 1.0f,
			          1.0f);
		}
		const std::vector<float> tail(x.begin() + 36000, x.end());
		CHECK(all_finite(tail));
		CHECK(rms(tail) > 0.08);    // ringing on its own
		CHECK(peak(tail) <= 1.0f);  // soft-limited, not exploding
		// Its pitch is the cutoff, within a few percent.
		int crossings = 0;
		for(std::size_t i = 1; i < tail.size(); ++i)
		{
			crossings += (tail[i - 1] < 0.0f && tail[i] >= 0.0f) ? 1 : 0;
		}
		const double hz = crossings * kSr / static_cast<double>(tail.size());
		CHECK(hz == doctest::Approx(fc).epsilon(0.02));
	}
}

TEST_CASE("just under the threshold the resonance rings out, at any cutoff")
{
	for(float fc : {100.0f, 3000.0f, 10000.0f})
	{
		CAPTURE(fc);
		Ladder l;
		std::vector<float> x(48000, 0.0f);
		x[0] = 0.5f;
		const float g = Ladder::coefficient(fc);
		for(int i = 0; i < 48000; i += kBlockSize)
		{
			l.process(&x[i], kBlockSize, g, g, Ladder::feedback(0.85f, g), 1.0f,
			          1.0f);
		}
		const std::vector<float> tail(x.begin() + 36000, x.end());
		CHECK(rms(tail) < 1e-4);
	}
}

TEST_CASE("drive adds harmonics; a clean input stays clean")
{
	// 200 Hz sine, filter wide open: compare the 3rd harmonic.
	const std::vector<float> soft =
	    filter_sine(200.0f, 18000.0f, 0.0f, 0.1f, 1.0f);
	const std::vector<float> hard =
	    filter_sine(200.0f, 18000.0f, 0.0f, 6.0f, 1.0f);
	const double h3_soft = tone(soft, 600.0) / tone(soft, 200.0);
	const double h3_hard = tone(hard, 600.0) / tone(hard, 200.0);
	CHECK(h3_soft < 0.001);
	CHECK(h3_hard > 0.05);
	CHECK(peak(hard) <= 1.0f);  // the output stage's tanh
	CHECK(all_finite(hard));
}

// --- Envelope ----------------------------------------------------------------

TEST_CASE("the envelope attacks in its set time and releases to idle")
{
	const Envelope::Coeffs c = Envelope::Coeffs::from(0.3f, 0.3f, 0.5f, 0.2f);
	Envelope e;
	e.gate_on();
	const float ta = Envelope::attack_seconds(0.3f);
	int n = 0;
	while(e.stage() == Envelope::Stage::Attack && n < 480000)
	{
		e.process(c);
		++n;
	}
	CHECK(static_cast<float>(n) / kSr == doctest::Approx(ta).epsilon(0.02));
	CHECK(e.level() == 1.0f);

	for(int i = 0; i < 4 * 48000; ++i)
	{
		e.process(c);
	}
	CHECK(e.level() == doctest::Approx(0.5f).epsilon(0.01));  // sustain

	e.gate_off();
	for(int i = 0; i < 4 * 48000 && !e.idle(); ++i)
	{
		e.process(c);
	}
	CHECK(e.idle());
	CHECK(e.level() == 0.0f);
}

TEST_CASE("a retriggered envelope restarts from its current level")
{
	const Envelope::Coeffs c = Envelope::Coeffs::from(0.5f, 0.3f, 0.8f, 0.5f);
	Envelope e;
	e.gate_on();
	for(int i = 0; i < 2000; ++i)
	{
		e.process(c);
	}
	const float before = e.level();
	e.gate_on();
	CHECK(e.process(c) >= before);  // no drop to zero
}

// --- Engine ----------------------------------------------------------------

TEST_CASE("an idle engine is exact silence")
{
	init_table();
	TestPatch tp;
	Engine e;
	const std::vector<float> x = render_engine(e, tp.p, 4800);
	CHECK(peak(x) == 0.0f);
	CHECK(e.active() == 0);
}

TEST_CASE("a held note sounds at its pitch; releasing it goes idle")
{
	init_table();
	TestPatch tp(sp::Basic::Sine);
	Engine e;
	e.note_on(69, 127, 0);  // A4
	const std::vector<float> x = render_engine(e, tp.p, 48000);
	CHECK(tone(x, 440.0) > 0.1);
	CHECK(tone(x, 466.16) < 0.01 * tone(x, 440.0));
	CHECK(e.active() == 1);

	e.note_off(69, 0);
	render_engine(e, tp.p, 48000);
	CHECK(e.active() == 0);
}

TEST_CASE("oscillator pitch and octave offsets move the pitch")
{
	init_table();
	TestPatch tp(sp::Basic::Sine);
	tp.p.mix = 0.0f;                // osc 1 only
	tp.p.osc_semis[0] = 12.0f + 7;  // an octave and a fifth up
	Engine e;
	e.note_on(57, 127, 0);  // A3 (220 Hz) -> E5 (659.26 Hz)
	const std::vector<float> x = render_engine(e, tp.p, 48000);
	CHECK(tone(x, 659.26) > 0.1);
	CHECK(tone(x, 220.0) < 0.01);
}

TEST_CASE("six voices play at once; a seventh steals the oldest")
{
	init_table();
	TestPatch tp;
	Engine e;
	for(int i = 0; i < 6; ++i)
	{
		e.note_on(static_cast<std::uint8_t>(60 + i), 100, 0);
	}
	render_engine(e, tp.p, 640);
	CHECK(e.active() == 6);

	e.note_on(80, 100, 0);
	int has80 = 0;
	int has60 = 0;
	for(int i = 0; i < Engine::kVoices; ++i)
	{
		has80 += (e.voice(i).note() == 80) ? 1 : 0;
		has60 += (e.voice(i).note() == 60 && e.voice(i).gate()) ? 1 : 0;
	}
	CHECK(has80 == 1);
	CHECK(has60 == 0);  // 60 was the oldest
	CHECK(e.active() == 6);
}

TEST_CASE("a releasing voice is stolen before a held one")
{
	init_table();
	TestPatch tp;
	tp.p.release = 0.9f;  // long tails
	Engine e;
	for(int i = 0; i < 6; ++i)
	{
		e.note_on(static_cast<std::uint8_t>(60 + i), 100, 0);
	}
	render_engine(e, tp.p, 640);
	e.note_off(64, 0);  // not the oldest, but releasing
	render_engine(e, tp.p, 640);
	e.note_on(90, 100, 0);
	bool held60 = false;
	bool has64 = false;
	for(int i = 0; i < Engine::kVoices; ++i)
	{
		held60 = held60 || (e.voice(i).note() == 60 && e.voice(i).gate());
		has64 = has64 || e.voice(i).note() == 64;
	}
	CHECK(held60);
	CHECK_FALSE(has64);
}

TEST_CASE("note-offs only release voices of the same source")
{
	init_table();
	TestPatch tp;
	Engine e;
	e.note_on(60, 100, 0);  // keyboard
	e.note_on(60, 100, 1);  // sequencer, same note
	CHECK(e.active() == 2);
	e.note_off(60, 1);
	int gated = 0;
	for(int i = 0; i < Engine::kVoices; ++i)
	{
		if(e.voice(i).gate())
		{
			++gated;
			CHECK(e.voice(i).tag() == 0);
		}
	}
	CHECK(gated == 1);

	e.release_all(0);
	render_engine(e, tp.p, 48000);
	CHECK(e.active() == 0);
}

TEST_CASE("a full chord stack stays finite and inside full scale")
{
	init_table();
	TestPatch tp(sp::Basic::Saw);
	tp.p.cutoff = 0.6f;
	tp.p.resonance = 0.9f;
	tp.p.drive = 1.0f;
	Engine e;
	const std::uint8_t notes[] = {36, 48, 52, 55, 59, 62};
	for(std::uint8_t n : notes)
	{
		e.note_on(n, 127, 0);
	}
	const std::vector<float> x = render_engine(e, tp.p, 48000);
	CHECK(all_finite(x));
	CHECK(peak(x) < 2.5f);  // the master soft clip takes the rest
	CHECK(rms(x) > 0.05);
}

TEST_CASE("changing a slot's spectrum mid-note glides instead of jumping")
{
	init_table();
	TestPatch tp(sp::Basic::Sine);
	tp.p.mix = 0.0f;
	Engine e;
	e.note_on(57, 127, 0);
	std::vector<float> x = render_engine(e, tp.p, 4800);
	// Swap osc 1's slot A to a square: the next block must not step.
	sp::make_basic(sp::Basic::Square, tp.spec[0][0]);
	std::vector<float> y = render_engine(e, tp.p, 64);
	float max_step = std::fabs(y[0] - x.back());
	for(std::size_t i = 1; i < y.size(); ++i)
	{
		max_step = std::max(max_step, std::fabs(y[i] - y[i - 1]));
	}
	// 220 Hz sine at voice gain 0.3 moves ~0.009 per sample; a hard switch
	// to a square would jump by tenths.
	CHECK(max_step < 0.05f);
}

TEST_CASE("the modulation matrix routes the envelope to the cutoff")
{
	init_table();
	TestPatch tp(sp::Basic::Saw);
	tp.p.cutoff = 0.2f;  // ~80 Hz: dark
	tp.p.mix = 0.0f;
	Engine dark;
	dark.note_on(57, 127, 0);
	const std::vector<float> a = render_engine(dark, tp.p, 24000);

	tp.p.routes[0] = ModRoute{static_cast<std::uint8_t>(ModSrc::AmpEnv),
	                          static_cast<std::uint8_t>(ModDst::Cutoff), 0.7f};
	Engine bright;
	bright.note_on(57, 127, 0);
	const std::vector<float> b = render_engine(bright, tp.p, 24000);
	// The 8th harmonic (1760 Hz) only gets through with the envelope open.
	CHECK(tone(b, 1760.0) > 10.0 * tone(a, 1760.0));
}

TEST_CASE("a render call longer than one block is cut into blocks")
{
	// audio_out::start() pre-fills the whole DMA buffer (2 blocks) in one
	// call; the engine's scratch only holds one block.
	init_table();
	TestPatch tp;
	Engine a;
	Engine b;
	for(Engine* e : {&a, &b})
	{
		e->note_on(48, 100, 0);
		e->note_on(55, 100, 0);
	}
	std::vector<float> big(3 * kBlockSize + 17, 0.0f);
	a.render(tp.p, big.data(), static_cast<int>(big.size()));
	// The same as rendering it block by block.
	std::vector<float> ref(big.size());
	for(std::size_t i = 0; i < ref.size(); i += kBlockSize)
	{
		const int len =
		    static_cast<int>(std::min<std::size_t>(kBlockSize, ref.size() - i));
		b.render(tp.p, &ref[i], len);
	}
	CHECK(big == ref);
	CHECK(all_finite(big));
}

// --- LFO -----------------------------------------------------------------------

TEST_CASE("the LFO shapes have the right values through a cycle")
{
	using S = dsp::LfoShape;
	CHECK(Lfo::shape_at(S::Sine, 0.25f, 0) == doctest::Approx(1.0f));
	CHECK(Lfo::shape_at(S::Sine, 0.75f, 0) == doctest::Approx(-1.0f));
	CHECK(Lfo::shape_at(S::Triangle, 0.0f, 0) == doctest::Approx(0.0f));
	CHECK(Lfo::shape_at(S::Triangle, 0.25f, 0) == doctest::Approx(1.0f));
	CHECK(Lfo::shape_at(S::Triangle, 0.5f, 0) == doctest::Approx(0.0f));
	CHECK(Lfo::shape_at(S::Triangle, 0.75f, 0) == doctest::Approx(-1.0f));
	CHECK(Lfo::shape_at(S::Saw, 0.0f, 0) == doctest::Approx(-1.0f));
	CHECK(Lfo::shape_at(S::Saw, 0.5f, 0) == doctest::Approx(0.0f));
	CHECK(Lfo::shape_at(S::Square, 0.2f, 0) == 1.0f);
	CHECK(Lfo::shape_at(S::Square, 0.7f, 0) == -1.0f);
	CHECK(Lfo::shape_at(S::SampleHold, 0.3f, 0.42f) == 0.42f);
}

TEST_CASE("a synced LFO is locked to the song position")
{
	// 1/4 = 4 steps per cycle: step 1 is a quarter through, step 6 half way
	// through the second cycle - whatever happened before.
	Lfo l;
	CHECK(l.lock(1.0f, 4.0f, dsp::LfoShape::Saw) == doctest::Approx(-0.5f));
	CHECK(l.lock(6.0f, 4.0f, dsp::LfoShape::Saw) == doctest::Approx(0.0f));
	CHECK(l.lock(16.0f, 16.0f, dsp::LfoShape::Saw) == doctest::Approx(-1.0f));
	// 2 bars: step 16 is half way.
	CHECK(l.lock(16.0f, 32.0f, dsp::LfoShape::Square) == -1.0f);
}

TEST_CASE("S&H picks a new level once per cycle, free or synced")
{
	Lfo l;
	const float a = l.lock(0.5f, 4.0f, dsp::LfoShape::SampleHold);
	CHECK(l.lock(3.9f, 4.0f, dsp::LfoShape::SampleHold) == a);  // held
	const float b = l.lock(4.1f, 4.0f, dsp::LfoShape::SampleHold);
	CHECK(b != a);
	CHECK(b >= -1.0f);
	CHECK(b <= 1.0f);

	// Free: 10 Hz -> a new level every 4800 frames.
	Lfo f;
	std::vector<float> seen;
	for(int i = 0; i < 48000; i += 64)
	{
		const float v = f.tick(10.0f, 64, dsp::LfoShape::SampleHold);
		if(seen.empty() || v != seen.back())
		{
			seen.push_back(v);
		}
	}
	CHECK(seen.size() >= 9);
	CHECK(seen.size() <= 11);
}

TEST_CASE("a synced LFO runs at the musical rate while stopped")
{
	// 1/4 at 120 BPM = 2 Hz: half a second per cycle.
	CHECK(Lfo::synced_hz(4.0f, 120.0f) == doctest::Approx(2.0f));
	CHECK(Lfo::synced_hz(64.0f, 120.0f) == doctest::Approx(0.125f));
	Lfo l;
	int wraps = 0;
	float prev = -2.0f;
	for(int i = 0; i < 52800; i += 64)  // 1.1 s: wraps at 0.5 and 1.0 s
	{
		const float v =
		    l.tick(Lfo::synced_hz(4.0f, 120.0f), 64, dsp::LfoShape::Saw);
		wraps += v < prev ? 1 : 0;
		prev = v;
	}
	CHECK(wraps == 2);
}

TEST_CASE("the engine's LFO follows shape, mode and song position")
{
	init_table();
	TestPatch tp(sp::Basic::Sine);
	tp.p.mix = 0.0f;
	tp.p.lfo_shape[0] = static_cast<std::uint8_t>(dsp::LfoShape::Square);
	tp.p.lfo_sync[0] = true;
	tp.p.lfo_div[0] = 5;  // 1/4
	tp.p.routes[0] = ModRoute{static_cast<std::uint8_t>(ModSrc::Lfo1),
	                          static_cast<std::uint8_t>(ModDst::Pitch), 1.0f};
	// First half of the cycle: square = +1 -> +12 semitones; second half -1.
	Engine up;
	tp.p.seq_pos = 1.0f;
	up.note_on(57, 127, 0);
	const std::vector<float> a = render_engine(up, tp.p, 24000);
	CHECK(tone(a, 440.0) > 10.0 * tone(a, 220.0));
	Engine down;
	tp.p.seq_pos = 3.0f;
	down.note_on(57, 127, 0);
	const std::vector<float> b = render_engine(down, tp.p, 24000);
	CHECK(tone(b, 110.0) > 10.0 * tone(b, 220.0));
}

TEST_CASE("the UI's LFO name lists match the engine's tables")
{
	const params::Spec& len = params::spec(params::Id::Lfo1Div);
	CHECK(static_cast<int>(len.max) + 1 == dsp::kLfoDivCount);
	const params::Spec& shp = params::spec(params::Id::Lfo1Shape);
	CHECK(static_cast<int>(shp.max) + 1 ==
	      static_cast<int>(dsp::LfoShape::Count));
}

TEST_CASE("the engine reports the shape and mix as modulated, for the scope")
{
	init_table();
	TestPatch tp(sp::Basic::Saw);
	tp.p.shape[0] = 0.2f;
	tp.p.shape[1] = 0.6f;
	tp.p.mix = 0.5f;
	// A synced square LFO on SHP (+0.5) and on MIX (-0.25).
	tp.p.lfo_shape[0] = static_cast<std::uint8_t>(dsp::LfoShape::Square);
	tp.p.lfo_sync[0] = true;
	tp.p.lfo_div[0] = 5;  // 1/4: steps 0..2 high, 2..4 low
	tp.p.routes[0] = ModRoute{static_cast<std::uint8_t>(ModSrc::Lfo1),
	                          static_cast<std::uint8_t>(ModDst::Shape), 0.5f};
	tp.p.routes[1] = ModRoute{static_cast<std::uint8_t>(ModSrc::Lfo1),
	                          static_cast<std::uint8_t>(ModDst::Mix), -0.25f};
	Engine e;
	float buf[kBlockSize];
	tp.p.seq_pos = 1.0f;  // LFO +1
	e.render(tp.p, buf, kBlockSize);
	CHECK(e.live_shape(0) == doctest::Approx(0.7f));
	CHECK(e.live_shape(1) == doctest::Approx(1.0f));  // clamped
	CHECK(e.live_mix() == doctest::Approx(0.25f));
	tp.p.seq_pos = 3.0f;  // LFO -1
	e.render(tp.p, buf, kBlockSize);
	CHECK(e.live_shape(0) == doctest::Approx(0.0f));   // clamped
	CHECK(e.live_shape(1) == doctest::Approx(0.1f));
	CHECK(e.live_mix() == doctest::Approx(0.75f));

	// ENV -> SHP uses the newest sounding voice's envelope; none: no mod.
	TestPatch te(sp::Basic::Saw);
	te.p.shape[0] = 0.1f;
	te.p.routes[0] = ModRoute{static_cast<std::uint8_t>(ModSrc::AmpEnv),
	                          static_cast<std::uint8_t>(ModDst::Shape), 0.5f};
	Engine f;
	f.render(te.p, buf, kBlockSize);
	CHECK(f.live_shape(0) == doctest::Approx(0.1f));
	f.note_on(60, 100, 0);
	render_engine(f, te.p, 4800);  // sustain 1: envelope at the top
	CHECK(f.live_shape(0) == doctest::Approx(0.6f).epsilon(0.01));
}

// --- PWM ----------------------------------------------------------------------

namespace
{

// Render a spectrum with PW through the oscillator.
std::vector<float> render_pw(const float* w, float pw, float hz, int n)
{
	float zero[sp::kHarmonics] = {};
	AdditiveOsc osc;
	std::vector<float> out(static_cast<std::size_t>(n));
	for(int i = 0; i < n; i += kBlockSize)
	{
		osc.render_pw(w, zero, sp::kHarmonics, 0.0f, 0.0f, pw, hz / kSr,
		              &out[i], std::min(kBlockSize, n - i));
	}
	return out;
}

}  // namespace

TEST_CASE("PWM is the wave minus a shifted copy, for any spectrum")
{
	init_table();
	for(float pw : {0.05f, 0.1f, 0.4f, 0.9f})
	{
		for(std::uint32_t seed : {3u, 4242u})
		{
			CAPTURE(pw);
			CAPTURE(seed);
			float w[sp::kHarmonics];
			sp::make_random(seed, w);
			// 100 Hz: one cycle is 480 samples.
			const std::vector<float> got = render_pw(w, pw, 100.0f, 480);
			std::vector<float> cyc(480);
			sp::render_cycle(w, cyc.data(), 480);
			float amount;
			float width;
			pw_map(pw, amount, width);
			const int shift = static_cast<int>(width * 480.0f + 0.5f);
			double worst = 0.0;
			for(int i = 0; i < 480; ++i)
			{
				// Compare at whole-sample shifts only.
				if(std::fabs(width * 480.0f - static_cast<float>(shift)) > 1e-3f)
				{
					break;
				}
				const double want =
				    (cyc[i] - amount * cyc[(i + shift) % 480]) / (1.0 + amount);
				worst = std::max(worst, std::fabs(got[i] - want));
			}
			CHECK(worst < 5e-4);
		}
	}
}

TEST_CASE("PWM on a saw is a pulse: 50 % has no even harmonics")
{
	init_table();
	float saw[sp::kHarmonics];
	sp::make_basic(sp::Basic::Saw, saw);
	const std::vector<float> sq = render_pw(saw, 0.1f, 200.0f, 48000);  // 50 %
	CHECK(tone(sq, 400.0) < 1e-3);  // 2nd harmonic gone
	CHECK(tone(sq, 600.0) > 0.05);  // 3rd there
	// 25 % width: the 4th harmonic vanishes, the 2nd is back.
	float amount;
	float width;
	const float pw25 = 0.1f + 0.9f * (0.5f - 0.25f) / 0.45f;
	pw_map(pw25, amount, width);
	REQUIRE(width == doctest::Approx(0.25f));
	const std::vector<float> p25 = render_pw(saw, pw25, 200.0f, 48000);
	CHECK(tone(p25, 800.0) < 1e-3);
	CHECK(tone(p25, 400.0) > 0.05);
	CHECK(peak(p25) <= 1.0f);
}

TEST_CASE("PW knob mapping: off, fade-in at 50 %, then narrower")
{
	float a;
	float w;
	pw_map(0.0f, a, w);
	CHECK(a == 0.0f);
	pw_map(0.05f, a, w);
	CHECK(a == doctest::Approx(0.5f));
	CHECK(w == 0.5f);
	pw_map(1.0f, a, w);
	CHECK(a == 1.0f);
	CHECK(w == doctest::Approx(0.05f));
}

TEST_CASE("the engine plays PWM and LFO -> PW moves it")
{
	init_table();
	TestPatch tp(sp::Basic::Saw);
	tp.p.mix = 0.0f;
	tp.p.pw[0] = 0.1f;  // 50 %: a square from the saw
	Engine e;
	e.note_on(57, 127, 0);  // 220 Hz
	const std::vector<float> sq = render_engine(e, tp.p, 24000);
	CHECK(tone(sq, 440.0) < 0.02 * tone(sq, 660.0));
	// PW routed from a synced square LFO: +0.9 narrows it to 5 %, where the
	// 2nd harmonic is back.
	tp.p.lfo_shape[0] = static_cast<std::uint8_t>(dsp::LfoShape::Square);
	tp.p.lfo_sync[0] = true;
	tp.p.seq_pos = 1.0f;  // LFO = +1
	tp.p.routes[0] = ModRoute{static_cast<std::uint8_t>(ModSrc::Lfo1),
	                          static_cast<std::uint8_t>(ModDst::Pw), 0.9f};
	Engine f;
	f.note_on(57, 127, 0);
	const std::vector<float> narrow = render_engine(f, tp.p, 24000);
	CHECK(tone(narrow, 440.0) > 0.2 * tone(narrow, 220.0));
	CHECK(f.live_pw(0) == doctest::Approx(1.0f));
}

TEST_CASE("per-oscillator destinations only move their own oscillator")
{
	init_table();
	auto route = [](ModDst dst, float amount)
	{
		return ModRoute{static_cast<std::uint8_t>(ModSrc::Lfo1),
		                static_cast<std::uint8_t>(dst), amount};
	};
	// A synced square LFO held at +1 (song position 1 of a 1/4 cycle).
	auto patch = [](TestPatch& tp)
	{
		tp.p.lfo_shape[0] = static_cast<std::uint8_t>(dsp::LfoShape::Square);
		tp.p.lfo_sync[0] = true;
		tp.p.lfo_div[0] = 5;
		tp.p.seq_pos = 1.0f;
	};

	// PT2 +1/12 = +1 semitone on osc 2 only: osc 1 stays on A3.
	TestPatch tp(sp::Basic::Sine);
	patch(tp);
	tp.p.routes[0] = route(ModDst::Pitch2, 1.0f / 12.0f);
	Engine e;
	e.note_on(57, 127, 0);
	const std::vector<float> x = render_engine(e, tp.p, 48000);
	CHECK(tone(x, 220.0) > 0.05);       // osc 1 untouched
	CHECK(tone(x, 233.08) > 0.05);      // osc 2 a semitone up
	CHECK(tone(x, 220.0 * 0.5) < 0.01);

	// SH1 and the "both" SHP add up for osc 1; osc 2 gets only SHP.
	TestPatch ts(sp::Basic::Saw);
	patch(ts);
	ts.p.routes[0] = route(ModDst::Shape, 0.2f);
	ts.p.routes[1] = route(ModDst::Shape1, 0.3f);
	ts.p.routes[2] = route(ModDst::Pw2, 0.4f);
	Engine f;
	float buf[kBlockSize];
	f.render(ts.p, buf, kBlockSize);
	CHECK(f.live_shape(0) == doctest::Approx(0.5f));
	CHECK(f.live_shape(1) == doctest::Approx(0.2f));
	CHECK(f.live_pw(0) == doctest::Approx(0.0f));
	CHECK(f.live_pw(1) == doctest::Approx(0.4f));
}

// --- Effects --------------------------------------------------------------------

#include "dsp/fx.hpp"

TEST_CASE("the delay repeats at its time, ping-pong alternates sides")
{
	static std::int16_t l[48000];
	static std::int16_t r[48000];
	Delay d;
	d.init(l, r, 48000);
	DelayParams p;
	p.seconds = 0.1f;  // 4800 samples
	p.feedback = 0.5f;
	p.tone = 1.0f;
	p.ping = true;
	std::vector<float> in(24000, 0.0f), ol(24000), orr(24000);
	in[0] = 0.5f;
	d.process(p, in.data(), ol.data(), orr.data(), 24000);
	CHECK(std::fabs(ol[4800]) > 0.3f);   // first repeat: left
	CHECK(std::fabs(orr[4800]) < 0.01f);
	CHECK(std::fabs(orr[9600]) > 0.1f);  // second: right
	CHECK(std::fabs(ol[9600]) < 0.01f);
}

TEST_CASE("the reverb tail is stereo, decays and stays bounded")
{
	static Reverb rv;
	rv.init();
	ReverbParams p;
	p.decay = 0.7f;
	std::vector<float> in(96000, 0.0f), ol(96000), orr(96000);
	in[0] = 0.8f;
	rv.process(p, in.data(), in.data(), ol.data(), orr.data(), 96000);
	CHECK(all_finite(ol));
	CHECK(peak(ol) < 1.5f);
	const std::vector<float> early(ol.begin() + 4800, ol.begin() + 24000);
	const std::vector<float> late(ol.begin() + 72000, ol.end());
	CHECK(rms(early) > 1e-3);
	CHECK(rms(late) < rms(early));
	CHECK(ol != orr);  // decorrelated sides
}

// --- MOD FX and the chain -------------------------------------------------------

namespace
{

// Run ModFx over white-ish noise; return the two wet channels.
void run_modfx(const ModFxParams& p, int n, std::vector<float>& l,
               std::vector<float>& r)
{
	static ModFx fx;
	fx.init();
	std::vector<float> in(static_cast<std::size_t>(n));
	std::uint32_t s = 1u;
	for(float& v : in)
	{
		s = s * 1664525u + 1013904223u;
		v = (static_cast<float>(s >> 9) / 4194304.0f - 1.0f) * 0.5f;
	}
	l.assign(static_cast<std::size_t>(n), 0.0f);
	r.assign(static_cast<std::size_t>(n), 0.0f);
	for(int i = 0; i < n; i += kBlockSize)
	{
		fx.process(p, &in[i], &l[i], &r[i], std::min(kBlockSize, n - i));
	}
}

}  // namespace

TEST_CASE("phaser, flanger and chorus stay finite, bounded and stereo")
{
	init_table();
	for(int t = 0; t < static_cast<int>(dsp::ModFxType::Count); ++t)
	{
		CAPTURE(t);
		ModFxParams p;
		p.type = static_cast<dsp::ModFxType>(t);
		p.rate = 0.6f;
		p.depth = 1.0f;
		p.feedback = 1.0f;  // the worst case
		std::vector<float> l;
		std::vector<float> r;
		run_modfx(p, 48000, l, r);
		CHECK(all_finite(l));
		CHECK(all_finite(r));
		CHECK(peak(l) < 4.0f);
		CHECK(rms(l) > 0.05);
		CHECK(l != r);  // the quadrature LFO makes the sides differ
	}
}

TEST_CASE("the flanger is a delayed copy at its swept time")
{
	init_table();
	// An impulse, LFO frozen at its start (rate ~0): the wet output is the
	// impulse delayed by the base 1 ms plus half the sweep (sine at phase 0).
	static ModFx fx;
	fx.init();
	ModFxParams p;
	p.type = dsp::ModFxType::Flanger;
	p.rate = 0.0f;
	p.depth = 1.0f;
	p.feedback = 0.5f;  // none
	std::vector<float> in(4800, 0.0f);
	std::vector<float> l(4800);
	std::vector<float> r(4800);
	in[0] = 1.0f;
	for(int i = 0; i < 4800; i += kBlockSize)
	{
		fx.process(p, &in[i], &l[i], &r[i], kBlockSize);
	}
	const int want = static_cast<int>(0.001f * kSr + 0.5f * 0.005f * kSr);
	int at = 0;
	for(int i = 1; i < 4800; ++i)
	{
		at = std::fabs(l[i]) > std::fabs(l[at]) ? i : at;
	}
	CHECK(std::abs(at - want) <= 2);
}

TEST_CASE("with every MIX at 0 the chain passes the dry signal untouched")
{
	init_table();
	static std::int16_t dl[48000];
	static std::int16_t dr[48000];
	static Effects fx;
	fx.init(dl, dr, 48000);
	FxParams p;  // all mixes default to 0
	std::vector<float> in(4800);
	for(int i = 0; i < 4800; ++i)
	{
		in[i] = 0.5f * std::sin(0.01f * static_cast<float>(i));
	}
	std::vector<float> l(4800);
	std::vector<float> r(4800);
	fx.process(p, in.data(), l.data(), r.data(), 4800);
	CHECK(l == in);
	CHECK(r == in);

	// With the delay up, the sides differ (ping-pong) and something echoes
	// after the input stops.
	p.delay_mix = 0.8f;
	p.delay.ping = true;
	p.delay.seconds = 0.05f;
	p.reverb_mix = 0.3f;
	std::vector<float> quiet(9600, 0.0f);
	std::vector<float> l2(9600);
	std::vector<float> r2(9600);
	fx.process(p, in.data(), l2.data(), r2.data(), 4800);
	fx.process(p, quiet.data(), l2.data() + 4800, r2.data() + 4800, 4800);
	const std::vector<float> tail(l2.begin() + 4800, l2.end());
	CHECK(rms(tail) > 1e-3);
	CHECK(l2 != r2);
}

TEST_CASE("the UI's FX name lists match the engine's tables")
{
	CHECK(static_cast<int>(params::spec(params::Id::DelayDiv).max) + 1 ==
	      dsp::kDelayDivCount);
	CHECK(static_cast<int>(params::spec(params::Id::ModFxDiv).max) + 1 ==
	      dsp::kLfoDivCount);
	CHECK(static_cast<int>(params::spec(params::Id::ModFxType).max) + 1 ==
	      static_cast<int>(dsp::ModFxType::Count));
}

// --- Scope capture ---------------------------------------------------------------

#include "dsp/scope_tap.hpp"

TEST_CASE("the scope tap triggers on a rising zero crossing")
{
	static dsp::ScopeTap tap;
	// A sine starting at a peak: the first rising zero crossing is 3/4 of a
	// cycle in.
	const float period = 200.0f;
	std::vector<float> x(4000);
	for(int i = 0; i < 4000; ++i)
	{
		x[i] = std::cos(2.0f * 3.14159265f * static_cast<float>(i) / period);
	}
	CHECK_FALSE(tap.ready());
	tap.feed(x.data(), 4000, period);
	REQUIRE(tap.ready());
	CHECK(tap.period() == period);
	std::vector<float> cap(dsp::ScopeTap::kSize);
	tap.take(cap.data());
	CHECK_FALSE(tap.ready());       // re-armed
	CHECK(cap[0] >= 0.0f);          // starts at the crossing ...
	CHECK(cap[0] < 0.05f);
	CHECK(cap[10] > cap[0]);        // ... going up
	CHECK(cap[50] == doctest::Approx(1.0f).epsilon(0.01));  // a quarter on
	CHECK(tap.count() == 1);
}

TEST_CASE("the scope tap free-runs when nothing crosses zero")
{
	static dsp::ScopeTap tap;
	std::vector<float> dc(dsp::ScopeTap::kMaxWait + dsp::ScopeTap::kSize + 64,
	                      0.25f);
	tap.feed(dc.data(), static_cast<int>(dc.size()), 0.0f);
	REQUIRE(tap.ready());
	CHECK(tap.period() == 0.0f);
}
