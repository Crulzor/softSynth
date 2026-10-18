// SPDX-License-Identifier: MIT
//
// Small retained-mode widget set on top of the display module.
//
// Each widget owns a screen rectangle, draws only itself, and only when it is
// dirty: sync() compares what it last drew against the model (the parameter
// set / the pattern) and marks itself dirty on a change. So a value changed
// from anywhere - touch, a pot, CV, a preset load - shows up on screen without
// the UI being told, and an idle screen costs no drawing at all. That matters
// because we draw straight into the scanned-out framebuffer: less redrawing
// means less flicker.
//
// Touch arrives as press / drag / release, routed by the page to whichever
// widget was under the finger on press (that widget keeps the touch until
// release, even if the finger slides off it).
//
// No HAL: this compiles on the host too, so layouts and interactions are
// unit-tested and rendered to an image under test/.
#pragma once

#include <cstdint>

#include "params/parameters.hpp"
#include "sequencer/pattern.hpp"
#include "sequencer/sequencer.hpp"

namespace ui
{

struct Rect
{
	int x;
	int y;
	int w;
	int h;

	bool contains(int px, int py) const
	{
		return px >= x && px < x + w && py >= y && py < y + h;
	}
};

// Value text for a parameter as the UI shows it ("+7", "64", "AMP"). `out`
// needs room for 8 chars including the NUL.
void format_value(const params::Spec& spec, float value, char* out);

// MIDI note name: 60 -> "C4", 61 -> "C#4". `out` needs 5 chars.
void format_note(std::uint8_t note, char* out);

// Decimal integer, "-" for negatives, "+" for positives if `force_sign`.
// Returns the pointer to the terminating NUL (for appending).
char* format_int(int v, char* out, bool force_sign = false);

class Widget
{
public:
	explicit Widget(Rect r) : rect_(r) {}

	const Rect& rect() const { return rect_; }

	// Once per UI frame: compare against the model, invalidate() on change.
	virtual void sync() {}

	virtual void press(int /*x*/, int /*y*/, std::uint32_t /*now_ms*/) {}
	virtual void drag(int /*x*/, int /*y*/, std::uint32_t /*now_ms*/) {}
	virtual void release(std::uint32_t /*now_ms*/) {}

	// Once per UI frame, for time-based behaviour (long press).
	virtual void tick(std::uint32_t /*now_ms*/) {}

	virtual void invalidate() { dirty_ = true; }

	void draw_if_dirty()
	{
		if(dirty_)
		{
			dirty_ = false;
			draw();
		}
	}

protected:
	// Widgets live in static storage and are never deleted through a base
	// pointer, so no virtual destructor (and no operator delete pulled in).
	~Widget() = default;

	virtual void draw() = 0;

	Rect rect_;
	bool dirty_ = true;
};

// A parameter edited by *relative* drag, like turning a knob: the value moves
// by how far the finger travels, not to where it lands, so even small cells
// stay precise and nothing jumps on touch-down.
class ParamControl : public Widget
{
public:
	ParamControl(Rect r, params::Parameters& p, params::Id id)
	    : Widget(r), params_(p), id_(id)
	{
	}

	void sync() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;

protected:
	// Signed travel in pixels for a drag of (dx, dy) from the press point;
	// positive increases the value.
	virtual int travel(int dx, int dy) const = 0;

	params::Parameters& params_;
	params::Id id_;
	float shown_ = 0.0f;  // value at the last draw
	bool active_ = false;

private:
	int x0_ = 0;
	int y0_ = 0;
	float n0_ = 0.0f;  // normalized value at press
};

// Elektron-style mini knob: arc showing the value, value text, label.
// Drag right or up to increase. Bipolar parameters fill from 12 o'clock.
class Knob : public ParamControl
{
public:
	using ParamControl::ParamControl;

protected:
	int travel(int dx, int dy) const override { return dx - dy; }
	void draw() override;
};

// Vertical crossfader for the oscillator mix: top = osc 1, bottom = osc 2,
// matching the two oscillator rows beside it. Drag down towards osc 2.
class CrossFader : public ParamControl
{
public:
	using ParamControl::ParamControl;

protected:
	int travel(int /*dx*/, int dy) const override { return dy; }
	void draw() override;
};

// Horizontal slider with *absolute* positioning (the value follows the
// finger). Used for tempo, where the track is long enough to aim at.
class Slider : public Widget
{
public:
	Slider(Rect r, params::Parameters& p, params::Id id)
	    : Widget(r), params_(p), id_(id)
	{
	}

