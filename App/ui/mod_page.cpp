// SPDX-License-Identifier: MIT
#include "ui/mod_page.hpp"

#include "display.hpp"
#include "dsp/lfo.hpp"
#include "modulation/matrix.hpp"
#include "ui/theme.hpp"

namespace ui
{
namespace
{

using params::Id;

// --- Layout (kPageW x kPageH) ------------------------------------------------
// The matrix as two columns of three 44 px routing rows, each reading
// "SRC -> DST  amount": a CycleButton for the source, an arrow, one for the
// destination, and a bipolar amount bar. A row with amount 0 does nothing,
// so its buttons and arrow are drawn dimmed. The two LFOs ride in the strip
// below, 240 px apart: rate knob, shape button, mode button.
constexpr int kRowH = 44;
constexpr int kRowPitch = 48;
constexpr int kRowY0 = 14;

constexpr int kCellX[3] = {8, 84, 150};  // src, dst, amount
constexpr int kCellW[3] = {60, 60, 82};
constexpr int kCol2Off = 240;  // rows 4-6 repeat the column, offset
constexpr int kArrowX = 70;    // between src and dst, 12 px wide

constexpr Rect cell(int i, int c)
{
	return Rect{kCellX[c] + (i / 3) * kCol2Off, kRowY0 + (i % 3) * kRowPitch,
	            kCellW[c], kRowH};
}

constexpr int kSepY = 158;
constexpr int kLfoY = 172;
constexpr int kLfoPitch = 240;
constexpr Rect lfo_rate(int i) { return Rect{12 + i * kLfoPitch, kLfoY, 64, kRowH}; }
constexpr Rect lfo_shape(int i) { return Rect{82 + i * kLfoPitch, kLfoY, 72, kRowH}; }
constexpr Rect lfo_mode(int i) { return Rect{160 + i * kLfoPitch, kLfoY, 72, kRowH}; }

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

// The "->" between a row's source and destination, in the accent while the
// route is live, dim when its amount is zero.
void draw_arrow(int row, bool live)
{
	const Rect src = cell(row, 0);
	const int x = src.x - kCellX[0] + kArrowX;
	const int y = src.y + src.h / 2;
	const display::Color c = live ? theme::kModAccent : theme::kTrack;
	display::fill_rect(x, y - 4, 12, 9, theme::kBackground);
	display::fill_rect(x, y, 8, 1, c);
	for(int i = 0; i < 4; ++i)
	{
		display::fill_rect(x + 8 + i, y - 3 + i, 1, 7 - 2 * i, c);
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
      rate_{Knob(lfo_rate(0), ctx.params, Id::Lfo1Rate),
            Knob(lfo_rate(1), ctx.params, Id::Lfo2Rate)},
      len_{Knob(lfo_rate(0), ctx.params, Id::Lfo1Div),
           Knob(lfo_rate(1), ctx.params, Id::Lfo2Div)},
      shape_{ShapeButton(lfo_shape(0), ctx.params, Id::Lfo1Shape),
             ShapeButton(lfo_shape(1), ctx.params, Id::Lfo2Shape)},
      mode_{EnumButton(lfo_mode(0), ctx.params, Id::Lfo1Mode),
            EnumButton(lfo_mode(1), ctx.params, Id::Lfo2Mode)},
      params_(ctx.params)
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
	for(int i = 0; i < 2; ++i)
	{
		widgets_[n++] = &rate_[i];
		widgets_[n++] = &len_[i];
		widgets_[n++] = &shape_[i];
		widgets_[n++] = &mode_[i];
	}
	for(Widget* w : widgets_)
	{
		w->set_accent(theme::kModAccent);
	}
	for(int i = 0; i < modulation::kRows; ++i)
	{
		srcs_[i].set_gate(&ctx.matrix.rows[i].amount);
		dsts_[i].set_gate(&ctx.matrix.rows[i].amount);
		amts_[i].set_bar(true);
		amount_[i] = &ctx.matrix.rows[i].amount;
	}
}

void ModPage::redraw_all()
{
	display::fill(theme::kBackground);
	draw_section(8, 2, kPageW - 16, "MATRIX", theme::kModAccent);
	draw_section(8, kSepY, kPageW - 16, "LFO", theme::kModAccent);
	draw_row_tag(2, kLfoY, "LF1");
	draw_row_tag(kLfoPitch, kLfoY, "LF2");
	for(Widget* w : widgets_)
	{
		w->invalidate();
	}
	arrows_valid_ = false;
}

bool ModPage::active(const Widget* w) const
{
	for(int i = 0; i < 2; ++i)
	{
		const bool sync =
		    params_.get(i == 0 ? Id::Lfo1Mode : Id::Lfo2Mode) > 0.5f;
		if(w == &rate_[i])
		{
			return !sync;
		}
		if(w == &len_[i])
		{
			return sync;
		}
	}
	return true;
}

Widget* ModPage::hit(int x, int y)
{
	for(Widget* w : widgets_)
	{
		if(active(w) && w->rect().contains(x, y))
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
	// A mode change swaps the rate knob in that cell: draw the new one.
	for(int i = 0; i < 2; ++i)
	{
		const bool sync =
		    params_.get(i == 0 ? Id::Lfo1Mode : Id::Lfo2Mode) > 0.5f;
		if(sync != shown_sync_[i])
		{
			shown_sync_[i] = sync;
			(sync ? len_[i] : rate_[i]).invalidate();
		}
	}
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
		if(active(w))
		{
			w->draw_if_dirty();
		}
	}
	for(int i = 0; i < modulation::kRows; ++i)
	{
		const bool live = *amount_[i] != 0.0f;
		if(!arrows_valid_ || live != arrow_live_[i])
		{
			arrow_live_[i] = live;
			draw_arrow(i, live);
		}
	}
	arrows_valid_ = true;
}

// --- ShapeButton ------------------------------------------------------------

void ModPage::ShapeButton::sync()
{
	if(params_.get(id_) != shown_)
	{
		invalidate();
	}
}

void ModPage::ShapeButton::press(int, int, std::uint32_t)
{
	const params::Spec& s = params::spec(id_);
	const float v = params_.get(id_) + 1.0f;
	params_.set(id_, v > s.max ? s.min : v);
	pressed_ = true;
	invalidate();
}

void ModPage::ShapeButton::release(std::uint32_t)
{
	pressed_ = false;
	invalidate();
}

void ModPage::ShapeButton::draw()
{
	const Rect& r = rect_;
	const params::Spec& s = params::spec(id_);
	shown_ = params_.get(id_);
	const auto shape = static_cast<dsp::LfoShape>(static_cast<int>(shown_));
	display::fill_rect(r.x, r.y, r.w, r.h,
	                   pressed_ ? theme::kHeader : theme::kPanel);
	char text[8];
	format_value(s, shown_, text);
	const int tw = display::text_width(text, 2);
	display::draw_text(r.x + (r.w - tw) / 2, r.y + 4, text,
	                   pressed_ ? accent_ : theme::kText, 2);

	// The wave: one cycle across the cell, in a 14 px band under the name.
	// S&H gets a fixed pattern of held levels.
	static constexpr float kHeld[4] = {0.5f, -0.8f, 0.9f, -0.2f};
	const int x0 = r.x + 8;
	const int w = r.w - 16;
	const int mid = r.y + 32;
	const int amp = 6;
	int prev = mid;
	for(int i = 0; i < w; ++i)
	{
		const float p = static_cast<float>(i) / static_cast<float>(w);
		const float v =
		    shape == dsp::LfoShape::SampleHold
		        ? kHeld[(i * 4) / w]
		        : dsp::Lfo::shape_at(shape, p, 0.0f);
		const int y = mid - static_cast<int>(v * static_cast<float>(amp) +
		                                     (v < 0.0f ? -0.5f : 0.5f));
		if(i > 0)
		{
			display::draw_line(x0 + i - 1, prev, x0 + i, y, accent_);
		}
		prev = y;
	}
}

}  // namespace ui
