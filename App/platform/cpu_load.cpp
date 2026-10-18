// SPDX-License-Identifier: MIT
#include "cpu_load.hpp"

extern "C"
{
#include "main.h"
}

namespace
{

// Cortex-M7 DWT lock access register key (writes to the DWT are ignored until
// it is unlocked).
constexpr std::uint32_t kDwtUnlock = 0xC5ACCE55;

float g_budget_cycles = 1.0f;  // CPU cycles in one block period

// Written by the audio interrupt, read and reset by the foreground.
volatile std::uint32_t g_sum = 0;
volatile std::uint32_t g_count = 0;
volatile std::uint32_t g_max = 0;

}  // namespace

namespace cpu_load
{

void init(std::uint32_t block_frames, std::uint32_t sample_rate)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->LAR = kDwtUnlock;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	g_budget_cycles = static_cast<float>(HAL_RCC_GetSysClockFreq()) *
	                  static_cast<float>(block_frames) /
	                  static_cast<float>(sample_rate);
}

std::uint32_t begin() { return DWT->CYCCNT; }

void end(std::uint32_t start)
{
	const std::uint32_t cycles = DWT->CYCCNT - start;  // wraps correctly
	g_sum = g_sum + cycles;
	g_count = g_count + 1;
	if(cycles > g_max)
	{
		g_max = cycles;
	}
}

Stats take()
{
	// The interrupt updates three words; mask it briefly so we read (and
	// reset) a consistent set.
	const std::uint32_t primask = __get_PRIMASK();
	__disable_irq();
	const std::uint32_t sum = g_sum;
	const std::uint32_t count = g_count;
	const std::uint32_t max = g_max;
	g_sum = 0;
	g_count = 0;
	g_max = 0;
	__set_PRIMASK(primask);

	if(count == 0)
	{
		return Stats{0.0f, 0.0f};
	}
	return Stats{
	    static_cast<float>(sum) / static_cast<float>(count) / g_budget_cycles,
	    static_cast<float>(max) / g_budget_cycles};
}

}  // namespace cpu_load
