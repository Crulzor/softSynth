// SPDX-License-Identifier: MIT
#include "ui/ui.hpp"

#include "midi/midi.hpp"

namespace ui
{

Ui::Ui(params::Parameters& params, sequencer::Pattern& pattern,
       sequencer::Transport& transport, modulation::Matrix& matrix,
       params::WaveSlots& waves)
    : ctx_{params, pattern, transport, matrix, waves},
      osc_(ctx_),
      env_(ctx_),
      mod_(ctx_),
      seq_(ctx_),
      midi_(ctx_),
      dbg_(ctx_),
      bar_(Rect{0, kPageH, kPageW, kTabBarH}, kNumPages, kPageNames,
	       kPageAccents, transport, pattern, params)
{
	pages_[0] = &osc_;
	pages_[1] = &env_;
	pages_[2] = &mod_;
	pages_[3] = &seq_;
	pages_[4] = &midi_;
	pages_[5] = &dbg_;
}

// The MIDI tab's LED: off with nothing attached, amber while enumerating,
// green once the MIDI class runs (flashing white as packets arrive), red for
// a device without a MIDI interface.
void Ui::update_midi_led(std::uint32_t now_ms)
{
	const std::uint32_t packets =
	    midi::parser().packets.load(std::memory_order_relaxed);
	if(packets != seen_packets_)
	{
		seen_packets_ = packets;
		flash_until_ = now_ms + kMidiFlashMs;
	}
	switch(link_)
	{
		case midi::LinkState::NoDevice:
			bar_.set_badge(kMidiTab, false, theme::kTrack);
			break;
		case midi::LinkState::Enumerating:
			bar_.set_badge(kMidiTab, true, theme::kBusy);
			break;
		case midi::LinkState::Ready:
			bar_.set_badge(kMidiTab, true,
			               static_cast<std::int32_t>(flash_until_ - now_ms) > 0
			                   ? theme::kText
							   : theme::kOk);
			break;
		case midi::LinkState::NotMidi:
			bar_.set_badge(kMidiTab, true, theme::kWarn);
			break;
	}
}

void Ui::switch_page(int i)
{
	page_ = i;
	pages_[page_]->redraw_all();
	needs_full_ = true;
}

void Ui::redraw_all()
{
	pages_[page_]->redraw_all();
	bar_.invalidate();
	bar_.sync();
	bar_.draw_if_dirty();
	needs_full_ = true;
}

void Ui::update(const Touch& touch, std::uint32_t now_ms)
{
	// Consumed before the touch handling below: a switch made by this
	// frame's own press must not eat the flag it just set, the stale buffer
	// is repainted on the frame *after* the switch frame has been promoted.
	if(needs_full_)
	{
		pages_[page_]->redraw_all();
		needs_full_ = false;
	}

	if(touch.pressed && !was_pressed_)
	{
		if(bar_.rect().contains(touch.x, touch.y))
		{
			bar_.press(touch.x, touch.y, now_ms);
			const int i = bar_.take_switch();
			if(i >= 0 && i != page_)
			{
				switch_page(i);
			}
		}
		else
		{
			pages_[page_]->press(touch.x, touch.y, now_ms);
		}
	}
	else if(touch.pressed)
	{
		pages_[page_]->drag(touch.x, touch.y, now_ms);
	}
	else if(was_pressed_)
	{
		pages_[page_]->release(now_ms);
	}
	was_pressed_ = touch.pressed;

	// The page may have redrawn over the bar; the bar always draws last.
	update_midi_led(now_ms);
	bar_.invalidate();
	bar_.sync();
	pages_[page_]->update(now_ms);
	bar_.draw_if_dirty();
}

}  // namespace ui
