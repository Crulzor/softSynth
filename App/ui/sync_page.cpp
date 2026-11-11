// SPDX-License-Identifier: MIT
#include "ui/sync_page.hpp"

#include "display.hpp"
#include "ui/theme.hpp"

namespace ui
{
namespace
{

using params::Id;

// --- Layout (kPageW x kPageH) ------------------------------------------------
constexpr int kMargin = 8;
constexpr int kClockRuleY = 4;
constexpr Rect kSource{kMargin, 16, 108, 60};
constexpr Rect kDivision{kMargin + 116, 16, 108, 60};
constexpr Rect kTempo{kMargin + 232, 16, kPageW - 2 * kMargin - 232, 60};
constexpr int kInputRuleY = 86;
constexpr Rect kClockMeter{kMargin, 98, kPageW - 2 * kMargin, 40};
constexpr Rect kResetMeter{kMargin, 144, kPageW - 2 * kMargin, 40};
constexpr int kHintY = 202;

int pct(float v)
{
	const int p = static_cast<int>(v * 100.0f + 0.5f);
	return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// A small round-ish light: lit in `c`, else a dark ring.
void draw_light(int x, int y, bool lit, display::Color c)
{
	const display::Color off = theme::kTrack;
	display::fill_rect(x + 1, y, 8, 10, lit ? c : off);
	display::fill_rect(x, y + 1, 10, 8, lit ? c : off);
}

}  // namespace

// --- Meter
// --------------------------------------------------------------------

void SyncPage::Meter::tick(std::uint32_t now_ms)
{
	now_ = now_ms;
	const std::uint32_t edges = mon_.edges[channel_];
	if(edges != seen_edges_)
	{
		seen_edges_ = edges;
		lit_until_ = now_ms + kBlinkMs;
	}
	lit_ = static_cast<std::int32_t>(lit_until_ - now_ms) > 0;
}

void SyncPage::Meter::sync()
{
	if(pct(mon_.level[channel_]) / 2 != shown_level_ / 2 ||
	   pct(mon_.threshold[channel_]) != shown_thr_ || lit_ != shown_lit_ ||
	   mon_.edges[channel_] != shown_edges_)
	{
		invalidate();
	}
}

void SyncPage::Meter::draw()
{
	const Rect& r = rect_;
	shown_level_ = pct(mon_.level[channel_]);
	shown_thr_ = pct(mon_.threshold[channel_]);
	shown_lit_ = lit_;
	shown_edges_ = mon_.edges[channel_];

	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);
	display::draw_text(r.x + 6, r.y + (r.h - 8) / 2, label_, theme::kText);

	// The bar: 0 .. full scale; the level in the accent, the threshold as a
	// white tick, clipping (>= 99 %) in the warning colour.
	const int bx = r.x + 84;
	const int bw = r.w - 84 - 110;
	const int by = r.y + (r.h - 12) / 2;
	display::fill_rect(bx, by, bw, 12, theme::kTrack);
	const int fill = (bw * shown_level_) / 100;
	display::fill_rect(bx, by, fill, 12,
	                   shown_level_ >= 99 ? theme::kWarn : accent_);
	const int tx = bx + (bw * shown_thr_) / 100;
	display::fill_rect(tx, by - 3, 2, 18, theme::kText);

