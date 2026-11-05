// SPDX-License-Identifier: MIT
#include "ui/debug_page.hpp"

#include "display.hpp"
#include "ui/theme.hpp"

namespace ui
{
namespace
{

using params::Id;

// --- Layout (480 x kPageH) ---------------------------------------------------
// Left third: parameter cells in rows, a 3-letter row tag down the left edge.
constexpr int kLeftW = 159;  // x = 159 is the separator line
constexpr int kTagW = 11;    // row tag column
constexpr int kCellW = 36;
constexpr int kCellPitch = 37;
constexpr int kHeaderH = 11;

constexpr int kOscHeaderY = 0;
constexpr int kOscRowY[2] = {12, 56};
constexpr int kOscCellH = 44;

constexpr int kModHeaderY = 100;
constexpr int kModRowY[3] = {112, 156, 200};
constexpr int kModCellH = 44;

constexpr Rect cell(int col, int y, int h)
{
	return Rect{kTagW + col * kCellPitch, y, kCellW, h};
}

constexpr Rect osc(int col, int row)
{
	return cell(col, kOscRowY[row], kOscCellH);
}
constexpr Rect mod(int col, int row)
{
	return cell(col, kModRowY[row], kModCellH);
}

// Right two thirds: top bar (note box, play/stop, tempo), then the 4x4 grid.
constexpr int kRightX = 160;
constexpr Rect kNoteBox{kRightX + 2, 1, 124, 32};
constexpr Rect kPlay{kRightX + 128, 1, 36, 32};
constexpr Rect kTempo{kRightX + 166, 1, 152, 32};
constexpr Rect kGrid{kRightX, 35, 320, 209};

// Row tags, drawn as stacked letters.
struct RowTag
{
	const char* text;
	int y;
	int h;
};
constexpr RowTag kRowTags[] = {
    {"OS1", kOscRowY[0], kOscCellH}, {"OS2", kOscRowY[1], kOscCellH},
    {"ENV", kModRowY[0], kModCellH}, {"LF1", kModRowY[1], kModCellH},
    {"LF2", kModRowY[2], kModCellH},
};

// Above this peak load the CPU read-out turns red.
constexpr int kLoadWarnTenths = 800;

void draw_header(int y, const char* title)
{
	display::fill_rect(0, y, kLeftW, kHeaderH, theme::kHeader);
	display::draw_text(3, y + 2, title, theme::kText);
}

// Text right-aligned in a header bar.
void draw_header_note(int y, const char* text, display::Color c)
{
	const int w = display::text_width(text);
	display::draw_text(kLeftW - w - 3, y + 2, text, c);
}

int to_tenths_percent(float fraction)
{
	return static_cast<int>(fraction * 1000.0f + 0.5f);
}

// Append `s` at `out`; returns the pointer to the new terminating NUL.
char* put_str(const char* s, char* out)
{
	while(*s != '\0')
	{
		*out++ = *s++;
	}
	*out = '\0';
	return out;
}

// "12.3" from 123.
char* put_tenths(int tenths, char* out)
{
	out = format_int(tenths / 10, out);
	*out++ = '.';
	return format_int(tenths % 10, out);
}

void draw_row_tag(const RowTag& tag)
{
	const int top = tag.y + (tag.h - 26) / 2;
	char ch[2] = {0, 0};
	for(int i = 0; tag.text[i] != '\0'; ++i)
	{
		ch[0] = tag.text[i];
		display::draw_text(2, top + i * 9, ch, theme::kDim);
	}
}

}  // namespace

DebugPage::DebugPage(Ctx ctx)
    : knobs_{
          Knob(osc(0, 0), ctx.params, Id::Osc1Pitch),
          Knob(osc(1, 0), ctx.params, Id::Osc1Shape),
          Knob(osc(2, 0), ctx.params, Id::Osc1Octave),
          Knob(osc(0, 1), ctx.params, Id::Osc2Pitch),
          Knob(osc(1, 1), ctx.params, Id::Osc2Shape),
          Knob(osc(2, 1), ctx.params, Id::Osc2Octave),
          Knob(mod(0, 0), ctx.params, Id::EnvAttack),
          Knob(mod(1, 0), ctx.params, Id::EnvDecay),
          Knob(mod(2, 0), ctx.params, Id::EnvSustain),
          Knob(mod(3, 0), ctx.params, Id::EnvRelease),
          Knob(mod(0, 1), ctx.params, Id::Lfo1Rate),
          Knob(mod(0, 2), ctx.params, Id::Lfo2Rate),
      },
      // The mix column spans both oscillator rows.
      mix_(Rect{kTagW + 3 * kCellPitch, kOscRowY[0], kCellW,
	        kOscRowY[1] + kOscCellH - kOscRowY[0]},
	       ctx.params, Id::OscMix),
      tempo_(kTempo, ctx.params, Id::Tempo),
      play_(kPlay, ctx.transport),
      grid_(kGrid, ctx.pattern, ctx.transport),
      note_box_(kNoteBox, grid_, ctx.pattern),
      picker_(kGrid, ctx.pattern)
{
	int n = 0;
	for(Knob& k : knobs_)
	{
		widgets_[n++] = &k;
	}
	widgets_[n++] = &mix_;
	widgets_[n++] = &tempo_;
	widgets_[n++] = &play_;
	widgets_[n++] = &grid_;
	widgets_[n++] = &note_box_;
}

void DebugPage::redraw_all()
{
	display::fill(theme::kBackground);
	frame_dirty_ = true;
	for(Widget* w : widgets_)
	{
		w->invalidate();
	}
	picker_.invalidate();
}

void DebugPage::set_cpu_mhz(int mhz)
{
	cpu_mhz_ = mhz;
	frame_dirty_ = true;
}

void DebugPage::set_cpu_load(float average, float peak)
{
	const int avg = to_tenths_percent(average);
	const int pk = to_tenths_percent(peak);
	if(avg != load_avg_tenths_ || pk != load_peak_tenths_)
	{
		load_avg_tenths_ = avg;
		load_peak_tenths_ = pk;
		frame_dirty_ = true;
	}
}

void DebugPage::draw_frame()
{
	draw_header(kOscHeaderY, "OSC");
	draw_header(kModHeaderY, "MOD");
	char text[24];
	if(cpu_mhz_ > 0)
	{
		put_str("MHz", format_int(cpu_mhz_, text));
		draw_header_note(kOscHeaderY, text, theme::kDim);
	}
	if(load_avg_tenths_ >= 0)
	{
		char* p = put_str("CPU ", text);
		p = put_str("/", put_tenths(load_avg_tenths_, p));
		put_str("%", put_tenths(load_peak_tenths_, p));
		draw_header_note(
		    kModHeaderY, text,
		    load_peak_tenths_ >= kLoadWarnTenths ? theme::kWarn : theme::kText);
	}
	for(const RowTag& tag : kRowTags)
	{
		draw_row_tag(tag);
	}
	display::fill_rect(kLeftW, 0, 1, kPageH, theme::kHeader);
}

Widget* DebugPage::hit(int x, int y)
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

void DebugPage::press(int x, int y, std::uint32_t now_ms)
{
	if(picker_.visible())
	{
		// The picker is modal: a touch outside it just dismisses it.
		if(picker_.rect().contains(x, y))
		{
			picker_.press(x, y, now_ms);
		}
		else
		{
			picker_.close();
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

void DebugPage::drag(int x, int y, std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->drag(x, y, now_ms);
	}
}

void DebugPage::release(std::uint32_t now_ms)
{
	if(captured_ != nullptr)
	{
		captured_->release(now_ms);
		captured_ = nullptr;
	}
}

void DebugPage::update(std::uint32_t now_ms)
{
	for(Widget* w : widgets_)
	{
		w->tick(now_ms);
	}
	// Either a long press on a step or a tap on the note box opens the picker
	// for the selected step.
	const bool want_picker = grid_.take_picker_request();
	const bool box_tapped = note_box_.take_request();
	if((want_picker || box_tapped) && grid_.selected() >= 0)
	{
		picker_.open(grid_.selected());
	}

	if(frame_dirty_)
	{
		frame_dirty_ = false;
		draw_frame();
	}

	for(Widget* w : widgets_)
	{
		w->sync();
	}

	if(picker_.visible())
	{
		// The picker covers the grid: draw everything else, then the picker.
		for(Widget* w : widgets_)
		{
			if(w != &grid_)
			{
				w->draw_if_dirty();
			}
		}
		picker_.draw_if_dirty();
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
