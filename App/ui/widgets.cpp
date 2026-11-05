// SPDX-License-Identifier: MIT
#include "ui/widgets.hpp"

#include <cmath>

#include "display.hpp"
#include "ui/theme.hpp"

namespace ui
{
namespace
{

// Relative-drag sensitivity: a continuous parameter sweeps its whole range
// over this many pixels of travel; a stepped one moves a step per kPxPerStep.
constexpr float kPxFullRange = 160.0f;
constexpr float kPxPerStep = 14.0f;

constexpr float kTwoPi = 6.28318530718f;

// Knob geometry: ring radii and the arc it sweeps (0.75 turn, gap at the
// bottom, like a real pot).
constexpr int kKnobOuter = 12;
constexpr int kKnobInner = 9;
constexpr float kKnobSweep = 0.75f;

// Integer to decimal text. Returns the pointer past the last digit written.
char* put_int(int v, char* out, bool force_sign)
{
	if(v < 0)
	{
		*out++ = '-';
		v = -v;
	}
	else if(force_sign && v > 0)
	{
		*out++ = '+';
	}
	char digits[10];
	int n = 0;
	do
	{
		digits[n++] = static_cast<char>('0' + v % 10);
		v /= 10;
	} while(v > 0);
	while(n > 0)
	{
		*out++ = digits[--n];
	}
	*out = '\0';
	return out;
}

int round_to_int(float v)
{
	return static_cast<int>(v < 0.0f ? v - 0.5f : v + 0.5f);
}

// Draw `s` horizontally centred in `r` with its top at `y`.
void draw_centered(const Rect& r, int y, const char* s, display::Color c,
                   int scale = 1)
{
	const int x = r.x + (r.w - display::text_width(s, scale)) / 2;
	display::draw_text(x, y, s, c, scale);
}

// The knob ring centred on (cx, cy). `n` is the normalized value; bipolar
// rings light the arc between 12 o'clock and the value instead of from the
// start of the sweep.
void draw_ring(int cx, int cy, float n, bool bipolar)
{
	const float start = -kKnobSweep / 2.0f;  // in turns, 0 = 12 o'clock
	for(int dy = -kKnobOuter; dy <= kKnobOuter; ++dy)
	{
		for(int dx = -kKnobOuter; dx <= kKnobOuter; ++dx)
		{
			const int d2 = dx * dx + dy * dy;
			if(d2 > kKnobOuter * kKnobOuter || d2 < kKnobInner * kKnobInner)
			{
				continue;
			}
			// Clockwise angle from 12 o'clock in turns, -0.5..0.5. Screen y
			// points down, hence -dy.
			const float turns =
			    std::atan2(static_cast<float>(dx), static_cast<float>(-dy)) /
			    kTwoPi;
			const float pos = (turns - start) / kKnobSweep;  // 0..1 on the arc
			if(pos < 0.0f || pos > 1.0f)
			{
				continue;  // the gap at the bottom
			}
			bool lit;
			if(bipolar)
			{
				const float lo = n < 0.5f ? n : 0.5f;
				const float hi = n < 0.5f ? 0.5f : n;
				lit = pos >= lo && pos <= hi;
			}
			else
			{
				lit = n > 0.0f && pos <= n;
			}
			display::put_pixel(cx + dx, cy + dy,
			                   lit ? theme::kAccent : theme::kTrack);
		}
	}
}

// Outlined button with a centred scale-2 label.
void draw_button(const Rect& r, const char* label)
{
	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);
	display::draw_rect(r.x, r.y, r.w, r.h, theme::kDim);
	draw_centered(r, r.y + (r.h - 16) / 2, label, theme::kText, 2);
}

}  // namespace

// --- Formatting -------------------------------------------------------------

void format_value(const params::Spec& spec, float value, char* out)
{
	switch(spec.format)
	{
		case params::Format::Signed:
			put_int(round_to_int(value), out, true);
			break;
		case params::Format::Unsigned:
			put_int(round_to_int(value), out, false);
			break;
		case params::Format::Scale127:
			put_int(round_to_int(value * 127.0f), out, false);
			break;
		case params::Format::Bipolar64:
		{
			int v = round_to_int(value * 64.0f);
			put_int(v > 63 ? 63 : v, out, true);
			break;
		}
		case params::Format::Enum:
		{
			const char* name = spec.names[round_to_int(value - spec.min)];
			while(*name != '\0')
			{
				*out++ = *name++;
			}
			*out = '\0';
			break;
		}
	}
}

