// SPDX-License-Identifier: MIT
//
// Minimal LTDC display bring-up for the STM32H750B-DK on-board 4.3" panel
// (Rocktech RK043FN48H, 480x272 RGB).
//
// We drive a single LTDC layer backed by a double-buffered RGB565 pair in
// internal AXI-SRAM (see the .framebuffer section in the linker script).
// The CPU draws into the back buffer; at the next frame start the LTDC
// line-event interrupt swaps the two -- but only when something was actually
// drawn (the drawing calls set a dirty flag). The panel otherwise keeps
// scanning the last complete frame, so a retained-mode UI that redraws only
// invalidated widgets never alternates between a fresh and a stale buffer
// (no tearing, no ghosting). No TouchGFX / designer tool, no SDRAM.
//
// The module is split in two so the drawing code can run on a desktop:
//   display.cpp      - HAL side: panel GPIOs, LTDC, owns the framebuffer.
//   display_draw.cpp - pure drawing (fill, rects, text) into the framebuffer.
// The host build (test/) links display_draw.cpp against its own framebuffer
// array and dumps it to an image, so UI layouts can be checked without a board.
//
// Note: this assumes the CPU data cache is OFF (it is, in this project), so the
// LTDC sees framebuffer writes immediately with no cache maintenance.
#pragma once

#include <cstdint>

namespace display
{

struct Color
{
	std::uint8_t r;
	std::uint8_t g;
	std::uint8_t b;
};

namespace colors
{
inline constexpr Color Black{0, 0, 0};
inline constexpr Color Red{255, 0, 0};
inline constexpr Color Green{0, 255, 0};
inline constexpr Color Blue{0, 0, 255};
inline constexpr Color White{255, 255, 255};
}  // namespace colors

// Panel geometry (RK043FN48H).
inline constexpr std::uint16_t kWidth = 480;
inline constexpr std::uint16_t kHeight = 272;

// Bring up the panel GPIOs + LTDC + a single RGB565 layer, and clear both
// buffers to `background`. After this call the framebuffer is live and
// drawable. The line-event swap is not armed yet: draw one complete frame
// into each buffer (swap_buffers() in between), then enable_swap_irq().
void init(Color background = colors::Blue);

// Ping-pong control. The CPU draws into the back buffer; the LTDC scans out
// the front. swap_buffers() flips the two now, and the layer follows the new
// front. enable_swap_irq() arms the line event that performs the flip at a
// frame start, but only once the foreground has drawn a new frame (any
// drawing call sets the dirty flag; the flip consumes it).
void swap_buffers();
void enable_swap_irq();

// Call around each foreground draw pass. begin_draw() marks the pass in
// progress so a frame boundary inside it defers the flip instead of promoting
// a half-drawn frame (at most one boundary is missed); end_draw() clears the
// mark. The line interrupt stays armed throughout — masking it raced the LTDC
// and could livelock the IRQ (docs/ltdc-lie-race.md).
void begin_draw();
void end_draw();

// Fill the whole framebuffer with a solid colour.
void fill(Color c);

// Set a single pixel. Out-of-bounds coordinates are ignored.
void put_pixel(int x, int y, Color c);

// Fill an axis-aligned rectangle (clipped to the screen).
void fill_rect(int x, int y, int w, int h, Color c);

// 1-px outline of an axis-aligned rectangle (clipped to the screen).
void draw_rect(int x, int y, int w, int h, Color c);

// 1-px straight line (Bresenham), both endpoints included, clipped to the
// screen.
void draw_line(int x0, int y0, int x1, int y1, Color c);

// Fill the interior of a simple polygon given as vertex pairs (x0, y0, x1,
// y1, ...). Scanline fill from the leftmost to the rightmost edge crossing per
// row: correct for the simple, x-monotone shapes this module draws (curves,
// triangles), not a general even-odd fill.
void fill_polygon(const int* xy, int n, Color c);

// Draw a NUL-terminated string with its top-left corner at (x, y).
// Each glyph is 8x8 px multiplied by `scale` (scale=1 -> 8x8, scale=3 ->
// 24x24). Only the glyph (foreground) pixels are written; the background shows
// through. Returns the x coordinate just past the last glyph (for chaining).
int draw_text(int x, int y, const char* s, Color fg, int scale = 1);

// Width in pixels that draw_text() would cover for `s` at `scale`.
int text_width(const char* s, int scale = 1);

namespace detail
{
// The back buffer: kWidth * kHeight RGB565 pixels, row-major, what all the
// drawing calls write into. On the target display.cpp owns the buffer pair
// (.framebuffer section) and this points at whichever buffer is *not* being
// scanned out; the host build's stand-in points at a plain array. Only the
// drawing code and host tooling should touch it.
extern std::uint16_t* g_framebuffer;
// The ping-pong pair: g_back is the drawing target (g_framebuffer), g_front
// is what the LTDC scans out; swap_buffers() exchanges them. Defined in
// display.cpp (target) and the host stand-in.
extern std::uint16_t* g_back;
extern std::uint16_t* g_front;

// "A new frame has been drawn" -- set by the drawing calls (any pixel write),
// consumed (cleared) by the frame-swap interrupt when it flips. Word-sized so
// the flag is an atomic word access from both contexts, like the rest of the
// foreground/ISR handoff. Defined in display.cpp (target) and in the host
// stand-in (test/host_framebuffer.cpp).
extern volatile std::uint32_t g_dirty;
// Set around each begin_draw()/end_draw() pair so the swap interrupt can tell
// a completed frame from one still being drawn; the flip is deferred to the
// next boundary. Target only (display.cpp).
extern volatile std::uint32_t g_in_draw;

// Bounding box, in pixels, of everything the drawing calls have written into
// the back buffer since the last promotion (swap_buffers): x0/y0 inclusive,
// x1/y1 inclusive, screen-clamped; g_box_any false when nothing was written.
// Grown by the drawing calls (display_draw.cpp), consumed by flush_box().
// Not per-buffer: the CPU only ever draws into g_back.
extern int g_box_x0;
extern int g_box_y0;
extern int g_box_x1;
extern int g_box_y1;
extern bool g_box_any;

// Pull the drawn box out of the buffer that was just promoted to front and
// into the buffer just demoted to back, then clear the box. This is what
// keeps a retained-mode UI from ghosting: a one-shot widget change (a cycled
// label) is drawn into one buffer only, and the panel keeps alternating the
// two buffers while anything else (the tab bar) keeps flipping them; pulling
// the box into the demoted buffer makes both buffers agree on it. Called by
// swap_buffers() after the exchange; a no-op when nothing was drawn.
// Defined in display_draw.cpp so both builds get it.
void flush_box();
}  // namespace detail

}  // namespace display
