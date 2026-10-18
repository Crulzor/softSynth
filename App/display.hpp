// SPDX-License-Identifier: MIT
//
// Minimal LTDC display bring-up for the STM32H750B-DK on-board 4.3" panel
// (Rocktech RK043FN48H, 480x272 RGB).
//
// We drive a single LTDC layer backed by an RGB565 framebuffer that lives in
// internal AXI-SRAM (see the .framebuffer section in the linker script). All
// drawing is plain CPU writes into that buffer; the LTDC scans it out to the
// panel. No TouchGFX / designer tool and no external SDRAM are involved.
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

// Bring up the panel GPIOs + LTDC + a single RGB565 layer, and clear the screen
// to `background`. After this call the framebuffer is live and drawable.
void init(Color background = colors::Blue);

// Fill the whole framebuffer with a solid colour.
void fill(Color c);

// Set a single pixel. Out-of-bounds coordinates are ignored.
void put_pixel(int x, int y, Color c);

// Fill an axis-aligned rectangle (clipped to the screen).
void fill_rect(int x, int y, int w, int h, Color c);

// 1-px outline of an axis-aligned rectangle (clipped to the screen).
void draw_rect(int x, int y, int w, int h, Color c);

// Draw a NUL-terminated string with its top-left corner at (x, y).
// Each glyph is 8x8 px multiplied by `scale` (scale=1 -> 8x8, scale=3 ->
// 24x24). Only the glyph (foreground) pixels are written; the background shows
// through. Returns the x coordinate just past the last glyph (for chaining).
int draw_text(int x, int y, const char* s, Color fg, int scale = 1);

// Width in pixels that draw_text() would cover for `s` at `scale`.
int text_width(const char* s, int scale = 1);

namespace detail
{
// The RGB565 framebuffer, kWidth * kHeight pixels, row-major. Defined by
// display.cpp on the target (in the .framebuffer section) and by the host
// build's stand-in. Only the drawing code and host tooling should touch it.
extern std::uint16_t g_framebuffer[];
}  // namespace detail

}  // namespace display