char* format_int(int v, char* out, bool force_sign)
{
	return put_int(v, out, force_sign);
}

void format_note(std::uint8_t note, char* out)
{
	static constexpr const char* kNames[12] = {
	    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	for(const char* s = kNames[note % 12]; *s != '\0'; ++s)
	{
		*out++ = *s;
	}
	put_int(note / 12 - 1, out, false);
}

// --- ParamControl -----------------------------------------------------------

void ParamControl::sync()
{
	if(params_.get(id_) != shown_)
	{
		invalidate();
	}
}

void ParamControl::press(int x, int y, std::uint32_t)
{
	active_ = true;
	x0_ = x;
	y0_ = y;
	n0_ = params_.normalized(id_);
	invalidate();
}

void ParamControl::drag(int x, int y, std::uint32_t)
{
	const params::Spec& s = params::spec(id_);
	const float px_full =
	    s.step > 0.0f ? (s.max - s.min) / s.step * kPxPerStep : kPxFullRange;
	const float n =
	    n0_ + static_cast<float>(travel(x - x0_, y - y0_)) / px_full;
	params_.set_normalized(id_, n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n));
}

void ParamControl::release(std::uint32_t)
{
	active_ = false;
	invalidate();
}

// --- Knob -------------------------------------------------------------------

void Knob::draw()
{
	const Rect& r = rect_;
	const params::Spec& s = params::spec(id_);
	shown_ = params_.get(id_);

	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);
	draw_ring(r.x + r.w / 2, r.y + 2 + kKnobOuter, params_.normalized(id_),
	          s.min < 0.0f);

	char text[8];
	format_value(s, shown_, text);
	draw_centered(r, r.y + 27, text, theme::kText);
	draw_centered(r, r.y + 36, s.label, active_ ? theme::kAccent : theme::kDim);
}

// --- CrossFader -------------------------------------------------------------

void CrossFader::draw()
{
	const Rect& r = rect_;
	const params::Spec& s = params::spec(id_);
	shown_ = params_.get(id_);

	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);

	const int cx = r.x + r.w / 2;
	const int top = r.y + 8;
	const int bottom = r.y + r.h - 28;
	display::fill_rect(cx - 1, top, 3, bottom - top, theme::kTrack);
	display::draw_text(r.x + 2, top - 4, "1", theme::kDim);
	display::draw_text(r.x + 2, bottom - 4, "2", theme::kDim);

	const int ty = top + round_to_int(params_.normalized(id_) *
	                                  static_cast<float>(bottom - top));
	display::fill_rect(cx - 8, ty - 3, 17, 7,
	                   active_ ? theme::kText : theme::kAccent);

	char text[8];
	format_value(s, shown_, text);
	draw_centered(r, r.y + r.h - 19, text, theme::kText);
	draw_centered(r, r.y + r.h - 9, s.label,
	              active_ ? theme::kAccent : theme::kDim);
}

// --- FloatKnob --------------------------------------------------------------

float FloatKnob::normalized() const
{
	return (*value_ - min_) / (max_ - min_);
}

void FloatKnob::set_normalized(float n)
{
	const float v = min_ + n * (max_ - min_);
	*value_ = v < min_ ? min_ : (v > max_ ? max_ : v);
}

void FloatKnob::sync()
{
	if(*value_ != shown_)
	{
		invalidate();
	}
}

void FloatKnob::press(int x, int y, std::uint32_t)
{
	active_ = true;
	x0_ = x;
	y0_ = y;
	n0_ = normalized();
	invalidate();
}

void FloatKnob::drag(int x, int y, std::uint32_t)
{
	const float px_full =
	    step_ > 0.0f ? (max_ - min_) / step_ * kPxPerStep : kPxFullRange;
	const float n =
	    n0_ + static_cast<float>((x - x0_) - (y - y0_)) / px_full;
	set_normalized(n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n));
}

void FloatKnob::release(std::uint32_t)
{
	active_ = false;
	invalidate();
}

