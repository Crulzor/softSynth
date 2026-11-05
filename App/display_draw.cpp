// SPDX-License-Identifier: MIT
//
// Pure drawing into the RGB565 framebuffer. No HAL: this file also compiles in
// the host build (test/), where it draws into a stand-in framebuffer that gets
// dumped to an image.
#include <cstdlib>
#include <cstring>

#include "display.hpp"
#include "font8x8.hpp"

namespace display
{
namespace
{

using detail::g_framebuffer;

// Pack an 8-8-8 colour into the panel's 5-6-5 layout.
constexpr std::uint16_t rgb565(Color c)
{
	return static_cast<std::uint16_t>(((c.r & 0xF8) << 8) |
	                                  ((c.g & 0xFC) << 3) | (c.b >> 3));
}

// Grow the drawn box (detail::g_box_*) to include one pixel.
void mark_pixel(int x, int y)
{
	if(!detail::g_box_any)
	{
		detail::g_box_x0 = detail::g_box_x1 = x;
		detail::g_box_y0 = detail::g_box_y1 = y;
		detail::g_box_any = true;
		return;
	}
	if(x < detail::g_box_x0) detail::g_box_x0 = x;
	if(x > detail::g_box_x1) detail::g_box_x1 = x;
	if(y < detail::g_box_y0) detail::g_box_y0 = y;
	if(y > detail::g_box_y1) detail::g_box_y1 = y;
}

// Grow the drawn box to include the rect [x0, x1) x [y0, y1), clamped to the
// screen. Callers pass the clipped write region, so the box never leaves the
// framebuffer.
void mark_rect(int x0, int y0, int x1, int y1)
{
	if(x0 < 0) x0 = 0;
	if(y0 < 0) y0 = 0;
	if(x1 > kWidth) x1 = kWidth;
	if(y1 > kHeight) y1 = kHeight;
	if(x1 <= x0 || y1 <= y0)
	{
		return;
	}
	mark_pixel(x0, y0);
	mark_pixel(x1 - 1, y1 - 1);
}

}  // namespace

void fill(Color c)
{
	// Any pixel write marks the back buffer as a new frame for the swap
	// interrupt (host build: the stand-in flag, harmless).
	detail::g_dirty = 1;
	const std::uint16_t px = rgb565(c);
	for(std::uint32_t i = 0; i < kWidth * kHeight; ++i)
	{
		g_framebuffer[i] = px;
	}
	mark_rect(0, 0, kWidth, kHeight);
}

void put_pixel(int x, int y, Color c)
{
	if(x < 0 || x >= kWidth || y < 0 || y >= kHeight)
	{
		return;
	}
	detail::g_dirty = 1;
	g_framebuffer[static_cast<std::uint32_t>(y) * kWidth + x] = rgb565(c);
	mark_pixel(x, y);
}

void fill_rect(int x, int y, int w, int h, Color c)
{
	// Clip to the screen so callers don't have to.
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + w > kWidth ? kWidth : x + w;
	int y1 = y + h > kHeight ? kHeight : y + h;
	detail::g_dirty = 1;
	const std::uint16_t px = rgb565(c);
	for(int yy = y0; yy < y1; ++yy)
	{
		std::uint16_t* row =
		    &g_framebuffer[static_cast<std::uint32_t>(yy) * kWidth];
		for(int xx = x0; xx < x1; ++xx)
		{
			row[xx] = px;
		}
	}
	mark_rect(x0, y0, x1, y1);
}

void draw_rect(int x, int y, int w, int h, Color c)
{
	if(w <= 0 || h <= 0)
	{
		return;
	}
	fill_rect(x, y, w, 1, c);
	fill_rect(x, y + h - 1, w, 1, c);
	fill_rect(x, y, 1, h, c);
	fill_rect(x + w - 1, y, 1, h, c);
}

void draw_line(int x0, int y0, int x1, int y1, Color c)
{
	detail::g_dirty = 1;
	// Track the pixels actually written so the drawn box stays tight.
	int bx0 = x0, bx1 = x0, by0 = y0, by1 = y0;
	bool any = false;
	const std::uint16_t px = rgb565(c);
	int dx = std::abs(x1 - x0);
	const int dy = -std::abs(y1 - y0);
	const int sx = x0 < x1 ? 1 : -1;
	const int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for(;;)
	{
		if(x0 >= 0 && x0 < kWidth && y0 >= 0 && y0 < kHeight)
		{
			g_framebuffer[static_cast<std::uint32_t>(y0) * kWidth + x0] = px;
			if(x0 < bx0) bx0 = x0;
			if(x0 > bx1) bx1 = x0;
			if(y0 < by0) by0 = y0;
			if(y0 > by1) by1 = y0;
			any = true;
		}
		if(x0 == x1 && y0 == y1)
		{
			break;
		}
		const int e2 = 2 * err;
		if(e2 >= dy)
		{
			err += dy;
			x0 += sx;
		}
		if(e2 <= dx)
		{
			err += dx;
			y0 += sy;
		}
	}
	if(any)
	{
		mark_rect(bx0, by0, bx1 + 1, by1 + 1);
	}
}

void fill_polygon(const int* xy, int n, Color c)
{
	if(n < 3)
	{
		return;
	}
	int ymin = xy[1];
	int ymax = xy[1];
	for(int i = 1; i < n; ++i)
	{
		const int y = xy[i * 2 + 1];
		if(y < ymin)
		{
			ymin = y;
		}
		if(y > ymax)
		{
			ymax = y;
		}
	}
	for(int y = ymin; y <= ymax; ++y)
	{
		int xs[64];
		int count = 0;
		for(int i = 0; i < n; ++i)
		{
			const int x0 = xy[i * 2];
			const int y0 = xy[i * 2 + 1];
			const int x1 = xy[(i + 1) % n * 2];
			const int y1 = xy[(i + 1) % n * 2 + 1];
			// Count each horizontal-boundary vertex on exactly one edge.
			const bool crosses =
			    (y0 <= y && y1 > y) || (y1 <= y && y0 > y);
			if(crosses && count < 64)
			{
				xs[count++] = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
			}
		}
		if(count == 0)
		{
			continue;
		}
		int xmin = xs[0];
		int xmax = xs[0];
		for(int i = 1; i < count; ++i)
		{
			if(xs[i] < xmin)
			{
				xmin = xs[i];
			}
			if(xs[i] > xmax)
			{
				xmax = xs[i];
			}
		}
		fill_rect(xmin, y, xmax - xmin + 1, 1, c);
	}
}

int draw_text(int x, int y, const char* s, Color fg, int scale)
{
	if(scale < 1)
	{
		scale = 1;
	}
	detail::g_dirty = 1;
	// Track the glyphs actually written so the drawn box stays tight.
	int bx0 = x, bx1 = x, by0 = y, by1 = y;
	bool any = false;
	const std::uint16_t px = rgb565(fg);
	int cursor = x;
	for(; *s != '\0'; ++s)
	{
		char ch = *s;
		if(ch < font::kFirst || ch > font::kLast)
		{
			ch = '?';
		}
		const std::uint8_t* glyph = font::kFont8x8[ch - font::kFirst];
		for(int row = 0; row < font::kGlyphH; ++row)
		{
			std::uint8_t bits = glyph[row];
			while(bits != 0)
			{
				// Bit 0 is the leftmost column; handle each set pixel as a
				// scaled block.
				int col = __builtin_ctz(bits);
				bits &= static_cast<std::uint8_t>(bits - 1);
				int bx = cursor + col * scale;
				int by = y + row * scale;
				for(int sy = 0; sy < scale; ++sy)
				{
					int yy = by + sy;
					if(yy < 0 || yy >= kHeight)
					{
						continue;
					}
					std::uint16_t* line =
					    &g_framebuffer[static_cast<std::uint32_t>(yy) * kWidth];
					for(int sx = 0; sx < scale; ++sx)
					{
						int xx = bx + sx;
						if(xx >= 0 && xx < kWidth)
						{
							line[xx] = px;
							if(xx < bx0) bx0 = xx;
							if(xx > bx1) bx1 = xx;
							if(yy < by0) by0 = yy;
							if(yy > by1) by1 = yy;
							any = true;
						}
					}
				}
			}
		}
		cursor += font::kGlyphW * scale;
	}
	if(any)
	{
		mark_rect(bx0, by0, bx1 + 1, by1 + 1);
	}
	return cursor;
}

int text_width(const char* s, int scale)
{
	if(scale < 1)
	{
		scale = 1;
	}
	int n = 0;
	while(s[n] != '\0')
	{
		++n;
	}
	return n * font::kGlyphW * scale;
}

namespace detail
{

int g_box_x0 = 0;
int g_box_y0 = 0;
int g_box_x1 = 0;
int g_box_y1 = 0;
bool g_box_any = false;

void flush_box()
{
	// The box is only meaningful relative to the buffer that was just drawn
	// into: swap_buffers() calls this after the exchange, so the fresh pixels
	// are in g_front and the demoted g_back is stale in exactly this region.
	// Copy the region row by row (it is screen-clamped, so the rows stay in
	// bounds) and reset the box for the next draw pass.
	if(!g_box_any)
	{
		return;
	}
	for(int y = g_box_y0; y <= g_box_y1; ++y)
	{
		std::uint16_t* src = &g_front[static_cast<std::uint32_t>(y) * kWidth +
	                                 g_box_x0];
		std::uint16_t* dst = &g_back[static_cast<std::uint32_t>(y) * kWidth +
	                               g_box_x0];
		std::memcpy(dst, src,
		            static_cast<std::size_t>(g_box_x1 - g_box_x0 + 1) *
		            sizeof(std::uint16_t));
	}
	g_box_any = false;
}

}  // namespace detail

}  // namespace display
