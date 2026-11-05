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
// Left of the separator: the two oscillator rows in the debug page's cell
// geometry, with the mix fader spanning both rows in a fourth column. Right:
// the filter's two knobs, centred in the remaining space.
constexpr int kTagW = 11;
constexpr int kCellW = 36;
constexpr int kCellPitch = 37;
constexpr int kCellH = 44;
constexpr int kHeaderH = 11;

constexpr int kRowY[2] = {12, 56};

constexpr Rect cell(int col, int row)
{
	return Rect{kTagW + col * kCellPitch, kRowY[row], kCellW, kCellH};
}

constexpr int kDivX = 159;  // full-height separator line
constexpr Rect kMix{kTagW + 3 * kCellPitch, kRowY[0], kCellW, 2 * kCellH};

constexpr Rect kCutoff{231, kRowY[0], 64, kCellH};
constexpr Rect kResonance{343, kRowY[0], 64, kCellH};

// Row tags, drawn as stacked letters.
struct RowTag
{
	const char* text;
	int y;
};
constexpr RowTag kRowTags[] = {{"OS1", kRowY[0]}, {"OS2", kRowY[1]}};

void draw_row_tag(const RowTag& tag)
{
	const int top = tag.y + (kCellH - 26) / 2;
	char ch[2] = {0, 0};
	for(int i = 0; tag.text[i] != '\0'; ++i)
	{
		ch[0] = tag.text[i];
		display::draw_text(2, top + i * 9, ch, theme::kDim);
	}
}

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
      resonance_(kResonance, ctx.params, Id::FilterResonance)
{
	int n = 0;
	for(Knob& k : knobs_)
	{
		widgets_[n++] = &k;
	}
	widgets_[n++] = &mix_;
	widgets_[n++] = &cutoff_;
	widgets_[n++] = &resonance_;
}

void OscPage::redraw_all()
{
	display::fill(theme::kBackground);
	display::fill_rect(0, 0, kDivX, kHeaderH, theme::kHeader);
	display::draw_text(3, 2, "OSC", theme::kText);
	display::fill_rect(kDivX + 1, 0, kPageW - kDivX - 1, kHeaderH,
	                  theme::kHeader);
	display::draw_text(kDivX + 4, 2, "FIL", theme::kText);
	display::fill_rect(kDivX, 0, 1, kPageH, theme::kHeader);
	for(const RowTag& tag : kRowTags)
	{
		draw_row_tag(tag);
	}
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