void FloatKnob::draw()
{
	const Rect& r = rect_;
	shown_ = *value_;

	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);
	draw_ring(r.x + r.w / 2, r.y + 2 + kKnobOuter, normalized(), min_ < 0.0f);

	char text[8];
	put_int(round_to_int(*value_ * static_cast<float>(text_scale_)), text,
	        min_ < 0.0f);
	draw_centered(r, r.y + 27, text, theme::kText);
	draw_centered(r, r.y + 36, label_, active_ ? theme::kAccent : theme::kDim);
}

// --- Slider -----------------------------------------------------------------

Rect Slider::track() const
{
	return Rect{rect_.x + 60, rect_.y + rect_.h / 2 - 2, rect_.w - 70, 4};
}

void Slider::set_from_x(int x)
{
	const Rect t = track();
	params_.set_normalized(
	    id_, static_cast<float>(x - t.x) / static_cast<float>(t.w));
}

void Slider::sync()
{
	if(params_.get(id_) != shown_)
	{
		invalidate();
	}
}

void Slider::press(int x, int, std::uint32_t)
{
	active_ = true;
	set_from_x(x);
	invalidate();
}

void Slider::drag(int x, int, std::uint32_t) { set_from_x(x); }

void Slider::release(std::uint32_t)
{
	active_ = false;
	invalidate();
}

void Slider::draw()
{
	const Rect& r = rect_;
	const params::Spec& s = params::spec(id_);
	shown_ = params_.get(id_);

	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);

	char text[8];
	format_value(s, shown_, text);
	display::draw_text(r.x + 6, r.y + 3, text, theme::kText, 2);
	display::draw_text(r.x + 6, r.y + r.h - 10, s.label,
	                   active_ ? theme::kAccent : theme::kDim);

	const Rect t = track();
	const int fill =
	    round_to_int(params_.normalized(id_) * static_cast<float>(t.w));
	display::fill_rect(t.x, t.y, t.w, t.h, theme::kTrack);
	display::fill_rect(t.x, t.y, fill, t.h, theme::kAccent);
	display::fill_rect(t.x + fill - 3, r.y + 6, 7, r.h - 12,
	                   active_ ? theme::kAccent : theme::kText);
}

// --- StepGrid ---------------------------------------------------------------

Rect StepGrid::cell(int step) const
{
	const int col = step % 4;
	const int row = step / 4;
	const int pitch_x = rect_.w / 4;
	const int pitch_y = rect_.h / 4;
	return Rect{rect_.x + col * pitch_x + 2, rect_.y + row * pitch_y + 2,
	            pitch_x - 4, pitch_y - 4};
}

int StepGrid::step_at(int x, int y) const
{
	for(int i = 0; i < sequencer::kSteps; ++i)
	{
		if(cell(i).contains(x, y))
		{
			return i;
		}
	}
	return -1;
}

void StepGrid::mark(int step)
{
	if(step >= 0 && step < sequencer::kSteps)
	{
		dirty_cells_ |= static_cast<std::uint16_t>(1u << step);
		dirty_ = true;
	}
}

void StepGrid::invalidate()
{
	dirty_cells_ = 0xFFFF;
	dirty_ = true;
}

void StepGrid::select(int step)
{
	if(step != selected_)
	{
		mark(selected_);
		selected_ = step;
		mark(selected_);
	}
}

bool StepGrid::take_picker_request()
{
	const bool r = picker_request_;
	picker_request_ = false;
	return r;
}

void StepGrid::sync()
{
	const int playing = transport_.step;
	if(playing != playhead_)
	{
		mark(playhead_);
		playhead_ = playing;
		mark(playhead_);
	}
	for(int i = 0; i < sequencer::kSteps; ++i)
	{
		const sequencer::Step& s = pattern_.steps[i];
		if(s.on != shown_on_[i] || (s.on && s.note != shown_note_[i]))
		{
			mark(i);
		}
	}
}

void StepGrid::press(int x, int y, std::uint32_t now_ms)
{
	press_step_ = step_at(x, y);
	over_step_ = press_step_;
	press_ms_ = now_ms;
	long_fired_ = false;
}

void StepGrid::drag(int x, int y, std::uint32_t) { over_step_ = step_at(x, y); }

void StepGrid::tick(std::uint32_t now_ms)
{
	// A finger held still on one step long enough: select it for note entry.
	if(press_step_ >= 0 && !long_fired_ && over_step_ == press_step_ &&
	   now_ms - press_ms_ >= kLongPressMs)
	{
		long_fired_ = true;
		select(press_step_);
		picker_request_ = true;
	}
}

