// SPDX-License-Identifier: MIT
#include "sysclock.hpp"

extern "C"
{
#include "main.h"
}

namespace
{

// DBGMCU REV_ID of rev Y silicon (max 400 MHz). Rev V reads 0x2003.
constexpr std::uint32_t kRevY = 0x1003;

}  // namespace

namespace sysclock
{

bool init()
{
	const bool rev_y = HAL_GetREVID() == kRevY;

	// 1. Raise the core voltage before raising the frequency. VOS0 (needed for
	//    480 MHz) goes through VOS1 + the SYSCFG overdrive bit; the HAL macro
	//    does both, but SYSCFG must be clocked for it.
	__HAL_RCC_SYSCFG_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(rev_y ? PWR_REGULATOR_VOLTAGE_SCALE1
	                                      : PWR_REGULATOR_VOLTAGE_SCALE0);
	while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
	{
	}

	// 2. PLL1 from the HSE (25 MHz, already running in bypass mode):
	//    /5 -> 5 MHz ref, x192 -> 960 MHz VCO, /2 -> 480 MHz  (rev V)
	//    /5 -> 5 MHz ref, x160 -> 800 MHz VCO, /2 -> 400 MHz  (rev Y)
	//    PLL1 isn't SYSCLK yet (the CPU runs on HSI), so the HAL may stop and
	//    reprogram it.
	//    The Q output also feeds the USB 48 MHz clock (USBSEL=PLL, selected in
	//    the OTG_FS MspInit): /20 -> exactly 48.000 MHz from the 960 MHz VCO.
	//    (Rev-V silicon; on 400 MHz rev Y there is no integer 48 MHz from that
	//    VCO, the USB clock would have to come from HSI48 instead.)
	RCC_OscInitTypeDef osc{};
	osc.OscillatorType = RCC_OSCILLATORTYPE_NONE;
	osc.PLL.PLLState = RCC_PLL_ON;
	osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	osc.PLL.PLLM = 5;
	osc.PLL.PLLN = rev_y ? 160 : 192;
	osc.PLL.PLLP = 2;
	osc.PLL.PLLQ = 20;
	osc.PLL.PLLR = 2;
	osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;  // 4-8 MHz reference
	osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
	osc.PLL.PLLFRACN = 0;
	if(HAL_RCC_OscConfig(&osc) != HAL_OK)
	{
		return false;
	}

	// 3. Switch SYSCLK to PLL1. AXI/AHB max is 240 MHz (200 on rev Y) and APB
	//    max 120 MHz, hence /2 and /2. Four flash wait states cover 240 MHz
	//    AXI at VOS0/VOS1; the HAL raises the latency before switching.
	RCC_ClkInitTypeDef clk{};
	clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
	                RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
	                RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
	clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
	clk.AHBCLKDivider = RCC_HCLK_DIV2;
	clk.APB3CLKDivider = RCC_APB3_DIV2;
	clk.APB1CLKDivider = RCC_APB1_DIV2;
	clk.APB2CLKDivider = RCC_APB2_DIV2;
	clk.APB4CLKDivider = RCC_APB4_DIV2;
	if(HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK)
	{
		return false;
	}

	// 4. I2C4's kernel clock defaulted to APB4, which just went from 64 to
	//    120 MHz. Pin it to HSI (64 MHz) so the TIMINGR value in i2c4.cpp,
	//    computed for 64 MHz, stays right whatever the core runs at.
	RCC_PeriphCLKInitTypeDef periph{};
	periph.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
	periph.I2c4ClockSelection = RCC_I2C4CLKSOURCE_HSI;
	if(HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
	{
		return false;
	}

	// 5. Instruction cache: code runs from flash, which at 480 MHz is 4 wait
	//    states behind the core. (D-cache stays off, see sysclock.hpp.)
	SCB_EnableICache();
	return true;
}

std::uint32_t cpu_hz() { return HAL_RCC_GetSysClockFreq(); }

}  // namespace sysclock
