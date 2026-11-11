// SPDX-License-Identifier: MIT
//
// Application entry point (C++). Called from the CubeMX-generated main() once
// the clocks and GPIOs are up.
//
// Two contexts, Mutable Instruments style (see ARCHITECTURE.md §3):
//   - The audio interrupt is the synth's real main loop. Once per block it
//     reads the controls, advances the sequencer clock and renders the voices.
//   - The foreground loop below only does what is too slow or blocking for the
//     interrupt: polling touch over I2C and drawing the UI.
// They meet in the parameter set, the wave slots, the matrix and the pattern:
// the UI writes them, the audio interrupt reads them.
//
// The audio interrupt runs the 6-voice engine (dsp::Engine: two additive
// oscillators -> Moog-style ladder -> envelope per voice). Notes come from
// two sources, each tagged so their note-offs never cross: USB-MIDI (the
// parser's event queue) and the step sequencer (clock -> Player, chords and
// ties). A sequencer step lands sample-accurately: the block is rendered in
// two parts, split at the step's frame.
//
// The sequencer runs from its own clock (SRC = INT) or from a modular clock
// on the audio input (EXT+ / EXT-): left channel = clock, right = reset,
// edges found per sample by sequencer::EdgeDetector and turned into steps by
// sequencer::ExtClock (see ext_clock.hpp).
#include <cmath>
#include <cstdint>

#include "display.hpp"
#include "dsp/engine.hpp"
#include "dsp/fx.hpp"
#include "dsp/ladder.hpp"
#include "dsp/sine_table.hpp"
#include "midi/midi.hpp"
#include "modulation/matrix.hpp"
#include "params/parameters.hpp"
#include "params/wave_slots.hpp"
#include "platform/audio_out.hpp"
#include "platform/codec.hpp"
#include "platform/cpu_load.hpp"
#include "platform/sysclock.hpp"
#include "platform/touch.hpp"
#include "platform/usbhost.hpp"
#include "sequencer/ext_clock.hpp"
#include "sequencer/pattern.hpp"
#include "sequencer/player.hpp"
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

// Who played a note (dsp::Engine tags).
constexpr uint8_t kTagMidi = 0;
constexpr uint8_t kTagSeq = 1;

static_assert(audio_out::kBlockSize == dsp::kBlockSize,
              "the engine renders one audio_out block per call");
static_assert(audio_out::kSampleRate == static_cast<uint32_t>(dsp::kSampleRate),
              "dsp and audio_out must agree on the sample rate");
static_assert(static_cast<int>(modulation::Source::Count) ==
                      static_cast<int>(dsp::ModSrc::Count) &&
                  static_cast<int>(modulation::Dest::Count) ==
                      static_cast<int>(dsp::ModDst::Count) &&
                  modulation::kRows == dsp::kModRoutes,
              "the matrix maps 1:1 onto the engine's routes");
// The matrix stores destinations by index: same order on both sides.
static_assert(static_cast<int>(modulation::Dest::Pitch1) ==
                      static_cast<int>(dsp::ModDst::Pitch1) &&
                  static_cast<int>(modulation::Dest::Shape2) ==
                      static_cast<int>(dsp::ModDst::Shape2) &&
                  static_cast<int>(modulation::Dest::Pw) ==
                      static_cast<int>(dsp::ModDst::Pw) &&
                  static_cast<int>(modulation::Dest::Mix) ==
                      static_cast<int>(dsp::ModDst::Mix) &&
                  static_cast<int>(modulation::Dest::Cutoff) ==
                      static_cast<int>(dsp::ModDst::Cutoff),
              "modulation::Dest and dsp::ModDst list the same destinations");
static_assert(dsp::kLfoDivCount == 12 &&
                  static_cast<int>(dsp::LfoShape::Count) == 5,
              "params' LFO LEN / SHP name lists match the engine");

params::Parameters g_params;
params::WaveSlots g_waves;
modulation::Matrix g_matrix;
sequencer::Pattern g_pattern;
sequencer::Transport g_transport;
sequencer::Sequencer g_sequencer(static_cast<float>(audio_out::kSampleRate));
ui::Ui g_ui(g_params, g_pattern, g_transport, g_matrix, g_waves);

// The delay lines: 1 s per channel, 16-bit, 192 KB in all - too big for
// DTCM, so they live in the 288 KB of D2 RAM next to the audio DMA buffers
// (the .dma_buffer section: NOLOAD, so Effects::init() clears them). The
// D2 SRAM bank clocks are switched on in app_main().
constexpr int kDelayLength = 48000;
__attribute__((section(".dma_buffer"))) int16_t g_delay_l[kDelayLength];
__attribute__((section(".dma_buffer"))) int16_t g_delay_r[kDelayLength];

