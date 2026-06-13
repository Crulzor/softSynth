// SPDX-License-Identifier: MIT
#include "touch.hpp"

#include "i2c4.hpp"

namespace
{

// FT5336 7-bit address 0x38, HAL-shifted left for the R/W bit -> 0x70.
constexpr uint16_t kTsAddress = 0x70;

// FT5336 register map (8-bit register addresses).
constexpr uint8_t kRegChipId = 0xA8;   // reads back 0x51 on the FT5336
constexpr uint8_t kRegTdStatus = 0x02; // low nibble = number of active touches
constexpr uint8_t kRegP1XH = 0x03;     // X high nibble + event flag
// 0x04 = P1 X low, 0x05 = P1 Y high nibble, 0x06 = P1 Y low (auto-increment).

constexpr uint8_t kChipId = 0x51;

}  // namespace

namespace touch
{

bool init()
{
	if(!i2c4::init())
	{
		return false;
	}

	uint8_t id = 0;
	if(!i2c4::read(kTsAddress, kRegChipId, 1, &id, 1))
	{
		return false;
	}
	return id == kChipId;
}

Point read()
{
	// One burst read covers the status byte and the first touch point:
	// [TD_STATUS, P1_XH, P1_XL, P1_YH, P1_YL].
	uint8_t buf[5] = {0};
	if(!i2c4::read(kTsAddress, kRegTdStatus, 1, buf, sizeof(buf)))
	{
		return Point{false, 0, 0};
	}

	const uint8_t touches = buf[0] & 0x0F;
	if(touches == 0)
	{
		return Point{false, 0, 0};
	}

	// High registers carry the top 4 bits of each 12-bit coordinate; the upper
	// bits of XH are an event flag and must be masked off.
	const uint16_t raw_x = static_cast<uint16_t>(((buf[1] & 0x0F) << 8) | buf[2]);
	const uint16_t raw_y = static_cast<uint16_t>(((buf[3] & 0x0F) << 8) | buf[4]);

	// The controller reports in its own portrait frame while the panel runs
	// landscape: swap the axes to match the LTDC framebuffer. Verified on
	// hardware — top-left tap -> raw(2,1), bottom-right -> raw(271,479): a pure
	// swap, no mirroring. So display.x = raw_y, display.y = raw_x.
	return Point{true, raw_y, raw_x};
}

}  // namespace touch