void StepGrid::release(std::uint32_t)
{
	// A tap is a press and release on the same step that didn't turn into a
	// long press.
	if(press_step_ >= 0 && !long_fired_ && over_step_ == press_step_)
	{
		sequencer::Step& s = pattern_.steps[press_step_];
		s.on = !s.on;
		select(press_step_);
		mark(press_step_);
	}
	press_step_ = -1;
	over_step_ = -1;
}

void StepGrid::draw()
{
	// The whole rect, not just the cells: the note picker draws over this
	// rectangle and its keys reach into the gaps between the cells. Redrawing
	// the cells only would leave the piano showing through those gaps. The
	// fill wipes every cell, so all of them are redrawn, not just the dirty
	// ones; the mask only decides whether to draw at all.
	display::fill_rect(rect_.x, rect_.y, rect_.w, rect_.h, theme::kBackground);
	for(int i = 0; i < sequencer::kSteps; ++i)
	{
		draw_cell(i);
	}
	dirty_cells_ = 0;
}

void StepGrid::draw_cell(int step)
{
	const Rect c = cell(step);
	const sequencer::Step& s = pattern_.steps[step];
	shown_on_[step] = s.on;
	shown_note_[step] = s.note;

	display::fill_rect(c.x, c.y, c.w, c.h,
	                   step == playhead_ ? theme::kPlayhead : theme::kPanel);
	if(step == selected_)
	{
		display::draw_rect(c.x, c.y, c.w, c.h, theme::kText);
		display::draw_rect(c.x + 1, c.y + 1, c.w - 2, c.h - 2, theme::kText);
	}

	char text[6];
	put_int(step + 1, text, false);
	display::draw_text(c.x + 5, c.y + 5, text, theme::kDim);

	if(s.on)
	{
		draw_centered(c, c.y + (c.h - 24) / 2, "X", theme::kAccent, 3);
		format_note(s.note, text);
		display::draw_text(c.x + c.w - display::text_width(text) - 5,
		                   c.y + c.h - 13, text, theme::kText);
	}
}

// --- PlayButton -------------------------------------------------------------

void PlayButton::sync()
{
	if(transport_.run != shown_run_)
	{
		invalidate();
	}
}

void PlayButton::press(int, int, std::uint32_t)
{
	transport_.run = !transport_.run;
}

void PlayButton::draw()
{
	const Rect& r = rect_;
	shown_run_ = transport_.run;
	const int cx = r.x + r.w / 2;
	const int cy = r.y + r.h / 2;

	if(shown_run_)
	{
		// Playing: lit button with a stop square.
		display::fill_rect(r.x, r.y, r.w, r.h, theme::kAccent);
		display::fill_rect(cx - 6, cy - 6, 12, 12, theme::kBackground);
		return;
	}
	// Stopped: a right-pointing play triangle, 15 px tall.
	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);
	for(int row = 0; row < 15; ++row)
	{
		const int from_tip = row < 7 ? row : 14 - row;
		display::fill_rect(cx - 5, cy - 7 + row, from_tip * 2 + 1, 1,
		                   theme::kText);
	}
}

// --- NoteBox ----------------------------------------------------------------

bool NoteBox::take_request()
{
	const bool r = request_;
	request_ = false;
	return r;
}

void NoteBox::sync()
{
	const int step = grid_.selected();
	if(step != shown_step_ ||
	   (step >= 0 && pattern_.steps[step].note != shown_note_))
	{
		invalidate();
	}
}

void NoteBox::press(int, int, std::uint32_t)
{
	request_ = grid_.selected() >= 0;
}

void NoteBox::draw()
{
	const Rect& r = rect_;
	const int step = grid_.selected();
	shown_step_ = step;

	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);
	display::draw_text(r.x + 6, r.y + 5, "STEP", theme::kDim);

	char text[8];
	if(step < 0)
	{
		display::draw_text(r.x + 6, r.y + 19, "--", theme::kDim);
		display::draw_text(r.x + 54, r.y + 9, "---", theme::kDim, 2);
		return;
	}
	shown_note_ = pattern_.steps[step].note;
	put_int(step + 1, text, false);
	display::draw_text(r.x + 6, r.y + 19, text, theme::kText);
	format_note(shown_note_, text);
	display::draw_text(r.x + 54, r.y + 9, text, theme::kAccent, 2);
	// A small down-pointing marker: tap here for the note picker.
	for(int i = 0; i < 4; ++i)
	{
		display::fill_rect(r.x + r.w - 17 + i, r.y + 14 + i, 9 - 2 * i, 1,
		                   theme::kDim);
	}
}

