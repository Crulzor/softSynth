// SPDX-License-Identifier: MIT
//
// The SYNC page: where the sequencer's clock comes from, and what arrives on
// the clock input.
//
//   +-- CLOCK -------------------------------------------------------+
//   | SRC        | DIV        |  121 BPM                       (o)   |
//   |  EXT+      |  1/16      |  EXT                                |
//   +-- INPUT -------------------------------------------------------+
//   | L CLOCK  [=========|----------------------]   (o)   1234       |
//   | R RESET  [|----------------------------------]  ( )      0       |
//   +----------------------------------------------------------------+
//   | CLOCK ON L (TIP), RESET ON R (RING) OF THE LINE IN.             |
//   | LINE LEVEL ONLY: ATTENUATE 5-10 V GATES TO ~1 V FIRST.         |
//
// SRC: INT (the tempo slider), EXT+ (the clock input), EXT- (the clock input
// inverted, for negative-going pulses). DIV: what one pulse is worth (a
// 16th, an 8th, a beat, or 24 / 48 PPQN). The tempo readout shows the
// internal tempo, or the tempo measured from the clock (NO CLOCK when
// nothing arrives); with an external source its light blinks on every clock
// edge.
// The meters show each input channel's level (peak per block) with the
// detector's current trigger threshold as a white tick, a light that blinks
// on each detected edge and the edge count - so a patch can be checked
// before pressing play. Everything shown comes from the audio interrupt
// through sequencer::Transport::clock.
#pragma once

#include <cstdint>

#include "ui/page.hpp"
#include "ui/widgets.hpp"

namespace ui
{

class SyncPage : public Page
{
   public:
	explicit SyncPage(Ctx ctx);

	void redraw_all() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int, int, std::uint32_t) override {}
	void release(std::uint32_t now_ms) override;
	void update(std::uint32_t now_ms) override;

	// How long an edge light stays lit.
	static constexpr std::uint32_t kBlinkMs = 80;

	// One input channel's meter (clock or reset).
	class Meter : public Widget
	{
	   public:
		Meter(Rect r, const sequencer::ClockMonitor& mon, int channel,
		      const char* label)
		    : Widget(r), mon_(mon), channel_(channel), label_(label)
		{
		}
		void tick(std::uint32_t now_ms) override;
		void sync() override;

	   protected:
		void draw() override;

	   private:
		const sequencer::ClockMonitor& mon_;
		int channel_;
		const char* label_;
		std::uint32_t seen_edges_ = 0;
		std::uint32_t lit_until_ = 0;
		std::uint32_t now_ = 0;
		bool lit_ = false;
		bool shown_lit_ = false;
		int shown_level_ = -1;
		int shown_thr_ = -1;
		std::uint32_t shown_edges_ = ~0u;
	};

	// The tempo: internal, measured, or NO CLOCK; blinks on clock edges.
	class TempoReadout : public Widget
	{
	   public:
		TempoReadout(Rect r, const params::Parameters& params,
		             const sequencer::ClockMonitor& mon)
		    : Widget(r), params_(params), mon_(mon)
		{
		}
		void tick(std::uint32_t now_ms) override;
		void sync() override;

	   protected:
		void draw() override;

	   private:
		const params::Parameters& params_;
		const sequencer::ClockMonitor& mon_;
		std::uint32_t seen_edges_ = 0;
		std::uint32_t lit_until_ = 0;
		std::uint32_t now_ = 0;
		bool lit_ = false;
		bool shown_lit_ = false;
		int shown_src_ = -1;
		int shown_bpm_ = -1;
	};

   private:
	static constexpr int kWidgets = 5;

	Widget* hit(int x, int y);

	EnumButton source_;
	EnumButton division_;
	TempoReadout tempo_;
	Meter clock_;
	Meter reset_;

	Widget* widgets_[kWidgets];
	Widget* captured_ = nullptr;
};

}  // namespace ui