	draw_light(bx + bw + 14, r.y + (r.h - 10) / 2, lit_, accent_);
	char text[12];
	format_int(static_cast<int>(shown_edges_ % 100000u), text);
	display::draw_text(r.x + r.w - 6 - display::text_width(text),
	                   r.y + (r.h - 8) / 2, text, theme::kDim);
}

// --- TempoReadout
// ----------------------------------------------------------------

void SyncPage::TempoReadout::tick(std::uint32_t now_ms)
{
	// Blinks on clock edges only while the clock input drives the sequencer
	// (the meters show input activity either way).
	now_ = now_ms;
	const std::uint32_t edges = mon_.edges[0];
	if(edges != seen_edges_)
	{
		seen_edges_ = edges;
		if(params_.get(Id::ClockSource) > 0.5f)
		{
			lit_until_ = now_ms + kBlinkMs;
		}
	}
	lit_ = static_cast<std::int32_t>(lit_until_ - now_ms) > 0;
}

void SyncPage::TempoReadout::sync()
{
	const int src = static_cast<int>(params_.get(Id::ClockSource));
	const float bpm = src == 0 ? params_.get(Id::Tempo) : mon_.bpm;
	const int b = static_cast<int>(bpm + 0.5f);
	if(src != shown_src_ || b != shown_bpm_ || lit_ != shown_lit_)
	{
		invalidate();
	}
}

void SyncPage::TempoReadout::draw()
{
	const Rect& r = rect_;
	const int src = static_cast<int>(params_.get(Id::ClockSource));
	const float bpm = src == 0 ? params_.get(Id::Tempo) : mon_.bpm;
	shown_src_ = src;
	shown_bpm_ = static_cast<int>(bpm + 0.5f);
	shown_lit_ = lit_;

	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);
	char text[12];
	if(src != 0 && shown_bpm_ <= 0)
	{
		display::draw_text(r.x + 10, r.y + 14, "NO CLOCK", theme::kWarn, 2);
	}
	else
	{
		format_int(shown_bpm_, text);
		const int x = display::draw_text(r.x + 10, r.y + 10, text,
		                                 src == 0 ? theme::kText : accent_, 3);
		display::draw_text(x + 6, r.y + 24, "BPM", theme::kDim);
	}
	display::draw_text(r.x + 10, r.y + r.h - 14,
	                   src == 0 ? "INTERNAL" : "EXTERNAL", theme::kDim);
	draw_light(r.x + r.w - 20, r.y + 10, lit_, accent_);
}

// --- SyncPage
// ------------------------------------------------------------------

SyncPage::SyncPage(Ctx ctx)
    : source_(kSource, ctx.params, Id::ClockSource),
      division_(kDivision, ctx.params, Id::ClockDiv),
      tempo_(kTempo, ctx.params, ctx.transport.clock),
      clock_(kClockMeter, ctx.transport.clock, 0, "L CLOCK"),
      reset_(kResetMeter, ctx.transport.clock, 1, "R RESET")
{
	widgets_[0] = &source_;
	widgets_[1] = &division_;
	widgets_[2] = &tempo_;
	widgets_[3] = &clock_;
	widgets_[4] = &reset_;
	for(Widget* w : widgets_)
	{
		w->set_accent(theme::kSyncAccent);
	}
}

void SyncPage::redraw_all()
{
	display::fill(theme::kBackground);
	draw_section(kMargin, kClockRuleY, kPageW - 2 * kMargin, "CLOCK",
	             theme::kSyncAccent);
	draw_section(kMargin, kInputRuleY, kPageW - 2 * kMargin, "INPUT",
	             theme::kSyncAccent);
	display::draw_text(kMargin, kHintY,
	                   "CLOCK ON L (TIP), RESET ON R (RING) OF THE LINE IN.",
	                   theme::kDim);
	display::draw_text(kMargin, kHintY + 14,
	                   "LINE LEVEL ONLY: ATTENUATE 5-10 V GATES TO ~1 V.",
	                   theme::kDim);
	for(Widget* w : widgets_)
	{
		w->invalidate();
	}
}

Widget* SyncPage::hit(int x, int y)
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

void SyncPage::press(int x, int y, std::uint32_t now_ms)
{
	captured_ = hit(x, y);
	if(captured_ != nullptr)
	{
		captured_->press(x, y, now_ms);
	}
}

void SyncPage::release(std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->release(now_ms);
		captured_ = nullptr;
	}
}

void SyncPage::update(std::uint32_t now_ms)
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
