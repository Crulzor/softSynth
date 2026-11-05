// SPDX-License-Identifier: MIT
#include "ui/env_page.hpp"

#include "display.hpp"
#include "ui/theme.hpp"

namespace ui
{
namespace
{

using params::Id;

// --- Layout (kPageW x kPageH) ------------------------------------------------
// A row of four knobs across the top (ATK / DEC / SUS / REL), the curve
// preview filling the rest of the page below.
constexpr int kHeaderH = 11;

constexpr int kKnobY = 14;
constexpr int kKnobW = 48;
constexpr int kKnobH = 44;
constexpr int kKnobSlot = kPageW / 4;  // 120

constexpr Rect knob(int i)
{
	return Rect{kKnobSlot * i + (kKnobSlot - kKnobW) / 2, kKnobY, kKnobW,
	            kKnobH};
}

constexpr int kCurveX = 8;
constexpr int kCurveY = 66;
constexpr int kCurveW = kPageW - 2 * kCurveX;
constexpr int kCurveH = kPageH - kCurveY - 6;

}  // namespace

// --- CurvePreview ------------------------------------------------------------

void CurvePreview::sync()
{
	Value v;
	v.a = params_.get(Id::EnvAttack);
	v.d = params_.get(Id::EnvDecay);
	v.s = params_.get(Id::EnvSustain);
	v.r = params_.get(Id::EnvRelease);
	if(v.a != shown_.a || v.d != shown_.d || v.s != shown_.s ||
	   v.r != shown_.r)
	{
		shown_ = v;
		invalidate();
	}
}

void CurvePreview::draw()
{
	const Rect& r = rect_;
	const int x0 = r.x;
	const int w = r.w;
	const int yTop = r.y;
	const int yBot = r.y + r.h;
	const int h = r.h;

	// Clear our own rect first, like every other widget: the curve moved, and
	// the pixels it vacated are ours to reclaim, not the page's to clean.
	display::fill_rect(x0, yTop, w, h, theme::kBackground);

	const float a = params_.get(Id::EnvAttack);
	const float d = params_.get(Id::EnvDecay);
	const float s = params_.get(Id::EnvSustain);
	const float rel = params_.get(Id::EnvRelease);
	const int susY = yBot - static_cast<int>(s * h + 0.5f);

	// The three durations (attack, decay, release) share three quarters of
	// the width in proportion to their values; the sustain level is a flat
	// top of the remaining quarter.
	const float t = a + d + rel;
	int x1 = x0;
	int x2 = x0;
	if(t > 0.0f)
	{
		const float f = 3.0f * w / (4.0f * t);
		x1 += static_cast<int>(a * f + 0.5f);
		x2 = x1 + static_cast<int>(d * f + 0.5f);
	}
	const int x3 = x2 + w / 4;
	const int x4 = x0 + w;

	// The region under the envelope: close along the bottom edge, so a single
	// polygon is the filled shape.
	const int xy[10] = {
	    x0,  yBot, x1, yTop, x2, susY, x3, susY, x4, yBot};
	display::fill_polygon(xy, 5, theme::kTrack);

	// Stroke the envelope on top.
	display::draw_line(x0, yBot, x1, yTop, theme::kAccent);
	display::draw_line(x1, yTop, x2, susY, theme::kAccent);
	display::draw_line(x2, susY, x3, susY, theme::kAccent);
	display::draw_line(x3, susY, x4, yBot, theme::kAccent);

	display::draw_rect(x0, yTop, w, h, theme::kHeader);
}

// --- EnvPage -----------------------------------------------------------------

EnvPage::EnvPage(Ctx ctx)
    : knobs_{Knob(knob(0), ctx.params, Id::EnvAttack),
             Knob(knob(1), ctx.params, Id::EnvDecay),
             Knob(knob(2), ctx.params, Id::EnvSustain),
             Knob(knob(3), ctx.params, Id::EnvRelease)},
      curve_(Rect{kCurveX, kCurveY, kCurveW, kCurveH}, ctx.params)
{
	widgets_[0] = &knobs_[0];
	widgets_[1] = &knobs_[1];
	widgets_[2] = &knobs_[2];
	widgets_[3] = &knobs_[3];
	widgets_[4] = &curve_;
}

void EnvPage::redraw_all()
{
	display::fill(theme::kBackground);
	display::fill_rect(0, 0, kPageW, kHeaderH, theme::kHeader);
	display::draw_text(3, 2, "ENV", theme::kText);
	for(Widget* w : widgets_)
	{
		w->invalidate();
	}
}

Widget* EnvPage::hit(int x, int y)
{
	for(Widget* w : widgets_)
	{
		if(w->rect().contains(x, y))
		{
			return w;
		}
	}
	return nullptr;
}

void EnvPage::press(int x, int y, std::uint32_t now_ms)
{
	captured_ = hit(x, y);
	if(captured_ != nullptr)
	{
		captured_->press(x, y, now_ms);
	}
}

void EnvPage::drag(int x, int y, std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->drag(x, y, now_ms);
	}
}

void EnvPage::release(std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->release(now_ms);
		captured_ = nullptr;
	}
}

void EnvPage::update(std::uint32_t now_ms)
{
	for(Widget* w : widgets_)
	{
		w->tick(now_ms);
	}
	for(Widget* w : widgets_)
	{
		w->sync();
	}
	for(Widget* w : widgets_)
	{
		w->draw_if_dirty();
	}
}

}  // namespace ui