// Audio-interrupt state.
dsp::Engine g_engine;
dsp::Effects g_fx;  // reverb + mod FX lines: ~70 KB of DTCM
dsp::FxParams g_fx_params;
sequencer::Player g_player;
dsp::Patch g_patch;
sequencer::ExtClock g_ext_clock(static_cast<float>(audio_out::kSampleRate));
sequencer::EdgeDetector g_clock_in;  // input L
sequencer::EdgeDetector g_reset_in;  // input R
int g_clock_src = 0;                 // last block's ClockSource

// Copy the UI-side model into the engine's patch: one consistent snapshot
// per block. Every field is a word the UI writes atomically.
DSP_HOT void fill_patch(dsp::Patch& p)
{
	using params::Id;
	const params::Parameters& q = g_params;
	p.osc_semis[0] = q.get(Id::Osc1Pitch) + 12.0f * q.get(Id::Osc1Octave);
	p.osc_semis[1] = q.get(Id::Osc2Pitch) + 12.0f * q.get(Id::Osc2Octave);
	p.shape[0] = q.get(Id::Osc1Shape);
	p.shape[1] = q.get(Id::Osc2Shape);
	p.mix = q.get(Id::OscMix);
	p.pw[0] = q.get(Id::Osc1Pw);
	p.pw[1] = q.get(Id::Osc2Pw);
	for(int o = 0; o < dsp::kOscs; ++o)
	{
		for(int s = 0; s < dsp::kSlots; ++s)
		{
			p.spectra[o][s] = g_waves.coeffs(o, s);
		}
	}
	p.attack = q.get(Id::EnvAttack);
	p.decay = q.get(Id::EnvDecay);
	p.sustain = q.get(Id::EnvSustain);
	p.release = q.get(Id::EnvRelease);
	p.cutoff = q.get(Id::FilterCutoff);
	p.resonance = q.get(Id::FilterResonance);
	p.drive = q.get(Id::FilterDrive);
	p.lfo_rate[0] = q.get(Id::Lfo1Rate);
	p.lfo_rate[1] = q.get(Id::Lfo2Rate);
	p.lfo_shape[0] = static_cast<uint8_t>(q.get(Id::Lfo1Shape));
	p.lfo_shape[1] = static_cast<uint8_t>(q.get(Id::Lfo2Shape));
	p.lfo_sync[0] = q.get(Id::Lfo1Mode) > 0.5f;
	p.lfo_sync[1] = q.get(Id::Lfo2Mode) > 0.5f;
	p.lfo_div[0] = static_cast<uint8_t>(q.get(Id::Lfo1Div));
	p.lfo_div[1] = static_cast<uint8_t>(q.get(Id::Lfo2Div));
	p.tempo_bpm = q.get(Id::Tempo);
	for(int i = 0; i < dsp::kModRoutes; ++i)
	{
		const modulation::Row& r = g_matrix.rows[i];
		p.routes[i] = dsp::ModRoute{static_cast<uint8_t>(r.src),
		                            static_cast<uint8_t>(r.dst), r.amount};
	}
}

// The FX page's parameters as the effects take them, once per block (after
// the clock, so synced times follow the current tempo / song position).
DSP_HOT void fill_fx(dsp::FxParams& f)
{
	using params::Id;
	const params::Parameters& q = g_params;
	auto index = [&](Id id, int count)
	{
		const int i = static_cast<int>(q.get(id));
		return i < 0 ? 0 : (i >= count ? count - 1 : i);
	};
	f.mod.type = static_cast<dsp::ModFxType>(
	    index(Id::ModFxType, static_cast<int>(dsp::ModFxType::Count)));
	f.mod.sync = q.get(Id::ModFxMode) > 0.5f;
	f.mod.rate = q.get(Id::ModFxRate);
	f.mod.div_steps = dsp::kLfoDivSteps[index(Id::ModFxDiv, dsp::kLfoDivCount)];
	f.mod.bpm = g_patch.tempo_bpm;
	f.mod.song_pos = g_patch.seq_pos;
	f.mod.depth = q.get(Id::ModFxDepth);
	f.mod.feedback = q.get(Id::ModFxFeedback);
	f.mod_mix = q.get(Id::ModFxMix);

	if(q.get(Id::DelayMode) > 0.5f)
	{
		const float steps =
		    dsp::kDelayDivSteps[index(Id::DelayDiv, dsp::kDelayDivCount)];
		f.delay.seconds = steps * 15.0f / g_patch.tempo_bpm;  // 16ths
	}
	else
	{
		f.delay.seconds = q.get(Id::DelayTime) * 0.001f;
	}
	f.delay.feedback = q.get(Id::DelayFeedback);
	f.delay.tone = q.get(Id::DelayTone);
	f.delay.ping = q.get(Id::DelayStereo) > 0.5f;
	f.delay_mix = q.get(Id::DelayMix);

	f.reverb.decay = q.get(Id::ReverbDecay);
	f.reverb.damp = q.get(Id::ReverbDamp);
	f.reverb_mix = q.get(Id::ReverbMix);
}

