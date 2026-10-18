// SPDX-License-Identifier: MIT
#include "params/parameters.hpp"

namespace params
{
namespace
{

constexpr const char* kLfoDestNames[] = {"---", "PIT", "SHP",
                                         "MIX", "AMP", "CUT"};
static_assert(sizeof(kLfoDestNames) / sizeof(kLfoDestNames[0]) ==
                  static_cast<int>(LfoDest::Count),
              "one name per LfoDest");

constexpr float kLastDest = static_cast<float>(LfoDest::Count) - 1.0f;

// Indexed by Id; keep in the same order as the enum.
constexpr Spec kSpecs[kCount] = {
    // label  min     max        def    step  format
    {"PIT", -12.0f, 12.0f, 0.0f, 1.0f, Format::Signed, nullptr},
    {"SHP", 0.0f, 1.0f, 0.0f, 0.0f, Format::Scale127, nullptr},
    {"OCT", -3.0f, 3.0f, 0.0f, 1.0f, Format::Signed, nullptr},
    {"PIT", -12.0f, 12.0f, 0.0f, 1.0f, Format::Signed, nullptr},
    {"SHP", 0.0f, 1.0f, 0.0f, 0.0f, Format::Scale127, nullptr},
    {"OCT", -3.0f, 3.0f, 0.0f, 1.0f, Format::Signed, nullptr},
    {"MIX", 0.0f, 1.0f, 0.5f, 0.0f, Format::Scale127, nullptr},

    {"ATK", 0.0f, 1.0f, 0.05f, 0.0f, Format::Scale127, nullptr},
    {"DEC", 0.0f, 1.0f, 0.5f, 0.0f, Format::Scale127, nullptr},

    {"SPD", 0.0f, 1.0f, 0.3f, 0.0f, Format::Scale127, nullptr},
    {"DST", 0.0f, kLastDest, 0.0f, 1.0f, Format::Enum, kLfoDestNames},
    {"DEP", -1.0f, 1.0f, 0.0f, 0.0f, Format::Bipolar64, nullptr},
    {"SPD", 0.0f, 1.0f, 0.3f, 0.0f, Format::Scale127, nullptr},
    {"DST", 0.0f, kLastDest, 0.0f, 1.0f, Format::Enum, kLfoDestNames},
    {"DEP", -1.0f, 1.0f, 0.0f, 0.0f, Format::Bipolar64, nullptr},

    {"BPM", 30.0f, 300.0f, 120.0f, 1.0f, Format::Unsigned, nullptr},
};

}  // namespace

const Spec& spec(Id id) { return kSpecs[static_cast<int>(id)]; }

void Parameters::reset()
{
	for(int i = 0; i < kCount; ++i)
	{
		values_[i] = kSpecs[i].def;
	}
}

void Parameters::set(Id id, float value)
{
	const Spec& s = spec(id);
	if(value < s.min)
	{
		value = s.min;
	}
	if(value > s.max)
	{
		value = s.max;
	}
	if(s.step > 0.0f)
	{
		// value - min is >= 0 here, so truncating after +0.5 rounds to nearest.
		const int k = static_cast<int>((value - s.min) / s.step + 0.5f);
		value = s.min + static_cast<float>(k) * s.step;
	}
	values_[index(id)] = value;
}

float Parameters::normalized(Id id) const
{
	const Spec& s = spec(id);
	return (get(id) - s.min) / (s.max - s.min);
}

void Parameters::set_normalized(Id id, float n)
{
	const Spec& s = spec(id);
	set(id, s.min + n * (s.max - s.min));
}

}  // namespace params
