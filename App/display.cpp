// SPDX-License-Identifier: MIT
#include "display.hpp"

#include "font8x8.hpp"

extern "C" {
#include "main.h"  // HAL + CubeMX-generated pin labels (LCD_*_Pin / _GPIO_Port)
}

namespace display {
namespace {

// RK043FN48H 4.3" 480x272 panel timing (values from ST's BSP rk043fn48h.h).
constexpr std::uint16_t kHsync = 41;
constexpr std::uint16_t kHbp = 13;
constexpr std::uint16_t kHfp = 32;
constexpr std::uint16_t kVsync = 10;
constexpr std::uint16_t kVbp = 2;
constexpr std::uint16_t kVfp = 2;

LTDC_HandleTypeDef g_ltdc{};

// RGB565 framebuffer in internal AXI-SRAM. 480*272*2 = ~255 KB, which fits the
// 512 KB AXI-SRAM. Placed by the linker in the .framebuffer (NOLOAD) section so
// it doesn't bloat the image and isn't zeroed by startup; init() clears it.
alignas(32) std::uint16_t g_framebuffer[kWidth * kHeight]
    __attribute__((section(".framebuffer")));

// Pack an 8-8-8 colour into the panel's 5-6-5 layout.
constexpr std::uint16_t rgb565(Color c) {
  return static_cast<std::uint16_t>(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) |
                                    (c.b >> 3));
}

// Point LTDC layer 1 at the framebuffer, covering the whole panel.
void layer_init() {
  LTDC_LayerCfgTypeDef cfg{};
  cfg.WindowX0 = 0;
  cfg.WindowX1 = kWidth;
  cfg.WindowY0 = 0;
  cfg.WindowY1 = kHeight;
  cfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  cfg.Alpha = 255;
  cfg.Alpha0 = 0;
  cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  cfg.FBStartAdress = reinterpret_cast<std::uint32_t>(g_framebuffer);
  cfg.ImageWidth = kWidth;
  cfg.ImageHeight = kHeight;
  cfg.Backcolor.Red = 0;
  cfg.Backcolor.Green = 0;
  cfg.Backcolor.Blue = 0;

  if (HAL_LTDC_ConfigLayer(&g_ltdc, &cfg, LTDC_LAYER_1) != HAL_OK) {
    Error_Handler();
  }
}

// Drive the panel control lines that CubeMX leaves as inputs / asserted-reset:
//   PD7  (LCD_DISPD7) -> panel DISP enable, must be HIGH
//   PB12 (LCD_RST)    -> panel reset, pulse low then release HIGH
//   PK0  (LCD_BL)     -> backlight enable, must be HIGH
// (The RGB/sync/clk data lines are configured as AF14 by HAL_LTDC_MspInit.)
void panel_control_init() {
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOK_CLK_ENABLE();

  GPIO_InitTypeDef gpio{};
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;

  gpio.Pin = LCD_DISPD7_Pin;
  HAL_GPIO_Init(LCD_DISPD7_GPIO_Port, &gpio);
  gpio.Pin = LCD_RST_Pin;
  HAL_GPIO_Init(LCD_RST_GPIO_Port, &gpio);
  gpio.Pin = LCD_BL_Pin;
  HAL_GPIO_Init(LCD_BL_GPIO_Port, &gpio);

  // Reset pulse.
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(20);

  // Enable the display and backlight.
  HAL_GPIO_WritePin(LCD_DISPD7_GPIO_Port, LCD_DISPD7_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
}

void ltdc_init(Color background) {
  // HAL_LTDC_Init() invokes HAL_LTDC_MspInit() (in stm32h7xx_hal_msp.c), which
  // configures the LTDC AF pins and the LTDC pixel clock (PLL3 -> ~9.6 MHz).
  g_ltdc.Instance = LTDC;
  g_ltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  g_ltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  g_ltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  g_ltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  g_ltdc.Init.HorizontalSync = kHsync - 1;                       // 40
  g_ltdc.Init.VerticalSync = kVsync - 1;                         // 9
  g_ltdc.Init.AccumulatedHBP = kHsync + kHbp - 1;                // 53
  g_ltdc.Init.AccumulatedVBP = kVsync + kVbp - 1;                // 11
  g_ltdc.Init.AccumulatedActiveW = kHsync + kHbp + kWidth - 1;   // 533
  g_ltdc.Init.AccumulatedActiveH = kVsync + kVbp + kHeight - 1;  // 283
  g_ltdc.Init.TotalWidth = kHsync + kHbp + kWidth + kHfp - 1;    // 565
  g_ltdc.Init.TotalHeigh = kVsync + kVbp + kHeight + kVfp - 1;   // 285
  g_ltdc.Init.Backcolor.Red = background.r;
  g_ltdc.Init.Backcolor.Green = background.g;
  g_ltdc.Init.Backcolor.Blue = background.b;

  if (HAL_LTDC_Init(&g_ltdc) != HAL_OK) {
    Error_Handler();
  }
  // No layer is enabled: every pixel shows the background colour above.
  // HAL_LTDC_Init() already enabled the controller, so the panel is now live.
}

}  // namespace

void init(Color background) {
  panel_control_init();
  ltdc_init(background);
  layer_init();
  fill(background);
}

void fill(Color c) {
  const std::uint16_t px = rgb565(c);
  for (std::uint32_t i = 0; i < kWidth * kHeight; ++i) {
    g_framebuffer[i] = px;
  }
}

void put_pixel(int x, int y, Color c) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
    return;
  }
  g_framebuffer[static_cast<std::uint32_t>(y) * kWidth + x] = rgb565(c);
}

void fill_rect(int x, int y, int w, int h, Color c) {
  // Clip to the screen so callers don't have to.
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + w > kWidth ? kWidth : x + w;
  int y1 = y + h > kHeight ? kHeight : y + h;
  const std::uint16_t px = rgb565(c);
  for (int yy = y0; yy < y1; ++yy) {
    std::uint16_t* row = &g_framebuffer[static_cast<std::uint32_t>(yy) * kWidth];
    for (int xx = x0; xx < x1; ++xx) {
      row[xx] = px;
    }
  }
}

int draw_text(int x, int y, const char* s, Color fg, int scale) {
  if (scale < 1) {
    scale = 1;
  }
  const std::uint16_t px = rgb565(fg);
  int cursor = x;
  for (; *s != '\0'; ++s) {
    char ch = *s;
    if (ch < font::kFirst || ch > font::kLast) {
      ch = '?';
    }
    const std::uint8_t* glyph = font::kFont8x8[ch - font::kFirst];
    for (int row = 0; row < font::kGlyphH; ++row) {
      std::uint8_t bits = glyph[row];
      while (bits != 0) {
        // Bit 0 is the leftmost column; handle each set pixel as a scaled block.
        int col = __builtin_ctz(bits);
        bits &= static_cast<std::uint8_t>(bits - 1);
        int bx = cursor + col * scale;
        int by = y + row * scale;
        for (int sy = 0; sy < scale; ++sy) {
          int yy = by + sy;
          if (yy < 0 || yy >= kHeight) {
            continue;
          }
          std::uint16_t* line =
              &g_framebuffer[static_cast<std::uint32_t>(yy) * kWidth];
          for (int sx = 0; sx < scale; ++sx) {
            int xx = bx + sx;
            if (xx >= 0 && xx < kWidth) {
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

}  // namespace display