	void sync() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;

protected:
	void draw() override;

private:
	Rect track() const;
	void set_from_x(int x);

	params::Parameters& params_;
	params::Id id_;
	float shown_ = 0.0f;
	bool active_ = false;
};

// 4x4 step grid. Tap a step: toggle it and select it. Hold a step: select it
// without toggling and ask for the note picker. The step the sequencer is
// playing (Transport::step) is shaded.
class StepGrid : public Widget
{
public:
	static constexpr std::uint32_t kLongPressMs = 450;

	StepGrid(Rect r, sequencer::Pattern& pattern,
	         const sequencer::Transport& transport)
	    : Widget(r), pattern_(pattern), transport_(transport)
	{
	}

	int selected() const { return selected_; }
	void select(int step);

	// True once after a long press asked for the note picker.
	bool take_picker_request();

	void sync() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;
	void tick(std::uint32_t now_ms) override;
	void invalidate() override;

protected:
	void draw() override;

private:
	int step_at(int x, int y) const;  // -1 if in a gap / outside
	Rect cell(int step) const;
	void mark(int step);
	void draw_cell(int step);

	sequencer::Pattern& pattern_;
	const sequencer::Transport& transport_;
	int selected_ = -1;
	int playhead_ = -1;
	std::uint16_t dirty_cells_ = 0xFFFF;

	// What each cell showed at its last draw, for sync().
	bool shown_on_[sequencer::kSteps] = {};
	std::uint8_t shown_note_[sequencer::kSteps] = {};

	// Touch in progress.
	int press_step_ = -1;  // step under the finger at press
	int over_step_ = -1;   // step under the finger now
	std::uint32_t press_ms_ = 0;
	bool long_fired_ = false;
	bool picker_request_ = false;
};

// Play / stop toggle for the sequencer (writes Transport::run).
class PlayButton : public Widget
{
public:
	PlayButton(Rect r, sequencer::Transport& transport)
	    : Widget(r), transport_(transport)
	{
	}

	void sync() override;
	void press(int x, int y, std::uint32_t now_ms) override;

protected:
	void draw() override;

private:
	sequencer::Transport& transport_;
	bool shown_run_ = false;
};

// Shows the selected step and its note; tap to open the note picker.
class NoteBox : public Widget
{
public:
	NoteBox(Rect r, const StepGrid& grid, const sequencer::Pattern& pattern)
	    : Widget(r), grid_(grid), pattern_(pattern)
	{
	}

	// True once after a tap while a step is selected.
	bool take_request();

	void sync() override;
	void press(int x, int y, std::uint32_t now_ms) override;

protected:
	void draw() override;

private:
	const StepGrid& grid_;
	const sequencer::Pattern& pattern_;
	int shown_step_ = -2;
	std::uint8_t shown_note_ = 0;
	bool request_ = false;
};

// Modal one-octave keyboard over the step grid, with octave -/+ and close.
// Picking a key sets the step's note, switches the step on and closes.
class NotePicker : public Widget
{
public:
	NotePicker(Rect r, sequencer::Pattern& pattern)
	    : Widget(r), pattern_(pattern)
	{
	}

	void open(int step);
	void close() { visible_ = false; }
	bool visible() const { return visible_; }

	void press(int x, int y, std::uint32_t now_ms) override;

protected:
	void draw() override;

private:
	static constexpr int kMinOctave = 0;
	static constexpr int kMaxOctave = 8;

	Rect minus_button() const;
	Rect plus_button() const;
	Rect close_button() const;
	Rect white_key(int i) const;     // i = 0..6, C D E F G A B
	Rect black_key(int i) const;     // i = 0..4, C# D# F# G# A#
	int key_at(int x, int y) const;  // semitone 0..11, or -1

	sequencer::Pattern& pattern_;
	bool visible_ = false;
	int step_ = 0;
	int octave_ = 4;
};

}  // namespace ui
