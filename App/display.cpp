// SPDX-License-Identifier: MIT
#include "display.hpp"

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
}

void fill(Color c) {
  // BCCR: [23:16]=Red [15:8]=Green [7:0]=Blue. Trigger an immediate reload.
  LTDC->BCCR = (static_cast<std::uint32_t>(c.r) << 16) |
               (static_cast<std::uint32_t>(c.g) << 8) |
               static_cast<std::uint32_t>(c.b);
  LTDC->SRCR = LTDC_SRCR_IMR;
}

}  // namespace display
