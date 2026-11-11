// SPDX-License-Identifier: MIT
#include "ui/osc_page.hpp"

#include "display.hpp"
#include "ui/theme.hpp"

namespace ui
{
namespace
{

using params::Id;

// --- Layout (kPageW x kPageH) ------------------------------------------------
// Two rows of 100 px cells under coloured section rules:
//   OSC 1 / OSC 2  PIT, SHP, PW large knobs (56 wide) and a stack of four
//                  50 x 22 cells: WAV, A, B and the OCT stepper
//   MIX            the crossfader, spanning both rows (top = osc 1)
//   FILTER         row 1 on the right: CUT, RES, DRV, in the filter's accent
//   WAVE           row 2 on the right: the scope, as wide as the filter
constexpr int kKnobW = 56;
constexpr int kCellH = 100;
constexpr int kGap = 5;
constexpr int kPitch = kKnobW + kGap;  // 61
constexpr int kMarginX = 5;

constexpr int kRuleY[2] = {4, 126};  // section rules; cells start 12 below
constexpr int kRowY[2] = {kRuleY[0] + 12, kRuleY[1] + 12};

constexpr int kBtnX = kMarginX + 3 * kPitch;  // 188
constexpr int kBtnW = 50;
constexpr int kBtnH = 22;
constexpr int kBtnPitch = 26;                  // 4 x 22 + 3 x 4 = 100
constexpr int kOscW = kBtnX + kBtnW - kMarginX;  // 233, to x = 238

constexpr Rect cell(int col, int row)
{
	return Rect{kMarginX + col * kPitch, kRowY[row], kKnobW, kCellH};
}

constexpr Rect button(int i, int row)
{
	return Rect{kBtnX, kRowY[row] + i * kBtnPitch, kBtnW, kBtnH};
}

constexpr int kMixX = kBtnX + kBtnW + kGap;  // 243
constexpr int kMixW = 48;  // room for the value at scale 2 ("127")
constexpr Rect kMix{kMixX, kRowY[0], kMixW, kRowY[1] + kCellH - kRowY[0]};

constexpr int kRightX = kMixX + kMixW + kGap;  // 296
constexpr int kRightW = 3 * kKnobW + 2 * kGap;  // 178, to x = 474
constexpr Rect filter_cell(int i)
{
	return Rect{kRightX + i * kPitch, kRowY[0], kKnobW, kCellH};
}
constexpr Rect kScope{kRightX, kRowY[1], kRightW, kCellH};

}  // namespace

OscPage::OscPage(Ctx ctx)
    : knobs_{
          Knob(cell(0, 0), ctx.params, Id::Osc1Pitch),
          Knob(cell(1, 0), ctx.params, Id::Osc1Shape),
          Knob(cell(2, 0), ctx.params, Id::Osc1Pw),
          Knob(cell(0, 1), ctx.params, Id::Osc2Pitch),
          Knob(cell(1, 1), ctx.params, Id::Osc2Shape),
          Knob(cell(2, 1), ctx.params, Id::Osc2Pw),
      },
      wave_{WaveButton(button(0, 0), ctx.waves, 0),
            WaveButton(button(0, 1), ctx.waves, 1)},
      slot_{SlotButton(button(1, 0), ctx.waves, 0, 0),
            SlotButton(button(2, 0), ctx.waves, 0, 1),
            SlotButton(button(1, 1), ctx.waves, 1, 0),
            SlotButton(button(2, 1), ctx.waves, 1, 1)},
      oct_{Stepper(button(3, 0), ctx.params, Id::Osc1Octave),
           Stepper(button(3, 1), ctx.params, Id::Osc2Octave)},
      mix_(kMix, ctx.params, Id::OscMix),
      filter_{Knob(filter_cell(0), ctx.params, Id::FilterCutoff),
              Knob(filter_cell(1), ctx.params, Id::FilterResonance),
              Knob(filter_cell(2), ctx.params, Id::FilterDrive)},
      scope_(kScope, ctx.waves, ctx.params)
{
	int n = 0;
	for(Knob& k : knobs_)
	{
		k.set_large(true);
		k.set_accent(theme::kOscAccent);
		widgets_[n++] = &k;
	}
	for(WaveButton& w : wave_)
	{
		w.set_accent(theme::kOscAccent);
		widgets_[n++] = &w;
	}
	for(SlotButton& b : slot_)
	{
		b.set_accent(theme::kOscAccent);
		widgets_[n++] = &b;
	}
	for(Stepper& o : oct_)
	{
		o.set_accent(theme::kOscAccent);
		widgets_[n++] = &o;
	}
	mix_.set_large(true);
	mix_.set_accent(theme::kOscAccent);
	widgets_[n++] = &mix_;
	for(Knob& k : filter_)
	{
		k.set_large(true);
		k.set_accent(theme::kFilAccent);
		widgets_[n++] = &k;
	}
	scope_.set_accent(theme::kOscAccent);
	widgets_[n++] = &scope_;
}

void OscPage::redraw_all()
{
	display::fill(theme::kBackground);
	draw_section(kMarginX, kRuleY[0], kOscW, "OSC 1", theme::kOscAccent);
	draw_section(kMarginX, kRuleY[1], kOscW, "OSC 2", theme::kOscAccent);
	draw_section(kMixX, kRuleY[0], kMixW, "MIX", theme::kOscAccent);
	draw_section(kRightX, kRuleY[0], kRightW, "FILTER", theme::kFilAccent);
	draw_section(kRightX, kRuleY[1], kRightW, "WAVE", theme::kOscAccent);
	for(Widget* w : widgets_)
	{
		w->invalidate();
	}
}

Widget* OscPage::hit(int x, int y)
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

void OscPage::press(int x, int y, std::uint32_t now_ms)
{
	captured_ = hit(x, y);
	if(captured_ != nullptr)
	{
		captured_->press(x, y, now_ms);
	}
}

void OscPage::drag(int x, int y, std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->drag(x, y, now_ms);
	}
}

void OscPage::release(std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->release(now_ms);
		captured_ = nullptr;
	}
}

void OscPage::update(std::uint32_t now_ms)
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
