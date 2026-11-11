// SPDX-License-Identifier: MIT
//
// Pure USB-MIDI 1.0 cable-packet parser.
//
// Fed from the USBH bulk-IN data hook (foreground, the single writer). It
// produces two things:
//   - `events`, a lock-free FIFO of note-ons / note-offs that the audio
//     interrupt drains at the top of each block into the 6-voice engine
//     (polyphonic: every key is its own event);
//   - `note_state`, the latest note as one packed atomic word, for the MIDI
//     page's read-out (latest-wins, display only).
// No HAL: it compiles under the host build for test/, exactly like the rest
// of the pure DSP core.
//
// A USB-MIDI 1.0 event packet is four bytes off the wire:
//   b0  high nibble = virtual cable number (0..15),
//       low nibble  = Code Index Number (CIN): what the packet carries
//       (0x8 note-off, 0x9 note-on, 0xB control change, 0x4..0x7 sysex,
//       0xF single byte / real-time, ...)
//   b1  MIDI status byte (for channel-voice CINs) or data
//   b2  data1
//   b3  data2
//
// Policy: NOTE_ON / NOTE_OFF (CIN 0x9 / 0x8, on any cable) are queued and
// update the note state; a vel-0 NOTE_ON is a NOTE_OFF. Everything else - CC, program change,
// aftertouch, pitch bend, sysex (whose data bytes never decode as notes
// because only the CIN decides) and system real-time such as MIDI clock - is
// counted but ignored.

#pragma once

#include <atomic>
#include <cstdint>

namespace midi
{

// One 4-byte USB-MIDI event packet as it comes off the wire.
struct Packet
{
	std::uint8_t b0;
	std::uint8_t b1;
	std::uint8_t b2;
	std::uint8_t b3;
};

// One note event as it crosses from the USB foreground to the audio ISR.
struct NoteEvent
{
	enum Kind : std::uint8_t
	{
		Off = 0,
		On = 1,
		AllOff = 2,  // release everything (device unplugged)
	};
	std::uint8_t kind;
	std::uint8_t note;
	std::uint8_t velocity;
	std::uint8_t channel;
};

// Single-producer / single-consumer ring of NoteEvents, one word each. The
// foreground pushes, the audio interrupt pops; head and tail each have one
// writer, so no locks. A full queue drops the new event (64 deep: far more
// than a block's worth of keys).
class EventQueue
{
public:
	static constexpr std::uint32_t kSize = 64;  // power of two

	bool push(NoteEvent e)
	{
		const std::uint32_t h = head_.load(std::memory_order_relaxed);
		if(h - tail_.load(std::memory_order_acquire) >= kSize)
		{
			return false;
		}
		buf_[h & (kSize - 1)] = pack(e);
		head_.store(h + 1, std::memory_order_release);
		return true;
	}

	bool pop(NoteEvent& e)
	{
		const std::uint32_t t = tail_.load(std::memory_order_relaxed);
		if(t == head_.load(std::memory_order_acquire))
		{
			return false;
		}
		e = unpack(buf_[t & (kSize - 1)]);
		tail_.store(t + 1, std::memory_order_release);
		return true;
	}

private:
	static std::uint32_t pack(NoteEvent e)
	{
		return std::uint32_t(e.kind) | (std::uint32_t(e.note) << 8) |
		       (std::uint32_t(e.velocity) << 16) |
		       (std::uint32_t(e.channel) << 24);
	}
	static NoteEvent unpack(std::uint32_t w)
	{
		return NoteEvent{static_cast<std::uint8_t>(w & 0xFF),
		                 static_cast<std::uint8_t>((w >> 8) & 0xFF),
		                 static_cast<std::uint8_t>((w >> 16) & 0xFF),
		                 static_cast<std::uint8_t>(w >> 24)};
	}

	std::atomic<std::uint32_t> head_{0};
	std::atomic<std::uint32_t> tail_{0};
	std::uint32_t buf_[kSize] = {};
};

// Packed note state, held in one std::atomic<uint32_t>:
//   bits  0..6   note number         0..127
//   bits  7..13  velocity            0..127
//   bits 14..17  MIDI channel       0..15
//   bit  18     active (1 = note held)
//   bits 19..31 reserved (0)
class Parser
{
public:
	std::atomic<std::uint32_t> note_state{0};
	std::atomic<std::uint32_t> packets{0};  // complete 4-byte packets parsed
	// The last packet seen, b0 in the top byte down to b3 in the bottom one;
	// a raw read-out for USB bring-up (the MIDI page shows it as hex).
	std::atomic<std::uint32_t> last_packet{0};
	// Note-ons / note-offs for the engine (drained by the audio ISR).
	EventQueue events;

	// Parse one bulk-IN chunk: every complete 4-byte packet in the first
	// `len` bytes of `buf` is fed to on_packet(Packet).
	void on_packet(const std::uint8_t* buf, int len);

	// Parse a single 4-byte cable packet; queues note events and updates
	// note_state.
	void on_packet(Packet p);

	// Queue a release of every MIDI-held note and clear note_state (the
	// device went away with keys down).
	void all_notes_off();

	// Build / read a packed note state.
	static std::uint32_t pack(std::uint8_t note, std::uint8_t vel,
	                         std::uint8_t ch, bool active)
	{
		return (std::uint32_t(note & 0x7F))
		     | (std::uint32_t(vel & 0x7F) << 7)
		     | (std::uint32_t(ch & 0x0F) << 14)
		     | (std::uint32_t(active ? 1u : 0u) << 18);
	}
	static std::uint8_t note(std::uint32_t s)
	{
		return s & 0x7F;
	}
	static std::uint8_t velocity(std::uint32_t s)
	{
		return (s >> 7) & 0x7F;
	}
	static std::uint8_t channel(std::uint32_t s)
	{
		return (s >> 14) & 0x0F;
	}
	static bool active(std::uint32_t s)
	{
		return (s & (1u << 18)) != 0;
	}
};

// The single instance shared by the USBH hook (writer) and the audio
// interrupt + MIDI page (readers). A function-local static: no heap.
Parser& parser();

}  // namespace midi
