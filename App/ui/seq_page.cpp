// SPDX-License-Identifier: MIT
#include "ui/seq_page.hpp"

#include "display.hpp"
#include "ui/theme.hpp"

namespace ui
{
namespace
{

// --- Layout (kPageW x kPageH) -----------------------------------------------
// Top bar: note box, play/stop, chord, tie, tempo. Below: the 4x4 grid,
// full width.
constexpr Rect kNoteBox{2, 1, 124, 32};
constexpr Rect kPlay{130, 1, 36, 32};
constexpr Rect kChord{170, 1, 76, 32};
constexpr Rect kTie{250, 1, 44, 32};
constexpr Rect kTempo{298, 1, 180, 32};
constexpr Rect kGrid{0, 35, kPageW, 211};

}  // namespace

SeqPage::SeqPage(Ctx ctx)
    : tempo_(kTempo, ctx.params, params::Id::Tempo),
      play_(kPlay, ctx.transport),
      grid_(kGrid, ctx.pattern, ctx.transport),
      note_box_(kNoteBox, grid_, ctx.pattern),
      chord_(kChord, grid_, ctx.pattern),
      tie_(kTie, grid_, ctx.pattern),
      picker_(kGrid, ctx.pattern),
      chord_picker_(kGrid, ctx.pattern)
{
	widgets_[0] = &tempo_;
	widgets_[1] = &play_;
	widgets_[2] = &grid_;
	widgets_[3] = &note_box_;
	widgets_[4] = &chord_;
	widgets_[5] = &tie_;
	for(Widget* w : widgets_)
	{
		w->set_accent(theme::kSeqAccent);
	}
	picker_.set_accent(theme::kSeqAccent);
	chord_picker_.set_accent(theme::kSeqAccent);
}

Widget* SeqPage::modal()
{
	if(picker_.visible())
	{
		return &picker_;
	}
	if(chord_picker_.visible())
	{
		return &chord_picker_;
	}
	return nullptr;
}

void SeqPage::redraw_all()
{
	display::fill(theme::kBackground);
	display::fill_rect(0, 0, kPageW, 34, theme::kPanel);
	display::fill_rect(0, 34, kPageW, 1, theme::kHeader);
	for(Widget* w : widgets_)
	{
		w->invalidate();
	}
	picker_.invalidate();
	chord_picker_.invalidate();
}

Widget* SeqPage::hit(int x, int y)
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

void SeqPage::press(int x, int y, std::uint32_t now_ms)
{
	if(Widget* m = modal())
	{
		// A picker is modal: a touch outside it just dismisses it.
		if(m->rect().contains(x, y))
		{
			m->press(x, y, now_ms);
		}
		else
		{
			picker_.close();
			chord_picker_.close();
		}
		captured_ = nullptr;
		return;
	}
	captured_ = hit(x, y);
	if(captured_ != nullptr)
	{
		captured_->press(x, y, now_ms);
	}
}

void SeqPage::drag(int x, int y, std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->drag(x, y, now_ms);
	}
}

void SeqPage::release(std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->release(now_ms);
		captured_ = nullptr;
	}
}

void SeqPage::update(std::uint32_t now_ms)
{
	for(Widget* w : widgets_)
	{
		w->tick(now_ms);
	}
	// Either a long press on a step or a tap on the note box opens the picker
	// for the selected step.
	const bool want_picker = grid_.take_picker_request();
	const bool box_tapped = note_box_.take_request();
	const bool chord_tapped = chord_.take_request();
	if((want_picker || box_tapped) && grid_.selected() >= 0)
	{
		chord_picker_.close();
		picker_.open(grid_.selected());
	}
	else if(chord_tapped && grid_.selected() >= 0)
	{
		picker_.close();
		chord_picker_.open(grid_.selected());
	}

	for(Widget* w : widgets_)
	{
		w->sync();
	}

	if(Widget* m = modal())
	{
		// The picker covers the grid: draw everything else, then the picker.
		for(Widget* w : widgets_)
		{
			if(w != &grid_)
			{
				w->draw_if_dirty();
			}
		}
		m->draw_if_dirty();
		picker_drawn_ = true;
		return;
	}
	if(picker_drawn_)
	{
		picker_drawn_ = false;
		grid_.invalidate();  // uncover the grid
	}
	for(Widget* w : widgets_)
	{
		w->draw_if_dirty();
	}
}

}  // namespace ui
