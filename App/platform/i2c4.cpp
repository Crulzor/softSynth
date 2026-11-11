// SPDX-License-Identifier: MIT
#include "i2c4.hpp"

extern "C"
{
#include "main.h"
}

namespace
{

// TIMINGR for ~100 kHz standard mode from a 64 MHz kernel clock
// (I2C4SEL = HSI = 64 MHz, pinned by sysclock::init()): PRESC=9, SCLDEL=4,
// SDADEL=2, SCLH=0x1B, SCLL=0x1F. Both the WM8994 and the FT5336 are happy up
// to 400 kHz, so the exact value is not critical.
constexpr uint32_t kTiming = 0x90421B1F;

// Per-transfer timeout. Our transfers are a few bytes (<1 ms at 100 kHz), so
// this only matters when a device stops answering - and then it bounds how
// long the foreground loop (touch poll, UI) stalls per call. The HAL's usual
// 1000 ms froze the UI for a second per frame on a wedged touch controller.
constexpr uint32_t kTimeoutMs = 10;

I2C_HandleTypeDef g_i2c4{};

// Read over SWD while debugging (non-halting): failed transfers, recoveries.
volatile uint32_t g_errors = 0;
volatile uint32_t g_recoveries = 0;
volatile uint32_t g_last_error = 0;  // HAL_I2C_ERROR_* bits of the last failure

// ~5 us at 480 MHz: half an SCL period of the bus-clear pulses (100 kHz).
void bus_delay()
{
	for(volatile int i = 0; i < 600; ++i)
	{
	}
}

// Free the bus: reset the peripheral, then drive SCL by hand - nine pulses
// make any slave that was mid-byte finish it and release SDA - and end with a
// STOP (SDA low -> high while SCL is high). The pins go back to I2C when
// init() runs again.
void bus_clear()
{
	HAL_I2C_DeInit(&g_i2c4);
	__HAL_RCC_I2C4_FORCE_RESET();
	__HAL_RCC_I2C4_RELEASE_RESET();
	g_i2c4.Instance = nullptr;

	GPIO_InitTypeDef gpio{};
	gpio.Pin = GPIO_PIN_12 | GPIO_PIN_13;  // PD12 = SCL, PD13 = SDA
	gpio.Mode = GPIO_MODE_OUTPUT_OD;
	gpio.Pull = GPIO_PULLUP;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12 | GPIO_PIN_13, GPIO_PIN_SET);
	HAL_GPIO_Init(GPIOD, &gpio);
	bus_delay();
	for(int i = 0; i < 9; ++i)
	{
		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET);
		bus_delay();
		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);
		bus_delay();
	}
	// STOP.
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET);
	bus_delay();
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);
	bus_delay();
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);
	bus_delay();
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);
	bus_delay();
}

constexpr uint16_t reg_addr_size(uint16_t reg_size)
{
	return reg_size == 2 ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
}

}  // namespace

namespace i2c4
{

bool init()
{
	if(g_i2c4.Instance == I2C4)
	{
		return true;  // already initialised by whichever driver got here first
	}

	__HAL_RCC_GPIOD_CLK_ENABLE();
	GPIO_InitTypeDef gpio{};
	gpio.Pin = GPIO_PIN_12 | GPIO_PIN_13;  // PD12 = SCL, PD13 = SDA
	gpio.Mode = GPIO_MODE_AF_OD;
	gpio.Pull = GPIO_PULLUP;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	gpio.Alternate = GPIO_AF4_I2C4;
	HAL_GPIO_Init(GPIOD, &gpio);

	__HAL_RCC_I2C4_CLK_ENABLE();
	g_i2c4.Instance = I2C4;
	g_i2c4.Init.Timing = kTiming;
	g_i2c4.Init.OwnAddress1 = 0;
	g_i2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	g_i2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	g_i2c4.Init.OwnAddress2 = 0;
	g_i2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
	g_i2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	g_i2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
	if(HAL_I2C_Init(&g_i2c4) != HAL_OK)
	{
		g_i2c4.Instance = nullptr;  // leave it un-initialised so a retry works
		return false;
	}
	HAL_I2CEx_ConfigAnalogFilter(&g_i2c4, I2C_ANALOGFILTER_ENABLE);
	HAL_I2CEx_ConfigDigitalFilter(&g_i2c4, 0);
	return true;
}

namespace
{

// After any failed transfer: count it, clear the bus and bring I2C4 back up,
// so the next transfer starts from a clean peripheral and a free bus.
bool fail()
{
	++g_errors;
	g_last_error = HAL_I2C_GetError(&g_i2c4);
	bus_clear();
	if(init())
	{
		++g_recoveries;
	}
	return false;
}

}  // namespace

bool read(uint16_t dev_addr, uint16_t reg, uint16_t reg_size, uint8_t* data,
          uint16_t len)
{
	if(g_i2c4.Instance != I2C4 && !init())
	{
		return false;
	}
	if(HAL_I2C_Mem_Read(&g_i2c4, dev_addr, reg, reg_addr_size(reg_size), data,
	                    len, kTimeoutMs) != HAL_OK)
	{
		return fail();
	}
	return true;
}

bool write(uint16_t dev_addr, uint16_t reg, uint16_t reg_size,
           const uint8_t* data, uint16_t len)
{
	if(g_i2c4.Instance != I2C4 && !init())
	{
		return false;
	}
	if(HAL_I2C_Mem_Write(&g_i2c4, dev_addr, reg, reg_addr_size(reg_size),
	                     const_cast<uint8_t*>(data), len, kTimeoutMs) != HAL_OK)
	{
		return fail();
	}
	return true;
}

uint32_t errors() { return g_errors; }
uint32_t recoveries() { return g_recoveries; }

}  // namespace i2c4
