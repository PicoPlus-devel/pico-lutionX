// Aulib/WavSink.h - pico-lutionX extension of the SDL_audiolib subset: receives a WAV file
// in pieces (DecodeMp3ToAdpcmWav, EncodeWavToAdpcmWav).
#pragma once

#include <cstddef>

namespace Aulib {

// begin gets the length of the whole file first and may refuse it; write then gets exactly
// that many bytes.
class WavSink {
public:
	virtual ~WavSink() = default;
	virtual auto begin(size_t bytes) -> bool = 0;
	virtual void write(const void *data, size_t bytes) = 0;
};

} // namespace Aulib
