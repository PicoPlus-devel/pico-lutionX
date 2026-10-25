// adpcm_wav.h - writes IMA ADPCM WAV files (ima_adpcm.h). Sound effects, decoded from MP3
// (aulib_mp3.cpp) or read from PCM WAV files (aulib.cpp), are stored in the flash asset
// cache in this form: about a quarter of 16-bit PCM, half of 8-bit PCM.
#pragma once

#include <Aulib/WavSink.h>

#include <cstdint>
#include <cstring>
#include <memory>

#include "ima_adpcm.h"

namespace Aulib {

// Writes `frames` frames of `channels` channels at `rate` Hz to sink. read(pcm, max) stores
// up to max interleaved 16-bit frames in pcm and returns how many it stored; frames it
// does not deliver are silence. Returns false if the format does not fit or the sink
// refused the file; after a successful begin the whole file is always written.
template <typename Read>
auto WriteAdpcmWav(unsigned channels, unsigned rate, uint64_t frames, WavSink &sink, Read &&read) -> bool
{
	using namespace ImaAdpcm;

	if (frames == 0 || frames > UINT32_MAX / 2 || channels == 0 || channels > 2 || rate == 0)
		return false;
	const unsigned blockAlign = BlockBytesPerChannel * channels;
	const auto blocks = static_cast<uint32_t>((frames + SamplesPerBlock - 1) / SamplesPerBlock);
	const uint32_t dataBytes = blocks * blockAlign;
	constexpr size_t HeaderBytes = 60; // RIFF, fmt (20 bytes), fact, data
	if (!sink.begin(HeaderBytes + dataBytes))
		return false;

	uint8_t header[HeaderBytes];
	auto le32 = [&](size_t at, uint32_t v) {
		for (int i = 0; i < 4; i++)
			header[at + i] = static_cast<uint8_t>(v >> (8 * i));
	};
	auto le16 = [&](size_t at, unsigned v) {
		header[at] = static_cast<uint8_t>(v);
		header[at + 1] = static_cast<uint8_t>(v >> 8);
	};
	memcpy(&header[0], "RIFF", 4);
	le32(4, static_cast<uint32_t>(HeaderBytes - 8 + dataBytes));
	memcpy(&header[8], "WAVEfmt ", 8);
	le32(16, 20);
	le16(20, 0x11); // IMA ADPCM
	le16(22, channels);
	le32(24, rate);
	le32(28, static_cast<uint32_t>(static_cast<uint64_t>(rate) * blockAlign / SamplesPerBlock));
	le16(32, blockAlign);
	le16(34, 4);
	le16(36, 2); // cbSize
	le16(38, SamplesPerBlock);
	memcpy(&header[40], "fact", 4);
	le32(44, 4);
	le32(48, static_cast<uint32_t>(frames));
	memcpy(&header[52], "data", 4);
	le32(56, dataBytes);
	sink.write(header, sizeof(header));

	// one block at a time; the last block is padded with silence (the fact chunk holds the
	// real length)
	auto pcm = std::make_unique<int16_t[]>(SamplesPerBlock * 2);
	auto block = std::make_unique<uint8_t[]>(BlockBytesPerChannel * 2);
	Channel state[2];
	uint64_t left = frames;
	for (uint32_t b = 0; b < blocks; b++) {
		const auto want = static_cast<unsigned>(left < SamplesPerBlock ? left : SamplesPerBlock);
		unsigned got = read(pcm.get(), want);
		if (got > want)
			got = want;
		memset(pcm.get() + got * channels, 0, (SamplesPerBlock - got) * channels * sizeof(int16_t));
		left -= want;

		memset(block.get(), 0, blockAlign);
		for (unsigned c = 0; c < channels; c++) {
			state[c].predictor = pcm[c];
			block[4 * c] = static_cast<uint8_t>(pcm[c]);
			block[4 * c + 1] = static_cast<uint8_t>(static_cast<uint16_t>(pcm[c]) >> 8);
			block[4 * c + 2] = static_cast<uint8_t>(state[c].index);
		}
		for (unsigned k = 1; k < SamplesPerBlock; k++)
			for (unsigned c = 0; c < channels; c++)
				block[NibbleByte(k, c, channels)] |= static_cast<uint8_t>(Encode(state[c], pcm[k * channels + c]) << NibbleShift(k));
		sink.write(block.get(), blockAlign);
	}
	return true;
}

} // namespace Aulib
