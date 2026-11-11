// SPDX-License-Identifier: MIT
#include "ui/mod_page.hpp"

#include "display.hpp"
#include "modulation/matrix.hpp"
#include "ui/theme.hpp"

namespace ui
{
namespace
{

using params::Id;

// --- Layout (kPageW x kPageH) ------------------------------------------------
// The matrix as two columns of three 44 px rows: per row, a CycleButton for
// the source, one for the destination, and a knob for the amount. The two
// LFO rate knobs ride in the strip below.
constexpr int kRowH = 44;
constexpr int kRowPitch = 48;
constexpr int kRowY0 = 14;

constexpr int kCellX[3] = {8, 88, 168};
constexpr int kCellW[3] = {72, 72, 64};
constexpr int kCol2Off = 246;  // rows 4-6 repeat the column, offset

constexpr Rect cell(int i, int c)
{
	return Rect{kCellX[c] + (i / 3) * kCol2Off, kRowY0 + (i % 3) * kRowPitch,
	            kCellW[c], kRowH};
}

constexpr int kSepY = 158;
constexpr int kLfoY = 172;
constexpr Rect kLfo1{12, kLfoY, 64, kRowH};
constexpr Rect kLfo2{96, kLfoY, 64, kRowH};

void draw_row_tag(int x, int y, const char* text)
{
	const int top = y + (kRowH - 26) / 2;
	char ch[2] = {0, 0};
	for(int i = 0; text[i] != '\0'; ++i)
	{
		ch[0] = text[i];
		display::draw_text(x, top + i * 9, ch, theme::kDim);
	}
}

}  // namespace

ModPage::ModPage(Ctx ctx)
    : srcs_{CycleButton(cell(0, 0), &ctx.matrix.rows[0].src,
                        static_cast<int>(modulation::Source::Count),
                        modulation::kSourceNames),
            CycleButton(cell(1, 0), &ctx.matrix.rows[1].src,
                        static_cast<int>(modulation::Source::Count),
                        modulation::kSourceNames),
            CycleButton(cell(2, 0), &ctx.matrix.rows[2].src,
                        static_cast<int>(modulation::Source::Count),
                        modulation::kSourceNames),
            CycleButton(cell(3, 0), &ctx.matrix.rows[3].src,
                        static_cast<int>(modulation::Source::Count),
                        modulation::kSourceNames),
            CycleButton(cell(4, 0), &ctx.matrix.rows[4].src,
                        static_cast<int>(modulation::Source::Count),
                        modulation::kSourceNames),
            CycleButton(cell(5, 0), &ctx.matrix.rows[5].src,
                        static_cast<int>(modulation::Source::Count),
                        modulation::kSourceNames)},
      dsts_{CycleButton(cell(0, 1), &ctx.matrix.rows[0].dst,
                        static_cast<int>(modulation::Dest::Count),
                        modulation::kDestNames),
            CycleButton(cell(1, 1), &ctx.matrix.rows[1].dst,
                        static_cast<int>(modulation::Dest::Count),
                        modulation::kDestNames),
            CycleButton(cell(2, 1), &ctx.matrix.rows[2].dst,
                        static_cast<int>(modulation::Dest::Count),
                        modulation::kDestNames),
            CycleButton(cell(3, 1), &ctx.matrix.rows[3].dst,
                        static_cast<int>(modulation::Dest::Count),
                        modulation::kDestNames),
            CycleButton(cell(4, 1), &ctx.matrix.rows[4].dst,
                        static_cast<int>(modulation::Dest::Count),
                        modulation::kDestNames),
            CycleButton(cell(5, 1), &ctx.matrix.rows[5].dst,
                        static_cast<int>(modulation::Dest::Count),
                        modulation::kDestNames)},
      amts_{FloatKnob(cell(0, 2), &ctx.matrix.rows[0].amount, -1.0f, 1.0f,
                      0.05f, 100, "AMT"),
            FloatKnob(cell(1, 2), &ctx.matrix.rows[1].amount, -1.0f, 1.0f,
                      0.05f, 100, "AMT"),
            FloatKnob(cell(2, 2), &ctx.matrix.rows[2].amount, -1.0f, 1.0f,
                      0.05f, 100, "AMT"),
            FloatKnob(cell(3, 2), &ctx.matrix.rows[3].amount, -1.0f, 1.0f,
                      0.05f, 100, "AMT"),
            FloatKnob(cell(4, 2), &ctx.matrix.rows[4].amount, -1.0f, 1.0f,
                      0.05f, 100, "AMT"),
            FloatKnob(cell(5, 2), &ctx.matrix.rows[5].amount, -1.0f, 1.0f,
                      0.05f, 100, "AMT")},
      lfo1_(kLfo1, ctx.params, Id::Lfo1Rate),
      lfo2_(kLfo2, ctx.params, Id::Lfo2Rate)
{
	int n = 0;
	for(CycleButton& b : srcs_)
	{
		widgets_[n++] = &b;
	}
	for(CycleButton& b : dsts_)
	{
		widgets_[n++] = &b;
	}
	for(FloatKnob& k : amts_)
	{
		widgets_[n++] = &k;
	}
	widgets_[n++] = &lfo1_;
	widgets_[n++] = &lfo2_;
	for(Widget* w : widgets_)
	{
		w->set_accent(theme::kModAccent);
	}
}

void ModPage::redraw_all()
{
	display::fill(theme::kBackground);
	draw_section(8, 2, kPageW - 16, "MATRIX", theme::kModAccent);
	draw_section(8, kSepY, kPageW - 16, "LFO", theme::kModAccent);
	draw_row_tag(2, kLfoY, "LF1");
	draw_row_tag(84, kLfoY, "LF2");
	for(Widget* w : widgets_)
	{
		w->invalidate();
	}
}

Widget* ModPage::hit(int x, int y)
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

void ModPage::press(int x, int y, std::uint32_t now_ms)
{
	captured_ = hit(x, y);
	if(captured_ != nullptr)
	{
		captured_->press(x, y, now_ms);
	}
}

void ModPage::drag(int x, int y, std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->drag(x, y, now_ms);
	}
}

void ModPage::release(std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->release(now_ms);
		captured_ = nullptr;
	}
}

void ModPage::update(std::uint32_t now_ms)
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
