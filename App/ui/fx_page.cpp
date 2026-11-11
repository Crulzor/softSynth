// SPDX-License-Identifier: MIT
#include "ui/fx_page.hpp"

#include "display.hpp"
#include "ui/theme.hpp"

namespace ui
{
namespace
{

using params::Id;

// --- Layout (kPageW x kPageH) ------------------------------------------------
// Three rows 82 px apart under section rules: two 76 px buttons, then four
// 70 px knob columns; MIX always in the last column.
constexpr int kRuleY[3] = {4, 86, 168};
constexpr int kCellH = 44;

constexpr Rect btn(int row, int i)
{
	return Rect{8 + i * 82, kRuleY[row] + 12, 76, kCellH};
}

constexpr Rect knob(int row, int col)
{
	return Rect{172 + col * 76, kRuleY[row] + 12, 70, kCellH};
}

constexpr const char* kTitles[2] = {"MOD FX", "DELAY"};
// Row 3 holds two sections: OUT (master volume) on the left, REVERB under
// its knobs on the right.
constexpr int kOutW = 158;
constexpr int kReverbX = 248;

}  // namespace

FxPage::FxPage(Ctx ctx)
    : mod_type_(btn(0, 0), ctx.params, Id::ModFxType),
      mod_mode_(btn(0, 1), ctx.params, Id::ModFxMode),
      mod_rate_(knob(0, 0), ctx.params, Id::ModFxRate),
      mod_len_(knob(0, 0), ctx.params, Id::ModFxDiv),
      mod_depth_(knob(0, 1), ctx.params, Id::ModFxDepth),
      mod_fbk_(knob(0, 2), ctx.params, Id::ModFxFeedback),
      mod_mix_(knob(0, 3), ctx.params, Id::ModFxMix),
      dly_mode_(btn(1, 0), ctx.params, Id::DelayMode),
      dly_stereo_(btn(1, 1), ctx.params, Id::DelayStereo),
      dly_time_(knob(1, 0), ctx.params, Id::DelayTime),
      dly_len_(knob(1, 0), ctx.params, Id::DelayDiv),
      dly_fbk_(knob(1, 1), ctx.params, Id::DelayFeedback),
      dly_tone_(knob(1, 2), ctx.params, Id::DelayTone),
      dly_mix_(knob(1, 3), ctx.params, Id::DelayMix),
      rev_decay_(knob(2, 1), ctx.params, Id::ReverbDecay),
      rev_damp_(knob(2, 2), ctx.params, Id::ReverbDamp),
      rev_mix_(knob(2, 3), ctx.params, Id::ReverbMix),
      volume_(Rect{8, kRuleY[2] + 12, 70, kCellH}, ctx.params,
	          Id::MasterVolume),
      params_(ctx.params)
{
	Widget* all[] = {&mod_type_,   &mod_mode_, &mod_rate_,  &mod_len_,
	                 &mod_depth_,  &mod_fbk_,  &mod_mix_,   &dly_mode_,
	                 &dly_stereo_, &dly_time_, &dly_len_,   &dly_fbk_,
	                 &dly_tone_,   &dly_mix_,  &rev_decay_, &rev_damp_,
	                 &rev_mix_,    &volume_};
	int n = 0;
	for(Widget* w : all)
	{
		w->set_accent(theme::kFxAccent);
		widgets_[n++] = w;
	}
}

bool FxPage::active(const Widget* w) const
{
	const bool mod_sync = params_.get(Id::ModFxMode) > 0.5f;
	const bool dly_sync = params_.get(Id::DelayMode) > 0.5f;
	if(w == &mod_rate_)
	{
		return !mod_sync;
	}
	if(w == &mod_len_)
	{
		return mod_sync;
	}
	if(w == &dly_time_)
	{
		return !dly_sync;
	}
	if(w == &dly_len_)
	{
		return dly_sync;
	}
	return true;
}

void FxPage::redraw_all()
{
	display::fill(theme::kBackground);
	for(int r = 0; r < 2; ++r)
	{
		draw_section(8, kRuleY[r], kPageW - 16, kTitles[r], theme::kFxAccent);
	}
	draw_section(8, kRuleY[2], kOutW, "OUT", theme::kFxAccent);
	draw_section(kReverbX, kRuleY[2], kPageW - 8 - kReverbX, "REVERB",
	             theme::kFxAccent);
	for(Widget* w : widgets_)
	{
		w->invalidate();
	}
}

Widget* FxPage::hit(int x, int y)
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

void FxPage::press(int x, int y, std::uint32_t now_ms)
{
	captured_ = hit(x, y);
	if(captured_ != nullptr)
	{
		captured_->press(x, y, now_ms);
	}
}

void FxPage::drag(int x, int y, std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->drag(x, y, now_ms);
	}
}

void FxPage::release(std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->release(now_ms);
		captured_ = nullptr;
	}
}

void FxPage::update(std::uint32_t now_ms)
{
	// A mode change swaps the knob in that cell: draw the one now in use.
	const bool sync[2] = {params_.get(Id::ModFxMode) > 0.5f,
	                      params_.get(Id::DelayMode) > 0.5f};
	Knob* free_knob[2] = {&mod_rate_, &dly_time_};
	Knob* sync_knob[2] = {&mod_len_, &dly_len_};
	for(int i = 0; i < 2; ++i)
	{
		if(sync[i] != shown_sync_[i])
		{
			shown_sync_[i] = sync[i];
			(sync[i] ? sync_knob[i] : free_knob[i])->invalidate();
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
}

}  // namespace ui
