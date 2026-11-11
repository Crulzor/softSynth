// SPDX-License-Identifier: MIT
#include "dsp/sine_table.hpp"

#include <cmath>

namespace dsp::sine_table
{

float g_table[kSize + 1];

void init()
{
	for(int i = 0; i <= kSize; ++i)
	{
		// Computed in double so the table itself carries no float rounding
		// beyond the final store.
		g_table[i] = static_cast<float>(
		    std::sin(6.283185307179586 * static_cast<double>(i) / kSize));
	}
	g_table[kSize] = g_table[0];
}

}  // namespace dsp::sine_table
