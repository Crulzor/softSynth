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
// Two rows on one 76 x 100 cell grid, columns under coloured section rules:
//   OSC 1 / OSC 2  two rows of large knobs (pitch, shape, octave)
//   MIX            the crossfader, spanning both rows (top = osc 1)
//   FILTER         row 1 on the right: cutoff and resonance side by side, in
//                  the filter's own accent
//   WAVE           row 2 on the right: the oscilloscope slot (two cells wide)
constexpr int kCellW = 76;
constexpr int kCellH = 100;
constexpr int kGap = 8;
constexpr int kColPitch = kCellW + kGap;  // 84
constexpr int kMarginX = 8;

constexpr int kRuleY[2] = {4, 126};  // section rules; cells start 12 below
constexpr int kRowY[2] = {kRuleY[0] + 12, kRuleY[1] + 12};

constexpr int kOscW = 3 * kCellW + 2 * kGap;  // 244

constexpr Rect cell(int col, int row)
{
	return Rect{kMarginX + col * kColPitch, kRowY[row], kCellW, kCellH};
}

constexpr int kMixX = kMarginX + kOscW + kGap;  // 260
constexpr int kMixW = 44;
constexpr Rect kMix{kMixX, kRowY[0], kMixW, kRowY[1] + kCellH - kRowY[0]};

constexpr int kRightX = kMixX + kMixW + kGap;  // 312
constexpr int kRightW = 2 * kCellW + kGap;     // 160, to x = 472
constexpr Rect kCutoff{kRightX, kRowY[0], kCellW, kCellH};
constexpr Rect kResonance{kRightX + kColPitch, kRowY[0], kCellW, kCellH};
constexpr Rect kScope{kRightX, kRowY[1], kRightW, kCellH};

}  // namespace

OscPage::OscPage(Ctx ctx)
    : knobs_{
          Knob(cell(0, 0), ctx.params, Id::Osc1Pitch),
          Knob(cell(1, 0), ctx.params, Id::Osc1Shape),
          Knob(cell(2, 0), ctx.params, Id::Osc1Octave),
          Knob(cell(0, 1), ctx.params, Id::Osc2Pitch),
          Knob(cell(1, 1), ctx.params, Id::Osc2Shape),
          Knob(cell(2, 1), ctx.params, Id::Osc2Octave),
      },
      mix_(kMix, ctx.params, Id::OscMix),
      cutoff_(kCutoff, ctx.params, Id::FilterCutoff),
      resonance_(kResonance, ctx.params, Id::FilterResonance),
      scope_(kScope)
{
	int n = 0;
	for(Knob& k : knobs_)
	{
		k.set_large(true);
		k.set_accent(theme::kOscAccent);
		widgets_[n++] = &k;
	}
	mix_.set_large(true);
	mix_.set_accent(theme::kOscAccent);
	widgets_[n++] = &mix_;
	Knob* filter[] = {&cutoff_, &resonance_};
	for(Knob* k : filter)
	{
		k->set_large(true);
		k->set_accent(theme::kFilAccent);
		widgets_[n++] = k;
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
