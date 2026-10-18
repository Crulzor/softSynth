// SPDX-License-Identifier: MIT
//
// Host stand-in for the LTDC framebuffer. On the target display.cpp defines it
// in AXI-SRAM; here it is a plain array that render_ui dumps to an image.
#include "display.hpp"

namespace display::detail
{
std::uint16_t g_framebuffer[kWidth * kHeight];
}  // namespace display::detail
