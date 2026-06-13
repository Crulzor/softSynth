// SPDX-License-Identifier: MIT
//
// Tiny 16-bit PCM WAV writer for the host-side auditioning tool. Host-only on
// purpose (uses <fstream>): it lives under test/, never under App/dsp/, so the
// pure DSP layer stays free of any file I/O.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace wav
{

// Write interleaved float samples in [-1, 1] as a 16-bit PCM WAV. `channels`
// describes how `samples` is interleaved (1 = mono, 2 = L,R,L,R,...). Returns
// false if the file could not be opened.
inline bool write(const std::string& path, const std::vector<float>& samples,
                  int channels, int sample_rate)
{
	std::ofstream f(path, std::ios::binary);
	if(!f)
	{
		return false;
	}

	const std::uint32_t data_bytes =
	    static_cast<std::uint32_t>(samples.size()) * sizeof(std::int16_t);
	const std::uint16_t bits = 16;
	const std::uint16_t block_align =
	    static_cast<std::uint16_t>(channels * bits / 8);
	const std::uint32_t byte_rate =
	    static_cast<std::uint32_t>(sample_rate) * block_align;

	auto put32 = [&f](std::uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
	auto put16 = [&f](std::uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };

	f.write("RIFF", 4);
	put32(36 + data_bytes);  // RIFF chunk size
	f.write("WAVE", 4);

	f.write("fmt ", 4);
	put32(16);                                        // fmt chunk size
	put16(1);                                         // PCM
	put16(static_cast<std::uint16_t>(channels));
	put32(static_cast<std::uint32_t>(sample_rate));
	put32(byte_rate);
	put16(block_align);
	put16(bits);

	f.write("data", 4);
	put32(data_bytes);
	for(float s : samples)
	{
		// Clamp then scale to full-scale 16-bit.
		const float c = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
		const auto v = static_cast<std::int16_t>(c * 32767.0f);
		put16(static_cast<std::uint16_t>(v));
	}
	return static_cast<bool>(f);
}

}  // namespace wav
