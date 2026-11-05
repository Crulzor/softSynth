// SPDX-License-Identifier: MIT
//
// Host stand-in for the LTDC double-buffered framebuffer. On the target
// display.cpp owns the RGB565 pair in AXI-SRAM and the line-event ISR swaps
// it at a frame start; here it is two plain arrays and a plain pointer swap,
// so a test can emulate the frame-start boundary by calling
// display::swap_buffers() between UI frames. g_framebuffer always points at
// the back buffer, what the drawing calls write into.
#include "display.hpp"

namespace display::detail
{
namespace
{
std::uint16_t g_buf_a[kWidth * kHeight];
std::uint16_t g_buf_b[kWidth * kHeight];
}  // namespace
std::uint16_t* g_back = g_buf_a;
std::uint16_t* g_front = g_buf_b;
std::uint16_t* g_framebuffer = g_back;
// Host has no swap interrupt; the drawing code still sets it.
volatile std::uint32_t g_dirty = 0;
}  // namespace display::detail

namespace display
{
void swap_buffers()
{
	std::uint16_t* t = detail::g_back;
	detail::g_back = detail::g_front;
	detail::g_front = t;
	detail::g_framebuffer = detail::g_back;
	// The host stand-in emulates the target's line-0 flip, which converges
	// the drawn box into the demoted buffer as it exchanges the pointers;
	// the tests rely on that to check ghosting the way the panel would.
	detail::flush_box();
}
}  // namespace display
