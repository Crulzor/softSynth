// SPDX-License-Identifier: MIT
//
// The SEQ page: the 4x4 step grid full width, with the note box, play/stop
// and tempo on a bar above it.
//
//   +-------------------------------+--------------------------------+
//   | STEP 05 C#4 v | > |  120  ====O====    |                      |
//   +-------------------------------+--------------------------------+
//   |  1    |  2    |  3    |  4    |                              |
//   |  5    |  6    |  7    |  8    |                              |
//   |  9    | 10    | 11    | 12    |                              |
//   | 13    | 14    | 15    | 16    |                              |
//   +----------------------------------------------+
//
// Nothing here drives sound yet: the page edits a sequencer::Pattern and the
// sequencer::Transport (play/stop), which the audio interrupt reads. The
// playhead comes back through the Transport.
#pragma once

#include <cstdint>

#include "ui/page.hpp"
#include "ui/widgets.hpp"

namespace ui
{

class SeqPage : public Page
{
public:
	explicit SeqPage(Ctx ctx);

	// Clear the page area and draw everything.
	void redraw_all() override;

	// Touch, already split into press / drag / release by ui::Ui.
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;

	// Once per UI frame.
	void update(std::uint32_t now_ms) override;

	// For tests.
	const StepGrid& grid() const { return grid_; }
	const NotePicker& picker() const { return picker_; }

private:
	static constexpr int kWidgets = 4;

	Widget* hit(int x, int y);

	Slider tempo_;
	PlayButton play_;
	StepGrid grid_;
	NoteBox note_box_;
	NotePicker picker_;

	// Every widget except the picker, for hit-testing and drawing.
	Widget* widgets_[kWidgets];

	Widget* captured_ = nullptr;  // widget that owns the current touch
	bool picker_drawn_ = false;  // picker was on screen last frame
};

}  // namespace ui
