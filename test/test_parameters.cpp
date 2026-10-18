// SPDX-License-Identifier: MIT
//
// Unit tests for the shared parameter set and its on-screen formatting.
#include <doctest/doctest.h>

#include <string>

#include "params/parameters.hpp"
#include "ui/widgets.hpp"

using params::Id;
using params::Parameters;

namespace
{

std::string shown(const Parameters& p, Id id)
{
	char buf[8];
	ui::format_value(params::spec(id), p.get(id), buf);
	return buf;
}

}  // namespace

TEST_CASE("parameters start at their spec defaults")
{
	Parameters p;
	for(int i = 0; i < params::kCount; ++i)
	{
		const Id id = static_cast<Id>(i);
		CHECK(p.get(id) == params::spec(id).def);
	}
}

TEST_CASE("set clamps to the range and snaps stepped values")
{
	Parameters p;
	p.set(Id::Osc1Octave, 9.0f);
	CHECK(p.get(Id::Osc1Octave) == 3.0f);
	p.set(Id::Osc1Octave, -1.4f);
	CHECK(p.get(Id::Osc1Octave) == -1.0f);
	p.set(Id::Osc1Pitch, 6.6f);
	CHECK(p.get(Id::Osc1Pitch) == 7.0f);
	p.set(Id::Osc1Shape, 0.333f);  // continuous: no snapping
	CHECK(p.get(Id::Osc1Shape) == doctest::Approx(0.333f));
}

TEST_CASE("normalized round-trips across the range")
{
	Parameters p;
	p.set_normalized(Id::Tempo, 0.0f);
	CHECK(p.get(Id::Tempo) == 30.0f);
	p.set_normalized(Id::Tempo, 1.0f);
	CHECK(p.get(Id::Tempo) == 300.0f);
	p.set(Id::Lfo1Amount, 0.5f);
	CHECK(p.normalized(Id::Lfo1Amount) == doctest::Approx(0.75f));
}

TEST_CASE("values format the way the UI shows them")
{
	Parameters p;
	p.set(Id::Osc1Pitch, 7.0f);
	CHECK(shown(p, Id::Osc1Pitch) == "+7");
	p.set(Id::Osc1Pitch, -3.0f);
	CHECK(shown(p, Id::Osc1Pitch) == "-3");
	p.set(Id::Osc1Pitch, 0.0f);
	CHECK(shown(p, Id::Osc1Pitch) == "0");

	p.set(Id::OscMix, 1.0f);
	CHECK(shown(p, Id::OscMix) == "127");
	p.set(Id::Lfo1Amount, 1.0f);
	CHECK(shown(p, Id::Lfo1Amount) == "+63");
	p.set(Id::Lfo1Amount, -1.0f);
	CHECK(shown(p, Id::Lfo1Amount) == "-64");

	p.set(Id::Lfo2Dest, static_cast<float>(params::LfoDest::Amp));
	CHECK(shown(p, Id::Lfo2Dest) == "AMP");
	CHECK(shown(p, Id::Tempo) == "120");
}

TEST_CASE("note names")
{
	char buf[5];
	ui::format_note(60, buf);
	CHECK(std::string(buf) == "C4");
	ui::format_note(61, buf);
	CHECK(std::string(buf) == "C#4");
	ui::format_note(69, buf);
	CHECK(std::string(buf) == "A4");
	ui::format_note(12, buf);
	CHECK(std::string(buf) == "C0");
}
