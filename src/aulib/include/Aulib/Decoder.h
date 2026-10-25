// Aulib/Decoder.h - decoder interface of the pico-lutionX SDL_audiolib subset (see aulib.h).
#pragma once

#include <SDL.h>

#include <chrono>

namespace Aulib {

class Decoder {
public:
	Decoder() = default;
	virtual ~Decoder() = default;
	Decoder(const Decoder &) = delete;
	auto operator=(const Decoder &) -> Decoder & = delete;

	auto isOpen() const -> bool { return isOpen_; }
	// Decodes up to len interleaved samples as floats in -1..1; returns how many.
	auto decode(float buf[], int len, bool &callAgain) -> int { return doDecoding(buf, len, callAgain); }

	virtual auto open(SDL_RWops *rwops) -> bool = 0;
	virtual auto getChannels() const -> int = 0;
	virtual auto getRate() const -> int = 0;
	virtual auto rewind() -> bool = 0;
	virtual auto duration() const -> std::chrono::microseconds = 0;
	virtual auto seekToTime(std::chrono::microseconds pos) -> bool = 0;
	// pico-lutionX extension: the stream stopped; free working memory until the next
	// decode (the next decode starts from the beginning).
	virtual void release() {}

protected:
	void setIsOpen(bool f) { isOpen_ = f; }
	virtual auto doDecoding(float buf[], int len, bool &callAgain) -> int = 0;

private:
	bool isOpen_ = false;
};

} // namespace Aulib
