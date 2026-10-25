// Aulib/DecoderDrwav.h - WAV decoder of the pico-lutionX SDL_audiolib subset: PCM (8 and
// 16-bit) and IMA ADPCM (format 0x11, in which src/aulib/aulib_mp3.cpp stores decoded MP3
// sound effects).
#pragma once

#include <Aulib/Decoder.h>
#include <Aulib/WavSink.h>

#include <cstdint>

namespace Aulib {

class DecoderDrwav : public Decoder {
public:
	DecoderDrwav() = default;
	~DecoderDrwav() override = default;
	auto open(SDL_RWops *rwops) -> bool override;
	auto getChannels() const -> int override { return channels_; }
	auto getRate() const -> int override { return rate_; }
	auto rewind() -> bool override;
	auto duration() const -> std::chrono::microseconds override;
	auto seekToTime(std::chrono::microseconds pos) -> bool override;

protected:
	auto doDecoding(float buf[], int len, bool &callAgain) -> int override;

private:
	auto decodeAdpcm(float buf[], int len) -> int;

	SDL_RWops *rwops_ = nullptr;
	int channels_ = 0;
	int rate_ = 0;
	int bytesPerSample_ = 0; // PCM: 1 (unsigned 8-bit) or 2 (signed 16-bit); 0: IMA ADPCM
	uint32_t dataStart_ = 0;
	uint32_t dataSize_ = 0;
	uint32_t position_ = 0; // PCM: bytes of the data chunk consumed; ADPCM: frames decoded
	// IMA ADPCM: the block layout and the state of each channel at position_
	uint32_t blockAlign_ = 0;
	uint32_t samplesPerBlock_ = 0;
	uint32_t frames_ = 0;
	int predictor_[2] = {};
	int stepIndex_[2] = {};
};

// pico-lutionX: converts a PCM WAV file (8 or 16-bit, mono or stereo) into an IMA ADPCM WAV
// file, so the full game's sound effects (8-bit PCM) take half the flash. Returns false if the
// data is not such a file or the sink refused it; after a successful begin the whole file is
// always written.
auto EncodeWavToAdpcmWav(const uint8_t *wav, size_t size, WavSink &sink) -> bool;

} // namespace Aulib
