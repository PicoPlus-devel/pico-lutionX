// ima_adpcm.h - IMA ADPCM, 4 bits per sample, as in WAV format 0x11 (Microsoft/IMA). The
// pico-lutionX SDL_audiolib subset stores decoded MP3 sound effects in the flash asset
// cache in this format (aulib_mp3.cpp encodes, DecoderDrwav in aulib.cpp decodes).
//
// Block layout, blockAlign = 256 bytes per channel here: per channel a 4-byte header
// (first sample, little-endian 16-bit; step index; 0), then groups of 4 bytes per channel
// in channel order, each holding 8 samples of that channel, low nibble first.
#pragma once

#include <cstdint>

namespace Aulib::ImaAdpcm {

constexpr unsigned BlockBytesPerChannel = 256;
// samples per channel in a block: the header sample plus two per data byte
constexpr unsigned SamplesPerBlock = (BlockBytesPerChannel - 4) * 2 + 1;

inline constexpr int16_t StepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
	73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
	449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
	9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};

inline constexpr int8_t IndexTable[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

struct Channel {
	int predictor = 0;
	int index = 0;
};

inline int16_t Decode(Channel &ch, unsigned nibble)
{
	const int step = StepTable[ch.index];
	int diff = step >> 3;
	if (nibble & 1)
		diff += step >> 2;
	if (nibble & 2)
		diff += step >> 1;
	if (nibble & 4)
		diff += step;
	ch.predictor += (nibble & 8) ? -diff : diff;
	ch.predictor = ch.predictor < -32768 ? -32768 : ch.predictor > 32767 ? 32767 : ch.predictor;
	ch.index += IndexTable[nibble & 15];
	ch.index = ch.index < 0 ? 0 : ch.index > 88 ? 88 : ch.index;
	return static_cast<int16_t>(ch.predictor);
}

// Returns the nibble for sample and advances ch exactly as Decode will.
inline unsigned Encode(Channel &ch, int sample)
{
	int delta = sample - ch.predictor;
	unsigned nibble = 0;
	if (delta < 0) {
		nibble = 8;
		delta = -delta;
	}
	int step = StepTable[ch.index];
	if (delta >= step) {
		nibble |= 4;
		delta -= step;
	}
	step >>= 1;
	if (delta >= step) {
		nibble |= 2;
		delta -= step;
	}
	step >>= 1;
	if (delta >= step)
		nibble |= 1;
	Decode(ch, nibble);
	return nibble;
}

// Where sample k (k >= 1, the header holds sample 0) of channel c lies in a block.
inline unsigned NibbleByte(unsigned k, unsigned c, unsigned channels)
{
	const unsigned i = k - 1;
	return 4 * channels + (i / 8) * 4 * channels + c * 4 + (i % 8) / 2;
}

inline unsigned NibbleShift(unsigned k)
{
	return ((k - 1) & 1) * 4;
}

} // namespace Aulib::ImaAdpcm
