// Aulib/DecoderDrmp3.h - MP3 decoder of the pico-lutionX SDL_audiolib subset (dr_mp3).
// DevilutionX's spawn.mpq stores its sounds and music as MP3.
#pragma once

#include <Aulib/Decoder.h>

#include <cstdint>
#include <memory>

namespace Aulib {

class DecoderDrmp3 : public Decoder {
public:
	DecoderDrmp3();
	~DecoderDrmp3() override;
	auto open(SDL_RWops *rwops) -> bool override;
	auto getChannels() const -> int override;
	auto getRate() const -> int override;
	auto rewind() -> bool override;
	auto duration() const -> std::chrono::microseconds override;
	auto seekToTime(std::chrono::microseconds pos) -> bool override;
	void release() override;

protected:
	auto doDecoding(float buf[], int len, bool &callAgain) -> int override;

private:
	// The decoder state (about 32 KB) exists only while the sound plays: every loaded
	// sound effect has a decoder, but few play at once.
	auto ensureDecoder() -> bool;

	struct Impl;
	std::unique_ptr<Impl> impl_;
	SDL_RWops *rwops_ = nullptr;
	int channels_ = 0;
	int rate_ = 0;
	mutable uint64_t frames_ = 0; // total frames, counted on first use
};

// pico-lutionX: receives a WAV file in pieces. begin gets the length of the whole file
// first and may refuse it; write then gets exactly that many bytes.
class WavSink {
public:
	virtual ~WavSink() = default;
	virtual auto begin(size_t bytes) -> bool = 0;
	virtual void write(const void *data, size_t bytes) = 0;
};

// pico-lutionX: decodes an MP3 file into an IMA ADPCM WAV file (4 bits per sample, about
// a quarter of 16-bit PCM), so a sound effect can be decoded once instead of every time it
// plays. Returns false if the data is not MP3 or the sink refused it; after a successful
// begin the whole file is always written.
auto DecodeMp3ToAdpcmWav(const uint8_t *mp3, size_t size, WavSink &sink) -> bool;

} // namespace Aulib