inline int16_t to_i16(float s)
{
	// Master soft clip: linear for normal levels, rounding off the peaks
	// when many voices stack up.
	return static_cast<int16_t>(dsp::fast_tanh(s) * 32767.0f);
}

// One block of at most dsp::kBlockSize frames: MIDI events, then the
// sequencer clock (internal, or the clock input in `in`), then the engine.
DSP_HOT void render_block(int16_t* out, const int16_t* in, int n)
{

	fill_patch(g_patch);

	// MIDI in: every queued key, at the top of the block.
	midi::NoteEvent me;
	while(midi::parser().events.pop(me))
	{
		switch(me.kind)
		{
			case midi::NoteEvent::On:
				g_engine.note_on(me.note, me.velocity, kTagMidi);
				break;
			case midi::NoteEvent::Off:
				g_engine.note_off(me.note, kTagMidi);
				break;
			default:
				g_engine.release_all(kTagMidi);
				break;
		}
	}

	// Clock input: edges on the left (clock) and right (reset) channels,
	// measured whether or not it drives the sequencer, so the SYNC page
	// shows what arrives.
	const int src = static_cast<int>(g_params.get(params::Id::ClockSource));
	const bool ext = src != 0;
	const bool invert = src == 2;
	const auto div = static_cast<sequencer::ClockDiv>(
	    static_cast<int>(g_params.get(params::Id::ClockDiv)));
	int clocks[sequencer::EdgeDetector::kMaxEdges];
	int resets[sequencer::EdgeDetector::kMaxEdges];
	const int n_clocks = g_clock_in.process(in, 2, n, invert, clocks);
	const int n_resets = g_reset_in.process(in + 1, 2, n, invert, resets);
	sequencer::ClockMonitor& mon = g_transport.clock;
	mon.level[0] = g_clock_in.level();
	mon.level[1] = g_reset_in.level();
	mon.threshold[0] = g_clock_in.threshold();
	mon.threshold[1] = g_reset_in.threshold();
	mon.edges[0] = mon.edges[0] + static_cast<uint32_t>(n_clocks);
	mon.edges[1] = mon.edges[1] + static_cast<uint32_t>(n_resets);

	// Sequencer: whichever clock is selected says where a step started or
	// its gate closed; the player turns that into notes (chords, ties).
	const bool run = g_transport.run;
	const sequencer::Sequencer::Event e_ext = g_ext_clock.process(
	    run && ext, clocks, n_clocks, resets, n_resets, div, n);
	const float ext_bpm = g_ext_clock.bpm(div);
	mon.bpm = ext_bpm;
	// The external clock is only "there" while pulses arrive. Until then
	// (nothing plugged in, or it stopped) the internal clock keeps running
	// as the timing reference, so synced LFOs and the Tempo source lock to
	// it - its steps only play notes when SRC = INT.
	const bool ext_live = ext && ext_bpm > 0.0f;
	const sequencer::Sequencer::Event e_int = g_sequencer.process(
	    run && !ext_live, g_params.get(params::Id::Tempo), n);
	const sequencer::Sequencer::Event e = ext ? e_ext : e_int;
	g_transport.step = ext ? g_ext_clock.step() : g_sequencer.step();
	if(ext_live)
	{
		g_patch.tempo_bpm = ext_bpm;  // synced LFOs and Tempo follow too
	}
	// Song position for the synced LFOs: the external clock's while it
	// runs, else the internal one's (< 0 when stopped: they free-run at the
	// synced rate).
	g_patch.seq_pos =
	    ext_live ? g_ext_clock.position() : g_sequencer.position();

	sequencer::NoteEvent ev[sequencer::Player::kMaxEvents];
	int count = 0;
	int at = 0;
	if(!run || src != g_clock_src)
	{
		// Stopped, or the clock source changed under a running pattern.
		count = g_player.stop(ev);
	}
	else if(e.step >= 0)
	{
		count = g_player.on_event(g_pattern, e, ev);
		at = e.offset;
	}
	else if(e.gate_off >= 0)
	{
		count = g_player.on_event(g_pattern, e, ev);
		at = e.gate_off;
	}
	g_clock_src = src;

	// Render up to the event's frame, apply it, render the rest.
	float buf[dsp::kBlockSize];
	if(count > 0 && at > 0)
	{
		g_engine.render(g_patch, buf, at);
	}
	else
	{
		at = 0;
	}
	for(int i = 0; i < count; ++i)
	{
		if(ev[i].on)
		{
			g_engine.note_on(ev[i].note, ev[i].velocity, kTagSeq);
		}
		else
		{
			g_engine.note_off(ev[i].note, kTagSeq);
		}
	}
	g_engine.render(g_patch, buf + at, n - at);
	g_waves.set_live(g_engine.live_shape(0), g_engine.live_shape(1),
	                 g_engine.live_mix(), g_engine.live_pw(0),
	                 g_engine.live_pw(1));

	// Effects: mono voices in, stereo out.
	fill_fx(g_fx_params);
	float left[dsp::kBlockSize];
	float right[dsp::kBlockSize];
	g_fx.process(g_fx_params, buf, left, right, n);
	for(int i = 0; i < n; ++i)
	{
		out[2 * i] = to_i16(left[i]);
		out[2 * i + 1] = to_i16(right[i]);
	}
}

