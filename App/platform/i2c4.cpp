// SPDX-License-Identifier: MIT
#include "i2c4.hpp"

extern "C"
{
#include "main.h"
}

namespace
{

// TIMINGR for ~100 kHz standard mode from a 64 MHz kernel clock
// (I2C4SEL = rcc_pclk4 = 64 MHz): PRESC=9, SCLDEL=4, SDADEL=2, SCLH=0x1B,
// SCLL=0x1F. Both the WM8994 and the FT5336 are happy up to 400 kHz, so the
// exact value is not critical.
constexpr uint32_t kTiming = 0x90421B1F;

I2C_HandleTypeDef g_i2c4{};

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

bool read(uint16_t dev_addr, uint16_t reg, uint16_t reg_size, uint8_t* data,
          uint16_t len)
{
	return HAL_I2C_Mem_Read(&g_i2c4, dev_addr, reg, reg_addr_size(reg_size),
	                        data, len, 1000) == HAL_OK;
}

bool write(uint16_t dev_addr, uint16_t reg, uint16_t reg_size,
           const uint8_t* data, uint16_t len)
{
	return HAL_I2C_Mem_Write(&g_i2c4, dev_addr, reg, reg_addr_size(reg_size),
	                         const_cast<uint8_t*>(data), len, 1000) == HAL_OK;
}

}  // namespace i2c4
