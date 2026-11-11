// SPDX-License-Identifier: MIT
//
// Screenshot the UI on the desktop: put the pages into a few states and dump
// the framebuffer as one binary PPM per page, so layout changes can be looked
// at before flashing.
//
//   ./render_ui [prefix]
//
// Writes <prefix>_{osc,env,mod,fx,seq,sync,midi,picker,chords}.ppm (default
// prefix "ui").
// Convert with e.g. `magick ui_seq.ppm ui_seq.png`.
#include <cstdio>
#include <string>

#include "display.hpp"
#include "sequencer/chords.hpp"
#include "ui/ui.hpp"

namespace
{

bool write_ppm(const std::string& path)
{
	std::FILE* f = std::fopen(path.c_str(), "wb");
	if(f == nullptr)
	{
		return false;
	}
	std::fprintf(f, "P6\n%d %d\n255\n", display::kWidth, display::kHeight);
	for(int i = 0; i < display::kWidth * display::kHeight; ++i)
	{
		// Expand RGB565 back to 8 bits per channel.
		const std::uint16_t px = display::detail::g_framebuffer[i];
		const unsigned char rgb[3] = {
		    static_cast<unsigned char>(((px >> 11) & 0x1F) * 255 / 31),
		    static_cast<unsigned char>(((px >> 5) & 0x3F) * 255 / 63),
		    static_cast<unsigned char>((px & 0x1F) * 255 / 31),
		};
		std::fwrite(rgb, 1, 3, f);
	}
	return std::fclose(f) == 0;
}

}  // namespace

int main(int argc, char** argv)
{
	const std::string prefix = (argc > 1) ? argv[1] : "ui";

	params::Parameters params;
	sequencer::Pattern pattern;

	// Some non-default state so every widget shows something.
	params.set(params::Id::Osc1Shape, 0.6f);
	params.set(params::Id::Osc2Pitch, 7.0f);
	params.set(params::Id::Osc2Octave, -1.0f);
	params.set(params::Id::OscMix, 0.3f);
	params.set(params::Id::Osc2Pw, 0.6f);  // osc 2: a ~25 % pulse
	params.set(params::Id::EnvDecay, 0.7f);
	// LFO 1 synced (1/8 dotted) with S&H, LFO 2 a free sine.
	params.set(params::Id::Lfo1Mode, 1.0f);
	params.set(params::Id::Lfo1Div, 7.0f);
	params.set(params::Id::Lfo1Shape, 4.0f);
	params.set(params::Id::Lfo2Shape, 0.0f);
	const int on[] = {0, 3, 6, 8, 11, 14};
	const std::uint8_t notes[] = {48, 55, 60, 51, 63, 58};
	for(int i = 0; i < 6; ++i)
	{
		pattern.steps[on[i]].on = true;
		pattern.steps[on[i]].note = notes[i];
	}
	// Chords on a few steps, and step 1 tied over steps 2 and 3.
	pattern.steps[0].chord = static_cast<std::uint8_t>(sequencer::Chord::Min7);
	pattern.steps[6].chord = static_cast<std::uint8_t>(sequencer::Chord::Maj9);
	pattern.steps[11].chord = static_cast<std::uint8_t>(sequencer::Chord::Sus4);
	pattern.steps[1].tie = true;
	pattern.steps[2].tie = true;

	sequencer::Transport transport;
	modulation::Matrix matrix;
	// Three live routes, so the MOD page shows live and idle rows.
	using modulation::Dest;
	matrix.rows[0] = {0, static_cast<int>(Dest::Pitch), 0.25f};    // LFO1
	matrix.rows[1] = {2, static_cast<int>(Dest::Cutoff), 0.6f};    // ENV
	matrix.rows[3] = {1, static_cast<int>(Dest::Shape2), -0.4f};   // LFO2
	params::WaveSlots waves;
	waves.randomize(0, 1, 1234);  // osc 1 morphs saw -> a random shape
	ui::Ui ui(params, pattern, transport, matrix, waves);
	ui.redraw_all();
	ui.set_cpu_load(0.123f, 0.187f);

	std::uint32_t now = 0;
	bool ok = true;
	auto frame = [&](bool pressed, int x, int y)
	{
		now += 20;
		ui.update(ui::Touch{pressed, x, y}, now);
	};
	// Centre of the tab-bar button for page `i`.
	auto tab = [&](int i)
	{
		frame(true, i * 48 + 24, 259);
		frame(false, 0, 0);
	};
	auto shot = [&](const char* name)
	{
		ok = write_ppm(prefix + "_" + name + ".ppm") && ok;
	};

	// As if play was pressed and the clock is on step 3.
	transport.run = true;
	transport.step = 3;
	frame(false, 0, 0);

	// SEQ: the tab starts on OSC; go to SEQ and tap step 7 to turn it on and
	// select it.
	tab(4);
	frame(true, 3 * 120 + 60, 35 + 52 + 26);
	frame(false, 0, 0);
	shot("seq");

	// Long press step 12 to bring up the note picker.
	for(int i = 0; i < 30; ++i)
	{
		frame(true, 60, 35 + 3 * 52 + 26);
	}
	frame(false, 0, 0);
	shot("picker");

	// The chord picker for the selected step (the first tap dismisses the
	// note picker still open from the shot above).
	frame(true, 400, 17);
	frame(false, 0, 0);
	frame(true, 208, 17);
	frame(false, 0, 0);
	shot("chords");
	frame(true, 400, 17);  // dismiss
	frame(false, 0, 0);

	tab(6);
	shot("midi");
	// SYNC with the clock input selected and a clock arriving.
	params.set(params::Id::ClockSource, 1.0f);
	transport.clock.level[0] = 0.62f;
	transport.clock.threshold[0] = 0.25f;
	transport.clock.edges[0] = 1234;
	transport.clock.level[1] = 0.01f;
	transport.clock.threshold[1] = 0.02f;
	transport.clock.bpm = 121.3f;
	tab(5);
	shot("sync");
	params.set(params::Id::ClockSource, 0.0f);
	tab(0);
	shot("osc");
	tab(1);
	shot("env");
	tab(2);
	shot("mod");
	// FX with all three effects in use, delay and mod FX synced.
	params.set(params::Id::ModFxType, 1.0f);
	params.set(params::Id::ModFxMix, 0.5f);
	params.set(params::Id::DelayMode, 1.0f);
	params.set(params::Id::DelayMix, 0.35f);
	params.set(params::Id::ReverbMix, 0.25f);
	tab(3);
	shot("fx");

	if(!ok)
	{
		std::fprintf(stderr, "render_ui: could not write images\n");
		return 1;
	}
	std::printf(
	    "render_ui: wrote %s_{osc,env,mod,fx,seq,sync,midi,picker,chords}.ppm\n",
	    prefix.c_str());
	return 0;
}
