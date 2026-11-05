// SPDX-License-Identifier: MIT
//
// Application entry point (C++). Called from the CubeMX-generated main() once
// the clocks and GPIOs are up.
//
// Two contexts, Mutable Instruments style (see ARCHITECTURE.md §3):
//   - The audio interrupt is the synth's real main loop. Once per block it will
//     read the controls, advance the sequencer clock and render the voices.
//   - The foreground loop below only does what is too slow or blocking for the
//     interrupt: polling touch over I2C and drawing the UI.
// They meet in the parameter set and the pattern: the UI writes them, the
// audio interrupt reads them.
//
// For now nothing makes sound: the DSP is still being written, so the audio
// interrupt streams silence and the UI edits parameters nobody reads yet.
#include <cstdint>

#include "display.hpp"
#include "modulation/matrix.hpp"
#include "params/parameters.hpp"
#include "platform/audio_out.hpp"
#include "platform/codec.hpp"
#include "platform/cpu_load.hpp"
#include "platform/sysclock.hpp"
#include "platform/touch.hpp"
#include "sequencer/pattern.hpp"
#include "sequencer/sequencer.hpp"
#include "ui/ui.hpp"

extern "C"
{
#include "main.h"
}

namespace
{

constexpr uint32_t kUiFrameMs = 20;   // ~50 Hz touch poll + redraw
constexpr uint32_t kLoadFrames = 25;  // CPU read-out window: ~0.5 s

params::Parameters g_params;
modulation::Matrix g_matrix;
sequencer::Pattern g_pattern;
sequencer::Transport g_transport;
sequencer::Sequencer g_sequencer(static_cast<float>(audio_out::kSampleRate));
ui::Ui g_ui(g_params, g_pattern, g_transport, g_matrix);

// DMA half-buffer callback, ISR context: render `frames` interleaved stereo
// frames into `out`. The per-block work goes here: controls -> sequencer clock
// -> engine.render. Only the clock exists so far; the audio is silence, so the
// stream keeps running and the codec stays clocked.
void audio_callback(int16_t* out, uint32_t frames)
{
	const uint32_t start = cpu_load::begin();

	// When the engine exists, an event whose step is on in g_pattern becomes
	// note_on(g_pattern.steps[e.step].note) at frame e.offset.
	const sequencer::Sequencer::Event e =
	    g_sequencer.process(g_transport.run, g_params.get(params::Id::Tempo),
		                    static_cast<int>(frames));
	(void)e;
	g_transport.step = g_sequencer.step();

	for(uint32_t i = 0; i < frames * 2; ++i)
	{
		out[i] = 0;
	}
	cpu_load::end(start);
}

const char* init_status(bool clock_ok, bool audio_ok, bool touch_ok)
{
	if(!clock_ok)
	{
		return "CLOCK FAIL";
	}
	if(!audio_ok && !touch_ok)
	{
		return "NO AUDIO/TOUCH";
	}
	if(!audio_ok)
	{
		return "NO AUDIO";
	}
	if(!touch_ok)
	{
		return "NO TOUCH";
	}
	return nullptr;
}

}  // namespace

extern "C" void app_main()
{
	// Full CPU speed before anything else, so every peripheral below is set up
	// against the final bus clocks.
	const bool clock_ok = sysclock::init();
	cpu_load::init(audio_out::kBlockSize, audio_out::kSampleRate);

	display::init(display::colors::Black);

	// Bring up the audio path: accurate 48 kHz SAI clock, SAI2 + DMA, then the
	// WM8994 codec over I2C. MCLK must be running (audio_out::init) before the
	// codec inits, so the order matters.
	const bool audio_ok =
	    audio_out::init() && codec::init(audio_out::kSampleRate, 70);
	const bool touch_ok = touch::init();

	if(audio_ok)
	{
		codec::play();
		audio_out::start(&audio_callback);
	}

	// Prime the ping-pong: one complete frame in each buffer before the
	// swap is armed. The UI only draws invalidated widgets, so each buffer
	// needs a full redraw_all() + update() pair, not just a clear.
	g_ui.redraw_all();
	g_ui.update(ui::Touch{false, 0, 0}, HAL_GetTick());
	display::swap_buffers();
	g_ui.redraw_all();
	g_ui.update(ui::Touch{false, 0, 0}, HAL_GetTick());
	display::enable_swap_irq();

	g_ui.set_status(init_status(clock_ok, audio_ok, touch_ok));
	g_ui.set_cpu_mhz(static_cast<int>(sysclock::cpu_hz() / 1000000));

	for(uint32_t frame = 1;; ++frame)
	{
		if(frame % kLoadFrames == 0)
		{
			const cpu_load::Stats load = cpu_load::take();
			g_ui.set_cpu_load(load.average, load.peak);
		}

		const touch::Point p = touch::read();
		display::begin_draw();
		g_ui.update(ui::Touch{p.pressed, p.x, p.y}, HAL_GetTick());
		display::end_draw();

		HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);  // heartbeat
		HAL_Delay(kUiFrameMs);
	}
}
