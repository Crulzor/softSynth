// SPDX-License-Identifier: MIT
#include "display.hpp"

#include <atomic>

extern "C"
{
#include "main.h"  // HAL + CubeMX-generated pin labels (LCD_*_Pin / _GPIO_Port)
}

namespace display
{

namespace detail
{
// The double-buffered pair in internal AXI-SRAM: 2 x 480*272*2 = 510 KB in the
// 512 KB. Placed by the linker in the .framebuffer (NOLOAD) section so they
// don't bloat the image and aren't zeroed by startup; init() clears them.
// g_back is what the CPU draws into, g_front what the LTDC scans out; the two
// are exchanged at the start of every frame (line event), so the panel never
// sees a half-drawn frame.
alignas(32) std::uint16_t g_buf_a[kWidth * kHeight]
    __attribute__((section(".framebuffer")));
alignas(32) std::uint16_t g_buf_b[kWidth * kHeight]
    __attribute__((section(".framebuffer")));
std::uint16_t* g_back = g_buf_a;
std::uint16_t* g_front = g_buf_b;
std::uint16_t* g_framebuffer = g_back;  // -> the back buffer (drawing target)
// Set by the drawing calls once a frame has been drawn; the frame-swap
// interrupt clears it when it flips, so an idle UI never re-flips an old
// buffer to the front.
volatile std::uint32_t g_dirty = 0;
// Non-zero while begin_draw()/end_draw() wraps a foreground pass; the swap
// defers to the next boundary rather than flipping a half-drawn frame.
volatile std::uint32_t g_in_draw = 0;
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

// Point LTDC layer 1 at the front buffer, covering the whole panel.
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
	cfg.FBStartAdress = reinterpret_cast<std::uint32_t>(detail::g_front);
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

	// Both buffers start cleared; the LTDC scans g_front, the CPU draws into
	// g_back.
	detail::g_framebuffer = detail::g_front;
	fill(background);
	detail::g_framebuffer = detail::g_back;
	fill(background);
	layer_init();
	// The fills above marked the buffers dirty and the drawn box as the whole
	// screen; clear both so the first real flip doesn't copy a full frame
	// that both buffers already share.
	detail::g_dirty = 0;
	detail::g_box_any = false;
}

void swap_buffers()
{
	std::uint16_t* back = detail::g_back;
	std::uint16_t* front = detail::g_front;
	detail::g_back = front;
	detail::g_front = back;
	detail::g_framebuffer = detail::g_back;

	// Converge the buffers: the pixels drawn since the last promotion are now
	// in the (new) front buffer; pull the drawn box out of it into the (new)
	// back buffer so a one-shot widget change (a cycled label) lands in both
	// and can't ghost while the tab bar keeps flipping the pair. No-op when
	// the frame drew nothing. In the line-0 IRQ this runs during vertical
	// blanking: the demoted back buffer is not being scanned, so the copy is
	// invisible.
	detail::flush_box();

	// Shadow write + immediate reload: from the next line the panel reads the
	// new front buffer. Both buffers always hold a complete frame (the UI is
	// retained-mode), so the one boundary line that may still be scanned from
	// the previous frame is indistinguishable.
	LTDC_LAYER(&g_ltdc, LTDC_LAYER_1)->CFBAR =
	    reinterpret_cast<std::uint32_t>(detail::g_front);
	g_ltdc.Instance->SRCR |= LTDC_SRCR_IMR;
}

void enable_swap_irq()
{
	// Below the audio DMA stream (5): the swap costs a handful of register
	// writes and must never delay an audio block.
	HAL_NVIC_SetPriority(LTDC_IRQn, 6, 0);
	HAL_NVIC_EnableIRQ(LTDC_IRQn);
	g_ltdc.Instance->LIPCR = 0;
	g_ltdc.Instance->IER |= LTDC_IT_LI;
}

void begin_draw()
{
	// Mark the pass; the line-event callback sees the flag and defers the flip
	// to a later boundary instead of promoting a half-drawn frame. The line
	// interrupt itself stays armed. Masking it here (clearing LIE) raced the
	// LTDC: a line-0 request latched while LIE was still set but serviced after
	// the mask landed was skipped by HAL_LTDC_IRQHandler (it only clears LIF
	// when LIE is set), so LIF stayed set and the IRQ re-entered forever,
	// starving this loop (docs/ltdc-lie-race.md).
	detail::g_in_draw = 1;
	// Keep the compiler from hoisting framebuffer writes above the flag.
	std::atomic_signal_fence(std::memory_order_seq_cst);
}

void end_draw()
{
	// Every pixel of the pass is written before the flag drops, so a boundary
	// event that sees g_in_draw == 0 promotes a complete frame.
	std::atomic_signal_fence(std::memory_order_seq_cst);
	detail::g_in_draw = 0;
}

// The LTDC IRQ vector and the HAL's weak line-event callback. They live in
// this namespace to reach g_ltdc; the extern "C" gives them the names the
// startup vector table and the HAL look up.
extern "C"
void LTDC_IRQHandler(void)
{
	HAL_LTDC_IRQHandler(&g_ltdc);
}

extern "C"
void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef* hltdc)
{
	// Flip only when the foreground has drawn a new frame: until then the
	// panel keeps scanning the last complete frame, so a retained-mode UI
	// (which redraws only invalidated widgets) never alternates between a
	// fresh buffer and a stale one. The in_draw check defers to the next
	// boundary if the boundary fell inside a foreground draw pass.
	if(detail::g_dirty && !detail::g_in_draw)
	{
		detail::g_dirty = 0;
		swap_buffers();
	}
	// The HAL masked and cleared the line interrupt before calling us; re-arm
	// it for the next frame. Raw writes, not HAL calls: this runs in the IRQ
	// itself, and the HAL's handle lock is not ISR-safe here.
	hltdc->Instance->LIPCR = 0;
	hltdc->Instance->IER |= LTDC_IT_LI;
}

}  // namespace display
