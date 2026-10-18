// SPDX-License-Identifier: MIT
#include "display.hpp"

extern "C"
{
#include "main.h"  // HAL + CubeMX-generated pin labels (LCD_*_Pin / _GPIO_Port)
}

namespace display
{

namespace detail
{
// RGB565 framebuffer in internal AXI-SRAM. 480*272*2 = ~255 KB, which fits the
// 512 KB AXI-SRAM. Placed by the linker in the .framebuffer (NOLOAD) section so
// it doesn't bloat the image and isn't zeroed by startup; init() clears it.
alignas(32) std::uint16_t g_framebuffer[kWidth * kHeight]
    __attribute__((section(".framebuffer")));
}  // namespace detail

namespace
{

// RK043FN48H 4.3" 480x272 panel timing (values from ST's BSP rk043fn48h.h).
constexpr std::uint16_t kHsync = 41;
constexpr std::uint16_t kHbp = 13;
constexpr std::uint16_t kHfp = 32;
constexpr std::uint16_t kVsync = 10;
constexpr std::uint16_t kVbp = 2;
constexpr std::uint16_t kVfp = 2;

LTDC_HandleTypeDef g_ltdc{};

// Point LTDC layer 1 at the framebuffer, covering the whole panel.
void layer_init()
{
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
	cfg.FBStartAdress = reinterpret_cast<std::uint32_t>(detail::g_framebuffer);
	cfg.ImageWidth = kWidth;
	cfg.ImageHeight = kHeight;
	cfg.Backcolor.Red = 0;
	cfg.Backcolor.Green = 0;
	cfg.Backcolor.Blue = 0;

	if(HAL_LTDC_ConfigLayer(&g_ltdc, &cfg, LTDC_LAYER_1) != HAL_OK)
	{
		Error_Handler();
	}
}

// Drive the panel control lines that CubeMX leaves as inputs / asserted-reset:
//   PD7  (LCD_DISPD7) -> panel DISP enable, must be HIGH
//   PB12 (LCD_RST)    -> panel reset, pulse low then release HIGH
//   PK0  (LCD_BL)     -> backlight enable, must be HIGH
// (The RGB/sync/clk data lines are configured as AF14 by HAL_LTDC_MspInit.)
void panel_control_init()
{
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

void ltdc_init(Color background)
{
	// HAL_LTDC_Init() invokes HAL_LTDC_MspInit() (in stm32h7xx_hal_msp.c),
	// which configures the LTDC AF pins and the LTDC pixel clock (PLL3 -> ~9.6
	// MHz).
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

	if(HAL_LTDC_Init(&g_ltdc) != HAL_OK)
	{
		Error_Handler();
	}
	// No layer is enabled: every pixel shows the background colour above.
	// HAL_LTDC_Init() already enabled the controller, so the panel is now live.
}

}  // namespace

void init(Color background)
{
	panel_control_init();
	ltdc_init(background);
	layer_init();
	fill(background);
}

}  // namespace display
