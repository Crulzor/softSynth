// SPDX-License-Identifier: MIT
//
// The ENV page: the ADSR knobs (attack / decay / sustain / release) with a
// read-only preview of the resulting envelope curve below.
#pragma once

#include "ui/page.hpp"
#include "ui/widgets.hpp"

namespace ui
{

// Read-only ADSR curve preview: the attack, decay and release spans share
// three quarters of the width in proportion to their values, and the sustain
// level (a level, not a duration) is the flat top of the remaining quarter;
// the shape under the curve is filled and the curve outlined.
class CurvePreview : public Widget
{
public:
	CurvePreview(Rect r, params::Parameters& p) : Widget(r), params_(p) {}

	void sync() override;

protected:
	void draw() override;

private:
	struct Value
	{
		float a;
		float d;
		float s;
		float r;
	};

	params::Parameters& params_;
	Value shown_{};
};

class EnvPage : public Page
{
public:
	explicit EnvPage(Ctx ctx);

	void redraw_all() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;
	void update(std::uint32_t now_ms) override;

private:
	static constexpr int kWidgets = 5;

	Widget* hit(int x, int y);

	Knob knobs_[4];  // ATK, DEC, SUS, REL
	CurvePreview curve_;
	Widget* widgets_[kWidgets];
	Widget* captured_ = nullptr;
};

}  // namespace ui