// --- NotePicker -------------------------------------------------------------

namespace
{
constexpr int kWhiteSemis[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kBlackSemis[5] = {1, 3, 6, 8, 10};
constexpr int kBlackAfterWhite[5] = {0, 1, 3, 4, 5};  // C# sits after C, ...
constexpr const char* kWhiteNames[7] = {"C", "D", "E", "F", "G", "A", "B"};

constexpr int kHeaderH = 36;
constexpr int kKeyPitch = 44;
constexpr int kWhiteW = 42;
constexpr int kBlackW = 28;
}  // namespace

void NotePicker::open(int step)
{
	step_ = step;
	const int octave = pattern_.steps[step].note / 12 - 1;
	octave_ = octave < kMinOctave ? kMinOctave
	                              : (octave > kMaxOctave ? kMaxOctave : octave);
	visible_ = true;
	invalidate();
}

Rect NotePicker::minus_button() const
{
	return Rect{rect_.x + 112, rect_.y + 4, 36, 28};
}

Rect NotePicker::plus_button() const
{
	return Rect{rect_.x + 208, rect_.y + 4, 36, 28};
}

Rect NotePicker::close_button() const
{
	return Rect{rect_.x + rect_.w - 40, rect_.y + 4, 36, 28};
}

Rect NotePicker::white_key(int i) const
{
	const int x0 = rect_.x + (rect_.w - (7 * kKeyPitch - 2)) / 2;
	return Rect{x0 + i * kKeyPitch, rect_.y + kHeaderH + 2, kWhiteW,
	            rect_.h - kHeaderH - 6};
}

Rect NotePicker::black_key(int i) const
{
	const Rect w = white_key(kBlackAfterWhite[i]);
	const int gap_centre = w.x + kWhiteW + 1;
	return Rect{gap_centre - kBlackW / 2, w.y, kBlackW, w.h * 3 / 5};
}

int NotePicker::key_at(int x, int y) const
{
	// Black keys sit on top of the white ones, so test them first.
	for(int i = 0; i < 5; ++i)
	{
		if(black_key(i).contains(x, y))
		{
			return kBlackSemis[i];
		}
	}
	for(int i = 0; i < 7; ++i)
	{
		if(white_key(i).contains(x, y))
		{
			return kWhiteSemis[i];
		}
	}
	return -1;
}

void NotePicker::press(int x, int y, std::uint32_t)
{
	if(minus_button().contains(x, y))
	{
		if(octave_ > kMinOctave)
		{
			--octave_;
			invalidate();
		}
		return;
	}
	if(plus_button().contains(x, y))
	{
		if(octave_ < kMaxOctave)
		{
			++octave_;
			invalidate();
		}
		return;
	}
	if(close_button().contains(x, y))
	{
		close();
		return;
	}
	const int semi = key_at(x, y);
	if(semi >= 0)
	{
		sequencer::Step& s = pattern_.steps[step_];
		s.note = static_cast<std::uint8_t>((octave_ + 1) * 12 + semi);
		s.on = true;
		close();
	}
}

void NotePicker::draw()
{
	const Rect& r = rect_;
	display::fill_rect(r.x, r.y, r.w, r.h, theme::kBackground);

	char text[12] = "STEP ";
	put_int(step_ + 1, text + 5, false);
	display::draw_text(r.x + 6, r.y + 14, text, theme::kText);

	draw_button(minus_button(), "-");
	draw_button(plus_button(), "+");
	draw_button(close_button(), "X");
	const Rect m = minus_button();
	const Rect p = plus_button();
	char oct[8] = "OCT ";
	put_int(octave_, oct + 4, false);
	draw_centered(Rect{m.x + m.w, m.y, p.x - m.x - m.w, m.h}, m.y + 10, oct,
	              theme::kText);

	// Highlight the step's current note if it is in the shown octave.
	const int note = pattern_.steps[step_].note;
	const int current = note / 12 - 1 == octave_ ? note % 12 : -1;

	for(int i = 0; i < 7; ++i)
	{
		const Rect k = white_key(i);
		display::fill_rect(
		    k.x, k.y, k.w, k.h,
		    kWhiteSemis[i] == current ? theme::kAccent : theme::kWhiteKey);
		draw_centered(k, k.y + k.h - 14, kWhiteNames[i], theme::kBlackKey);
	}
	for(int i = 0; i < 5; ++i)
	{
		const Rect k = black_key(i);
		display::fill_rect(k.x, k.y, k.w, k.h, theme::kBackground);  // outline
		display::fill_rect(
		    k.x + 1, k.y, k.w - 2, k.h - 1,
		    kBlackSemis[i] == current ? theme::kAccent : theme::kBlackKey);
	}
}

// --- TabBar -------------------------------------------------------------------

Rect TabBar::tab(int i) const
{
	return Rect{rect_.x + i * kTabW, rect_.y, kTabW, rect_.h};
}

int TabBar::tab_at(int x, int y) const
{
	if(!rect_.contains(x, y) || x >= rect_.x + num_tabs_ * kTabW)
	{
		return -1;
	}
	const int i = (x - rect_.x) / kTabW;
	return (i >= 0 && i < num_tabs_) ? i : -1;
}

void TabBar::sync()
{
	if(active_ != shown_active_ || status_ != shown_status_ ||
	   transport_.run != shown_run_ || transport_.step != shown_step_)
	{
		invalidate();
	}
}

void TabBar::press(int x, int y, std::uint32_t)
{
	const int i = tab_at(x, y);
	if(i >= 0)
	{
		active_ = i;
		switch_ = i;
		invalidate();
	}
}

int TabBar::take_switch()
{
	const int i = switch_;
	switch_ = -1;
	return i;
}

void TabBar::draw()
{
	const Rect& r = rect_;
	display::fill_rect(r.x, r.y, r.w, r.h, theme::kPanel);
	display::fill_rect(r.x, r.y, r.w, 1, theme::kTrack);

	for(int i = 0; i < num_tabs_; ++i)
	{
		const Rect t = tab(i);
		if(i == active_)
		{
			display::fill_rect(t.x, t.y, t.w, t.h - 1, theme::kAccent);
		}
		else
		{
			display::fill_rect(t.x + t.w - 1, t.y + 1, 1, t.h - 2,
			                   theme::kTrack);
		}
		draw_centered(t, t.y + (t.h - 8) / 2, tab_names_[i],
		              i == active_ ? display::colors::Black : theme::kDim);
	}

	if(status_ != nullptr)
	{
		const int dot_x = r.x + r.w - kDot - 4;
		int x = dot_x - 6 - display::text_width(status_);
		if(x < r.x + num_tabs_ * kTabW + 4)
		{
			x = r.x + num_tabs_ * kTabW + 4;
		}
		display::draw_text(x, r.y + (r.h - 8) / 2, status_, theme::kText);
	}

	// Transport dot: blinks with the step while running, dim when stopped.
	const int dot_x = r.x + r.w - kDot - 4;
	const int dot_y = r.y + (r.h - kDot) / 2;
	display::Color c = theme::kTrack;
	if(transport_.run)
	{
		c = ((transport_.step & 1) == 0) ? theme::kAccent : theme::kHeader;
	}
	display::fill_rect(dot_x, dot_y, kDot, kDot, c);

	shown_active_ = active_;
	shown_status_ = status_;
	shown_run_ = transport_.run;
	shown_step_ = transport_.step;
}

// --- CycleButton --------------------------------------------------------------

void CycleButton::sync()
{
	if(*value_ != shown_)
	{
		invalidate();
	}
}

void CycleButton::press(int, int, std::uint32_t)
{
	*value_ = (*value_ + 1) % count_;
	pressed_ = true;
	invalidate();
}

void CycleButton::release(std::uint32_t)
{
	pressed_ = false;
	invalidate();
}

void CycleButton::draw()
{
	const Rect& r = rect_;
	const int v = (shown_ = *value_);
	const char* name = (v >= 0 && v < count_) ? names_[v] : "???";
	display::fill_rect(r.x, r.y, r.w, r.h,
	                   pressed_ ? theme::kHeader : theme::kPanel);
	display::draw_rect(r.x, r.y, r.w, r.h,
	                   pressed_ ? theme::kAccent : theme::kDim);
	draw_centered(r, r.y + (r.h - 8) / 2, name,
	              pressed_ ? theme::kAccent : theme::kText);
}

}  // namespace ui
