// SPDX-License-Identifier: MIT
//
// Pure drawing into the RGB565 framebuffer. No HAL: this file also compiles in
// the host build (test/), where it draws into a stand-in framebuffer that gets
// dumped to an image.
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

}  // namespace

void fill(Color c)
{
	const std::uint16_t px = rgb565(c);
	for(std::uint32_t i = 0; i < kWidth * kHeight; ++i)
	{
		g_framebuffer[i] = px;
	}
}

void put_pixel(int x, int y, Color c)
{
	if(x < 0 || x >= kWidth || y < 0 || y >= kHeight)
	{
		return;
	}
	g_framebuffer[static_cast<std::uint32_t>(y) * kWidth + x] = rgb565(c);
}

void fill_rect(int x, int y, int w, int h, Color c)
{
	// Clip to the screen so callers don't have to.
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + w > kWidth ? kWidth : x + w;
	int y1 = y + h > kHeight ? kHeight : y + h;
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

int draw_text(int x, int y, const char* s, Color fg, int scale)
{
	if(scale < 1)
	{
		scale = 1;
	}
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
						}
					}
				}
			}
		}
		cursor += font::kGlyphW * scale;
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

}  // namespace display
