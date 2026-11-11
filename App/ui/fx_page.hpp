// SPDX-License-Identifier: MIT
//
// The FX page: the effects after the voices (dsp::Effects), one row each.
//
//   MOD FX   TYPE (PHSR / FLNG / CHRS)  MODE (FREE / SYNC)
//            RATE or LEN   DEPTH   FBK   MIX
//   DELAY    MODE (FREE / SYNC)  STEREO (MONO / PING)
//            MS or LEN     FBK     TONE  MIX
//   REVERB                  DECAY   DAMP  MIX
//
// MIX is each effect's wet level, 0 = off (the dry signal always passes).
// As on the MOD page, the rate / time knob shows the free value (Hz-ish
// RATE, milliseconds) or the synced length depending on MODE; only the one
// in use is drawn and touched.
#pragma once

#include "ui/page.hpp"
#include "ui/widgets.hpp"

namespace ui
{

class FxPage : public Page
{
   public:
	explicit FxPage(Ctx ctx);

	void redraw_all() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;
	void update(std::uint32_t now_ms) override;

   private:
	static constexpr int kWidgets = 17;

	Widget* hit(int x, int y);
	bool active(const Widget* w) const;

	// MOD FX
	EnumButton mod_type_;
	EnumButton mod_mode_;
	Knob mod_rate_;
	Knob mod_len_;
	Knob mod_depth_;
	Knob mod_fbk_;
	Knob mod_mix_;
	// DELAY
	EnumButton dly_mode_;
	EnumButton dly_stereo_;
	Knob dly_time_;
	Knob dly_len_;
	Knob dly_fbk_;
	Knob dly_tone_;
	Knob dly_mix_;
	// REVERB
	Knob rev_decay_;
	Knob rev_damp_;
	Knob rev_mix_;

	params::Parameters& params_;
	Widget* widgets_[kWidgets];
	Widget* captured_ = nullptr;
	bool shown_sync_[2] = {false, false};  // MOD FX, DELAY
};

}  // namespace ui
