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
#include "ui/theme.hpp"

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

// Section heading in place of a grey title bar: `label` in the section's
// accent at (x, y), then a thin rule in a darker shade of it out to x + w.
// 8 px tall, drawn on the page background.
void draw_section(int x, int y, int w, const char* label,
                  display::Color accent);

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

	// The colour this widget draws its value / "on" state in. Pages set it
	// to their section accent at construction.
	void set_accent(display::Color c) { accent_ = c; }

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
	display::Color accent_ = theme::kAccent;
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

	// Large style: a big knob with a cap and pointer (or a tall fader) and
	// the value at scale 2, for full-page layouts. Needs a cell of about
	// 84 x 100. The default compact style fits a 36 x 44 cell.
	void set_large(bool large) { large_ = large; }

   protected:
	// Signed travel in pixels for a drag of (dx, dy) from the press point;
	// positive increases the value.
	virtual int travel(int dx, int dy) const = 0;

	params::Parameters& params_;
	params::Id id_;
	float shown_ = 0.0f;  // value at the last draw
	bool active_ = false;
	bool large_ = false;

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

// A knob bound to a raw float instead of a params::Id, for model values that
// live outside the parameter set (the matrix's amounts). Same interaction as
// a knob: relative drag, a bipolar ring when the range straddles zero; the
// value text shows round(value * text_scale).
class FloatKnob : public Widget
{
public:
	FloatKnob(Rect r, float* value, float min, float max, float step,
	          int text_scale, const char* label)
	    : Widget(r),
	      value_(value),
	      min_(min),
	      max_(max),
	      step_(step),
	      text_scale_(text_scale),
	      label_(label)
	{
	}

	void sync() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void drag(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;

	// Bar style: the value at scale 2 over a horizontal bar (filled from the
	// centre for a bipolar range) instead of a ring. Same drag. For the MOD
	// page's routing rows.
	void set_bar(bool bar) { bar_ = bar; }

   protected:
	void draw() override;

private:
	float normalized() const;
	void set_normalized(float n);

	float* value_;
	float min_;
	float max_;
	float step_;
	int text_scale_;
	const char* label_;
	float shown_ = 0.0f;
	bool active_ = false;
	bool bar_ = false;
	int x0_ = 0;
	int y0_ = 0;
	float n0_ = 0.0f;
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
// without toggling and ask for the note picker. An on-step is a cell tinted
// in the accent with its note name large in the middle; the step the
// sequencer is playing (Transport::step) gets an accent outline, the selected
// one a white one; beat numbers (1, 5, 9, 13) are brighter.
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

// Bottom tab bar: one button per page, the active page's button filled with
// that page's accent. Any tab can carry a badge dot (the MIDI tab's link
// LED). The space right of the tabs is a status strip: transport icon, BPM,
// a 16-pip step row (pattern on-steps lit, the playhead in the SEQ accent)
// and the audio CPU load. A status message (an init failure) replaces the
// strip while set.
class TabBar : public Widget
{
public:
 TabBar(Rect r, int num_tabs, const char* const* tab_names,
	    const display::Color* tab_accents,
	    const sequencer::Transport& transport,
	    const sequencer::Pattern& pattern, const params::Parameters& params)
	 : Widget(r),
	   num_tabs_(num_tabs),
	   tab_names_(tab_names),
	   tab_accents_(tab_accents),
	   transport_(transport),
	   pattern_(pattern),
	   params_(params)
 {
 }

	int active() const { return active_; }
	void set_active(int i) { active_ = i; }

	// The tab pressed this frame, or -1 if the press was outside the tabs.
	int take_switch();

	// Short message shown at the right (e.g. an init failure); nullptr clears.
	void set_status(const char* text) { status_ = text; }

	// Average audio-interrupt load, 0..1, for the strip.
	void set_cpu_load(float average)
	{
		cpu_pct_ = static_cast<int>(average * 100.0f + 0.5f);
	}

	// Badge dot on tab `i` in `c`; `on` = false hides it.
	void set_badge(int i, bool on, display::Color c)
	{
		badge_tab_ = i;
		badge_on_ = on;
		badge_ = c;
	}

	// For tests: the x where the status strip begins.
	int strip_x() const { return rect_.x + num_tabs_ * kTabW; }

	void sync() override;
	void press(int x, int y, std::uint32_t now_ms) override;

protected:
	void draw() override;

private:
	static constexpr int kTabW = 48;

	Rect tab(int i) const;
	int tab_at(int x, int y) const;
	void draw_strip();

	int num_tabs_;
	const char* const* tab_names_;
	const display::Color* tab_accents_;
	const sequencer::Transport& transport_;
	const sequencer::Pattern& pattern_;
	const params::Parameters& params_;
	int active_ = 0;
	const char* status_ = nullptr;
	int switch_ = -1;
	int cpu_pct_ = 0;
	int badge_tab_ = -1;
	bool badge_on_ = false;
	display::Color badge_{0, 0, 0};
};

// Oscilloscope slot: a panel cell with a graticule (quarter divisions, a
// brighter zero line) and the trace in the widget's accent. Display only, no
// touch. For now there is no sample source, so the trace is the flat zero
// line; the source (a snapshot of the oscillator output handed from the
// audio side) gets wired in here once the real oscillator lands.
class Scope : public Widget
{
   public:
	explicit Scope(Rect r) : Widget(r) {}

   protected:
	void draw() override;
};

// Tap to cycle an enum value one step (wrapping); shows the current entry's
// name. The MOD matrix's source / destination cells are these.
class CycleButton : public Widget
{
public:
	CycleButton(Rect r, int* value, int count, const char* const* names)
	    : Widget(r), value_(value), count_(count), names_(names)
	{
	}

	// Draw dimmed while *gate == 0 (a matrix row whose amount is zero does
	// nothing, so its source / destination read as inactive).
	void set_gate(const float* gate) { gate_ = gate; }

	void sync() override;
	void press(int x, int y, std::uint32_t now_ms) override;
	void release(std::uint32_t now_ms) override;

protected:
	void draw() override;

private:
 bool live() const { return gate_ == nullptr || *gate_ != 0.0f; }

 int* value_;
 int count_;
 const char* const* names_;
 const float* gate_ = nullptr;
 int shown_ = -1;
 bool shown_live_ = true;
 bool pressed_ = false;
};

}  // namespace ui
