#include "codec.hpp"

#include "i2c4.hpp"

extern "C"
{
#include "main.h"
#include "wm8994/wm8994.h"
}

namespace
{

// 7-bit address 0x1A, shifted left by 1 for the HAL (R/W bit) -> 0x34.
constexpr uint16_t kCodecI2cAddress = 0x34;

// Convert a 0..100 volume to the codec's 0..63 scale.
constexpr uint8_t to_codec_volume(uint8_t v)
{
	return v > 100
	           ? 63
	           : static_cast<uint8_t>((static_cast<uint32_t>(v) * 63) / 100);
}

WM8994_Object_t g_codec{};

// --- WM8994 bus IO callbacks (plain C signatures the driver expects) -------
// These adapt the shared i2c4 bus to the WM8994 driver, which addresses its
// registers with a 16-bit register address.

int32_t i2c4_init() { return i2c4::init() ? 0 : -1; }

int32_t i2c4_deinit() { return 0; }

int32_t i2c4_write(uint16_t addr, uint16_t reg, uint8_t* data, uint16_t len)
{
	return i2c4::write(addr, reg, 2, data, len) ? 0 : -1;
}

int32_t i2c4_read(uint16_t addr, uint16_t reg, uint8_t* data, uint16_t len)
{
	return i2c4::read(addr, reg, 2, data, len) ? 0 : -1;
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
