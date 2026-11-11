// SPDX-License-Identifier: MIT
//
// The MOD page: the 6-row modulation matrix as routing rows ("SRC -> DST"
// cycle buttons and a bipolar amount bar; rows with amount 0 dimmed) in two
// columns of three, with the two LFOs in the strip below. Per LFO: a rate
// knob (SPD in Hz when FREE, LEN - a musical length - when SYNC; only the
// one matching the mode is shown), a shape button (tap: SIN TRI SAW SQR S&H,
// drawn as a little wave) and a MODE button (FREE / SYNC).
#pragma once

#include "ui/page.hpp"
#include "ui/widgets.hpp"

namespace ui
{

class ModPage : public Page
{
public:
	explicit ModPage(Ctx ctx);

	void redraw_all() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;
	void update(std::uint32_t now_ms) override;

	// Tap to cycle an LFO's shape; shows the name and a picture of the wave.
	class ShapeButton : public Widget
	{
	   public:
		ShapeButton(Rect r, params::Parameters& p, params::Id id)
		    : Widget(r), params_(p), id_(id)
		{
		}
		void sync() override;
		void press(int x, int y, std::uint32_t now_ms) override;
		void release(std::uint32_t now_ms) override;

	   protected:
		void draw() override;

	   private:
		params::Parameters& params_;
		params::Id id_;
		float shown_ = -1.0f;
		bool pressed_ = false;
	};

private:
	static constexpr int kWidgets = 6 * 3 + 2 * 4;

	Widget* hit(int x, int y);
	// False for the rate knob the LFO's mode hides (SPD when SYNC, LEN when
	// FREE).
	bool active(const Widget* w) const;

	CycleButton srcs_[modulation::kRows];
	CycleButton dsts_[modulation::kRows];
	FloatKnob amts_[modulation::kRows];
	Knob rate_[2];  // FREE: Hz
	Knob len_[2];   // SYNC: musical length
	ShapeButton shape_[2];
	EnumButton mode_[2];
	params::Parameters& params_;
	bool shown_sync_[2] = {false, false};

	Widget* widgets_[kWidgets];
	Widget* captured_ = nullptr;

	// Each row's arrow as last drawn (live = amount != 0).
	const float* amount_[modulation::kRows] = {};
	bool arrow_live_[modulation::kRows] = {};
	bool arrows_valid_ = false;
};

}  // namespace ui
