// SPDX-License-Identifier: MIT
#include "ui/ui.hpp"

namespace ui
{

void Ui::update(const Touch& touch, std::uint32_t now_ms)
{
	if(touch.pressed && !was_pressed_)
	{
		page_.press(touch.x, touch.y, now_ms);
	}
	else if(touch.pressed)
	{
		page_.drag(touch.x, touch.y, now_ms);
	}
	else if(was_pressed_)
	{
		page_.release(now_ms);
	}
	was_pressed_ = touch.pressed;

	page_.update(now_ms);
}

}  // namespace ui
