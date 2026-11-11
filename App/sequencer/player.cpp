// SPDX-License-Identifier: MIT
#include "sequencer/player.hpp"

namespace sequencer
{

int Player::release(NoteEvent* out)
{
	const int n = held_count_;
	for(int i = 0; i < n; ++i)
	{
		out[i] = NoteEvent{false, held_[i], 0};
	}
	held_count_ = 0;
	return n;
}

int Player::stop(NoteEvent* out)
{
	step_ = -1;
	return release(out);
}

int Player::on_event(const Pattern& pattern, const Sequencer::Event& e,
                     NoteEvent* out)
{
	int n = 0;
	if(e.step >= 0)
	{
		step_ = e.step;
		const Step& s = pattern.steps[e.step];
		if(s.tie && held_count_ > 0)
		{
			return 0;  // hold on through this step
		}
		n += release(out);
		if(s.on && !s.tie)
		{
			std::uint8_t notes[kMaxChordNotes];
			const int count = chord_notes(s.note, s.chord, notes);
			for(int i = 0; i < count; ++i)
			{
				out[n++] = NoteEvent{true, notes[i], kVelocity};
				held_[held_count_++] = notes[i];
			}
		}
		return n;
	}
	if(e.gate_off >= 0 && step_ >= 0)
	{
		const Step& next = pattern.steps[(step_ + 1) % kSteps];
		if(!next.tie)
		{
			n += release(out);
		}
	}
	return n;
}

}  // namespace sequencer
