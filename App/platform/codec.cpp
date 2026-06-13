#include "codec.hpp"

extern "C"
{
#include "main.h"
#include "wm8994/wm8994.h"
}

namespace
{

// 7-bit address 0x1A, shifted left by 1 for the HAL (R/W bit) -> 0x34.
constexpr uint16_t kCodecI2cAddress = 0x34;

// I2C4 TIMINGR for ~100 kHz standard mode from a 64 MHz kernel clock
// (I2C4SEL = rcc_pclk4 = 64 MHz here): PRESC=9, SCLDEL=4, SDADEL=2,
// SCLH=0x1B, SCLL=0x1F. The codec is happy anywhere up to 400 kHz, so the
// exact value is not critical.
constexpr uint32_t kI2c4Timing = 0x90421B1F;

// Convert a 0..100 volume to the codec's 0..63 scale.
constexpr uint8_t to_codec_volume(uint8_t v)
{
	return v > 100
	           ? 63
	           : static_cast<uint8_t>((static_cast<uint32_t>(v) * 63) / 100);
}

I2C_HandleTypeDef g_i2c4{};
WM8994_Object_t g_codec{};

// --- WM8994 bus IO callbacks (plain C signatures the driver expects) -------

int32_t i2c4_init()
{
	if(g_i2c4.Instance == I2C4)
	{
		return 0;  // already initialised
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
	g_i2c4.Init.Timing = kI2c4Timing;
	g_i2c4.Init.OwnAddress1 = 0;
	g_i2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	g_i2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	g_i2c4.Init.OwnAddress2 = 0;
	g_i2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
	g_i2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	g_i2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
	if(HAL_I2C_Init(&g_i2c4) != HAL_OK)
	{
		return -1;
	}
	HAL_I2CEx_ConfigAnalogFilter(&g_i2c4, I2C_ANALOGFILTER_ENABLE);
	HAL_I2CEx_ConfigDigitalFilter(&g_i2c4, 0);
	return 0;
}

int32_t i2c4_deinit() { return 0; }

int32_t i2c4_write(uint16_t addr, uint16_t reg, uint8_t* data, uint16_t len)
{
	return HAL_I2C_Mem_Write(&g_i2c4, addr, reg, I2C_MEMADD_SIZE_16BIT, data,
	                         len, 1000) == HAL_OK
	           ? 0
	           : -1;
}

int32_t i2c4_read(uint16_t addr, uint16_t reg, uint8_t* data, uint16_t len)
{
	return HAL_I2C_Mem_Read(&g_i2c4, addr, reg, I2C_MEMADD_SIZE_16BIT, data,
	                        len, 1000) == HAL_OK
	           ? 0
	           : -1;
}

int32_t get_tick() { return static_cast<int32_t>(HAL_GetTick()); }

}  // namespace

namespace codec
{

bool init(uint32_t sample_rate, uint8_t volume)
{
	WM8994_IO_t io{};
	io.Init = i2c4_init;
	io.DeInit = i2c4_deinit;
	io.Address = kCodecI2cAddress;
	io.WriteReg = i2c4_write;
	io.ReadReg = i2c4_read;
	io.GetTick = get_tick;

	if(WM8994_RegisterBusIO(&g_codec, &io) != WM8994_OK)
	{
		return false;
	}

	uint32_t id = 0;
	if(WM8994_ReadID(&g_codec, &id) != WM8994_OK || id != WM8994_ID)
	{
		return false;
	}

	WM8994_Init_t cfg{};
	cfg.InputDevice = WM8994_IN_NONE;
	cfg.OutputDevice = WM8994_OUT_HEADPHONE;
	cfg.Frequency = sample_rate;
	cfg.Resolution = WM8994_RESOLUTION_16b;
	cfg.Volume = to_codec_volume(volume);
	return WM8994_Init(&g_codec, &cfg) == WM8994_OK;
}

void play() { WM8994_Play(&g_codec); }

void set_volume(uint8_t volume)
{
	WM8994_SetVolume(&g_codec, VOLUME_OUTPUT, to_codec_volume(volume));
}

}  // namespace codec
