// SPDX-License-Identifier: MIT
//
// The SEQ page: the 4x4 step grid full width, with the note box, play/stop,
// the selected step's chord and tie, and tempo on a bar above it.
//
//   +---------------+---+--------+-----+------------------+
//   | STEP 05 C#4 v | > | CHORD  | TIE |  120  ====O====  |
//   |               |   |  MIN7  |     |                  |
//   +---------------+---+--------+-----+------------------+
//   |  1    |  2    |  3    |  4    |
//   |  5    |  6    |  7    |  8    |
//   |  9    | 10    | 11    | 12    |
//   | 13    | 14    | 15    | 16    |
//   +-------------------------------+
//
// The page edits a sequencer::Pattern and the sequencer::Transport
// (play/stop), which the audio interrupt reads; the playhead comes back
// through the Transport. CHORD opens the chord picker for the selected step,
// TIE ties it to the step before (the previous note or chord rings on).
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
	const ChordPicker& chord_picker() const { return chord_picker_; }

private:
	static constexpr int kWidgets = 6;

	Widget* hit(int x, int y);
	// The modal picker on screen, if any.
	Widget* modal();

	Slider tempo_;
	PlayButton play_;
	StepGrid grid_;
	NoteBox note_box_;
	ChordButton chord_;
	TieButton tie_;
	NotePicker picker_;
	ChordPicker chord_picker_;

	// Every widget except the pickers, for hit-testing and drawing.
	Widget* widgets_[kWidgets];

	Widget* captured_ = nullptr;  // widget that owns the current touch
	bool picker_drawn_ = false;  // a picker was on screen last frame
};

}  // namespace ui