// DMA callback, ISR context: render `frames` interleaved stereo frames into
// `out`. Normally one half-buffer (kBlockSize frames), but audio_out::start()
// pre-fills the whole double buffer in one call, so it is cut into blocks.
DSP_HOT void audio_callback(int16_t* out, const int16_t* in, uint32_t frames)
{
	const uint32_t start = cpu_load::begin();
	while(frames > 0)
	{
		const uint32_t n =
		    frames < audio_out::kBlockSize ? frames : audio_out::kBlockSize;
		render_block(out, in, static_cast<int>(n));
		out += 2 * n;
		in += 2 * n;
		frames -= n;
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
	// The oscillators' sine table must be filled before the first block,
	// and the effects' lines cleared - the delay's in D2 RAM, whose bank
	// clocks have to be on first.
	dsp::sine_table::init();
	// The per-sample code (DSP_HOT) runs from ITCM: copy it there from its
	// load image in flash before the first audio block.
	{
		extern uint32_t _sitcm;
		extern uint32_t _eitcm;
		extern uint32_t _siitcm;
		const uint32_t* src = &_siitcm;
		for(uint32_t* dst = &_sitcm; dst < &_eitcm;)
		{
			*dst++ = *src++;
		}
	}
	__HAL_RCC_D2SRAM1_CLK_ENABLE();
	__HAL_RCC_D2SRAM2_CLK_ENABLE();
	__HAL_RCC_D2SRAM3_CLK_ENABLE();
	g_fx.init(g_delay_l, g_delay_r, kDelayLength);
	const bool audio_ok =
	    audio_out::init() && codec::init(audio_out::kSampleRate, 70);
	const bool touch_ok = touch::init();

	if(audio_ok)
	{
		codec::play();
		audio_out::start(&audio_callback);
	}

	// USB-MIDI input on CN13, host role. init() arms the port; enumeration
	// then runs from process() below on the foreground frame loop - it can
	// block (~110 ms port reset), so it must never be in an ISR.
	usbhost::init();

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

	bool was_ready = false;
	for(uint32_t frame = 1;; ++frame)
	{
		if(frame % kLoadFrames == 0)
		{
			const cpu_load::Stats load = cpu_load::take();
			g_ui.set_cpu_load(load.average, load.peak);
		}

		// USB host: connect, enumeration and the armed bulk-IN. A device
		// that goes away with keys down must not leave them ringing.
		usbhost::process();
		const midi::Link link = usbhost::link();
		if(was_ready && link.state != midi::LinkState::Ready)
		{
			midi::parser().all_notes_off();
		}
		was_ready = link.state == midi::LinkState::Ready;
		g_ui.set_usb_link(link);

		const touch::Point p = touch::read();
		display::begin_draw();
		g_ui.update(ui::Touch{p.pressed, p.x, p.y}, HAL_GetTick());
		display::end_draw();

		HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);  // heartbeat
		HAL_Delay(kUiFrameMs);
	}
}
