// SPDX-License-Identifier: MIT
#include "params/parameters.hpp"

namespace params
{
namespace
{

constexpr const char* kClockSources[] = {"INT", "EXT+", "EXT-"};
constexpr const char* kLfoShapes[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
constexpr const char* kLfoModes[] = {"FREE", "SYNC"};
constexpr const char* kModFxTypes[] = {"PHSR", "FLNG", "CHRS"};
constexpr const char* kStereo[] = {"MONO", "PING"};
// Same order as dsp::kDelayDivSteps.
constexpr const char* kDelayDivs[] = {"1/32", "1/16T", "1/16", "1/8T",
                                      "1/16.", "1/8",  "1/4T", "1/8.",
                                      "1/4",  "1/4.", "1/2"};
// Same order as dsp::kLfoDivSteps.
constexpr const char* kLfoDivs[] = {"4BAR", "2BAR", "1BAR", "1/2",
                                    "1/4.", "1/4",  "1/4T", "1/8.",
                                    "1/8",  "1/8T", "1/16", "1/32"};
constexpr const char* kClockDivs[] = {"1/16", "1/8", "1/4", "24PQ", "48PQ"};

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
    {"PW", 0.0f, 1.0f, 0.0f, 0.0f, Format::Scale127, nullptr},
    {"PW", 0.0f, 1.0f, 0.0f, 0.0f, Format::Scale127, nullptr},

    {"ATK", 0.0f, 1.0f, 0.05f, 0.0f, Format::Scale127, nullptr},
    {"DEC", 0.0f, 1.0f, 0.5f, 0.0f, Format::Scale127, nullptr},
    {"SUS", 0.0f, 1.0f, 0.7f, 0.0f, Format::Scale127, nullptr},
    {"REL", 0.0f, 1.0f, 0.3f, 0.0f, Format::Scale127, nullptr},

    {"SPD", 0.0f, 1.0f, 0.3f, 0.0f, Format::Scale127, nullptr},
    {"SPD", 0.0f, 1.0f, 0.3f, 0.0f, Format::Scale127, nullptr},
    {"SHP", 0.0f, 4.0f, 1.0f, 1.0f, Format::Enum, kLfoShapes},
    {"SHP", 0.0f, 4.0f, 1.0f, 1.0f, Format::Enum, kLfoShapes},
    {"MODE", 0.0f, 1.0f, 0.0f, 1.0f, Format::Enum, kLfoModes},
    {"MODE", 0.0f, 1.0f, 0.0f, 1.0f, Format::Enum, kLfoModes},
    {"LEN", 0.0f, 11.0f, 5.0f, 1.0f, Format::Enum, kLfoDivs},
    {"LEN", 0.0f, 11.0f, 5.0f, 1.0f, Format::Enum, kLfoDivs},

    {"CUT", 0.0f, 1.0f, 0.7f, 0.0f, Format::Scale127, nullptr},
    {"RES", 0.0f, 1.0f, 0.1f, 0.0f, Format::Scale127, nullptr},
    {"DRV", 0.0f, 1.0f, 0.35f, 0.0f, Format::Scale127, nullptr},

    {"BPM", 30.0f, 300.0f, 120.0f, 1.0f, Format::Unsigned, nullptr},

    {"SRC", 0.0f, 2.0f, 0.0f, 1.0f, Format::Enum, kClockSources},
    {"DIV", 0.0f, 4.0f, 0.0f, 1.0f, Format::Enum, kClockDivs},

    {"TYPE", 0.0f, 2.0f, 0.0f, 1.0f, Format::Enum, kModFxTypes},
    {"MODE", 0.0f, 1.0f, 0.0f, 1.0f, Format::Enum, kLfoModes},
    {"RATE", 0.0f, 1.0f, 0.3f, 0.0f, Format::Scale127, nullptr},
    {"LEN", 0.0f, 11.0f, 2.0f, 1.0f, Format::Enum, kLfoDivs},
    {"DEPTH", 0.0f, 1.0f, 0.6f, 0.0f, Format::Scale127, nullptr},
    {"FBK", 0.0f, 1.0f, 0.5f, 0.0f, Format::Scale127, nullptr},
    {"MIX", 0.0f, 1.0f, 0.0f, 0.0f, Format::Scale127, nullptr},
    {"MODE", 0.0f, 1.0f, 0.0f, 1.0f, Format::Enum, kLfoModes},
    {"MS", 10.0f, 1000.0f, 350.0f, 0.0f, Format::Unsigned, nullptr},
    {"LEN", 0.0f, 10.0f, 7.0f, 1.0f, Format::Enum, kDelayDivs},
    {"FBK", 0.0f, 1.0f, 0.4f, 0.0f, Format::Scale127, nullptr},
    {"TONE", 0.0f, 1.0f, 0.6f, 0.0f, Format::Scale127, nullptr},
    {"STEREO", 0.0f, 1.0f, 1.0f, 1.0f, Format::Enum, kStereo},
    {"MIX", 0.0f, 1.0f, 0.0f, 0.0f, Format::Scale127, nullptr},
    {"DECAY", 0.0f, 1.0f, 0.6f, 0.0f, Format::Scale127, nullptr},
    {"DAMP", 0.0f, 1.0f, 0.4f, 0.0f, Format::Scale127, nullptr},
    {"MIX", 0.0f, 1.0f, 0.0f, 0.0f, Format::Scale127, nullptr},
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
