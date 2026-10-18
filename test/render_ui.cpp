// SPDX-License-Identifier: MIT
//
// Screenshot the UI on the desktop: drive the debug page into a few states and
// dump the framebuffer as binary PPM images, so layout changes can be looked
// at before flashing.
//
//   ./render_ui [prefix]
//
// Writes <prefix>_main.ppm and <prefix>_picker.ppm (default prefix "ui").
// Convert with e.g. `magick ui_main.ppm ui_main.png`.
#include <cstdio>
#include <string>

#include "display.hpp"
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
	params.set(params::Id::Osc1Shape, 0.4f);
	params.set(params::Id::Osc2Pitch, 7.0f);
	params.set(params::Id::Osc2Octave, -1.0f);
	params.set(params::Id::OscMix, 0.3f);
	params.set(params::Id::EnvDecay, 0.7f);
	params.set(params::Id::Lfo1Dest,
	           static_cast<float>(params::LfoDest::Pitch));
	params.set(params::Id::Lfo1Amount, 0.25f);
	params.set(params::Id::Lfo2Dest,
	           static_cast<float>(params::LfoDest::Cutoff));
	params.set(params::Id::Lfo2Amount, -0.6f);
	const int on[] = {0, 3, 6, 8, 11, 14};
	const std::uint8_t notes[] = {48, 55, 60, 51, 63, 58};
	for(int i = 0; i < 6; ++i)
	{
		pattern.steps[on[i]].on = true;
		pattern.steps[on[i]].note = notes[i];
	}

	sequencer::Transport transport;
	ui::Ui ui(params, pattern, transport);
	ui.redraw_all();
	ui.set_cpu_mhz(480);
	ui.set_cpu_load(0.123f, 0.187f);

	std::uint32_t now = 0;
	auto frame = [&](bool pressed, int x, int y)
	{
		now += 20;
		ui.update(ui::Touch{pressed, x, y}, now);
	};

	// Select step 7 with a double tap (toggle off and on again), and show the
	// sequencer playing step 4.
	for(int i = 0; i < 2; ++i)
	{
		frame(true, 160 + 2 * 80 + 40, 35 + 1 * 59 + 29);
		frame(false, 0, 0);
	}
	transport.run = true;  // as if play was pressed and the clock is on step 4
	transport.step = 3;
	frame(false, 0, 0);

	bool ok = write_ppm(prefix + "_main.ppm");

	// Long press step 12 to bring up the note picker.
	for(int i = 0; i < 30; ++i)
	{
		frame(true, 160 + 3 * 80 + 40, 35 + 2 * 59 + 29);
	}
	frame(false, 0, 0);
	ok = write_ppm(prefix + "_picker.ppm") && ok;

	if(!ok)
	{
		std::fprintf(stderr, "render_ui: could not write images\n");
		return 1;
	}
	std::printf("render_ui: wrote %s_main.ppm and %s_picker.ppm\n",
	            prefix.c_str(), prefix.c_str());
	return 0;
}
