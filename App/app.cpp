// SPDX-License-Identifier: MIT
//
// Application entry point (C++). Called from the CubeMX-generated main() once
// the clocks and GPIOs are up. This is where the softsynth will live; for now
// it paints the screen and gates a 440 Hz test tone from an on-screen touch
// button.
#include <cstdint>
#include <cstdio>

#include "display.hpp"
#include "platform/audio_out.hpp"
#include "platform/codec.hpp"
#include "platform/touch.hpp"

extern "C"
{
#include "main.h"
}

namespace
{

// --- 440 Hz test tone -------------------------------------------------------
// A 32-bit phase accumulator wraps once per cycle; the increment is the
// fraction of a full turn advanced per sample. This is the simplest possible
// stand-in for the real oscillator that will live in dsp/ later.
constexpr float kPi = 3.14159265358979f;
constexpr float kToneHz = 440.0f;
constexpr float kAmplitude = 8000.0f;  // ~0.25 full-scale: gentle on the ears

constexpr uint32_t kPhaseInc = static_cast<uint32_t>((static_cast<double>(kToneHz) * 4294967296.0) / audio_out::kSampleRate);

uint32_t g_phase = 0;

// The gate: written by the control loop (main), read by the audio ISR. A plain
// aligned 32-bit bool is read/written atomically on the M7, so no lock needed —
// this is the single-writer/single-reader boundary the architecture calls for.
volatile bool g_gate = false;

// Parabolic sine approximation over [-pi, pi]. Good to ~4% — inaudible for a
// test beep, and pulls in no libm (just FPU multiplies/adds). Exact at the
// peaks and zero crossings.
inline float fast_sin(uint32_t phase)
{
	const float t = static_cast<float>(phase) * (1.0f / 4294967296.0f);  // 0..1
	const float x = (t - 0.5f) * (2.0f * kPi);  // -pi..pi
	const float ax = x < 0.0f ? -x : x;
	return (4.0f / kPi) * x - (4.0f / (kPi * kPi)) * x * ax;
}

// DMA render callback: fill `frames` interleaved stereo frames. ISR context.
// When the gate is low we emit silence, so the DMA stream runs continuously and
// the tone simply switches on/off — no clicky DMA start/stop.
void render_tone(int16_t* out, uint32_t frames)
{
	if(!g_gate)
	{
		for(uint32_t i = 0; i < frames; ++i)
		{
			*out++ = 0;  // left
			*out++ = 0;  // right
		}
		return;
	}

	for(uint32_t i = 0; i < frames; ++i)
	{
		const int16_t s = static_cast<int16_t>(kAmplitude * fast_sin(g_phase));
		g_phase += kPhaseInc;
		*out++ = s;  // left
		*out++ = s;  // right
	}
}

// --- Debug touch button -----------------------------------------------------
constexpr int kBtnX = 90;
constexpr int kBtnY = 150;
constexpr int kBtnW = 300;
constexpr int kBtnH = 90;

void draw_button(bool pressed)
{
	const display::Color fill = pressed ? display::colors::Green : display::colors::Blue;
	display::fill_rect(kBtnX, kBtnY, kBtnW, kBtnH, fill);
	display::draw_text(kBtnX + 24, kBtnY + 33, "HOLD = TONE",
	                   display::colors::White, 3);
}

// True when a touch point falls inside the button rectangle (framebuffer space).
bool in_button(const touch::Point& p)
{
	return p.pressed && p.x >= kBtnX && p.x < kBtnX + kBtnW && p.y >= kBtnY &&
	       p.y < kBtnY + kBtnH;
}

}  // namespace

extern "C" void app_main()
{
	display::init(display::colors::Black);

	display::draw_text(8, 8, "crulSynth", display::colors::White, 4);
	display::draw_text(8, 56, "STM32H750B-DK  480x272 RGB565",
	                   display::colors::Green, 2);

	// Bring up the audio path: accurate 48 kHz SAI clock, SAI2 + DMA, then the
	// WM8994 codec over I2C. MCLK must be running (audio_out::init) before the
	// codec inits, so the order matters.
	const bool audio_ok =
	    audio_out::init() && codec::init(audio_out::kSampleRate, 70);
	const bool touch_ok = touch::init();

	if(audio_ok)
	{
		codec::play();
		// Start the DMA stream now; it carries silence until the gate goes high.
		audio_out::start(&render_tone);
	}
	else
	{
		display::draw_text(8, 100, "Audio init FAILED", display::colors::Red, 2);
	}
	if(!touch_ok)
	{
		display::draw_text(8, 100, "Touch init FAILED", display::colors::Red, 2);
	}

	draw_button(false);

	bool prev_pressed = false;
	while(1)
	{
		const touch::Point p = touch::read();
		const bool pressed = in_button(p);

		// Gate the tone while the button is held. Reset the phase on the rising
		// edge so the tone always starts at a zero crossing (no onset click).
		if(pressed && !prev_pressed)
		{
			g_phase = 0;
		}
		g_gate = pressed;

		if(pressed != prev_pressed)
		{
			draw_button(pressed);
			prev_pressed = pressed;
		}

		// Live raw-coordinate read-out, so we can later map a precise on-screen
		// button region to the controller's axes.
		char line[40];
		std::snprintf(line, sizeof(line), "touch %s  x=%-4u y=%-4u",
		              p.pressed ? "DOWN" : "up  ",
		              static_cast<unsigned>(p.x), static_cast<unsigned>(p.y));
		display::fill_rect(8, 252, 464, 16, display::colors::Black);
		display::draw_text(8, 252, line, display::colors::White, 2);

		HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);  // heartbeat
		HAL_Delay(20);                                // ~50 Hz control poll
	}
}
