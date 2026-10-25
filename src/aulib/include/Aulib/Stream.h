// Aulib/Stream.h - a playing sound of the pico-lutionX SDL_audiolib subset (see aulib.h).
#pragma once

#include <SDL.h>
#include <aulib.h>

#include <chrono>
#include <functional>
#include <memory>

namespace Aulib {

class Decoder;
class Resampler;

class Stream {
public:
	using Callback = std::function<void(Stream &)>;

	explicit Stream(SDL_RWops *rwops, std::unique_ptr<Decoder> decoder, std::unique_ptr<Resampler> resampler, bool closeRw);
	explicit Stream(SDL_RWops *rwops, std::unique_ptr<Decoder> decoder, bool closeRw);
	virtual ~Stream();
	Stream(const Stream &) = delete;
	auto operator=(const Stream &) -> Stream & = delete;

	virtual auto open() -> bool;
	// iterations: how often to play; 0 loops until stopped.
	virtual auto play(int iterations = 1, std::chrono::microseconds fadeTime = {}) -> bool;
	virtual void stop(std::chrono::microseconds fadeTime = {});
	virtual void pause(std::chrono::microseconds fadeTime = {});
	virtual void resume(std::chrono::microseconds fadeTime = {});
	virtual auto rewind() -> bool;
	virtual void setVolume(float volume);
	virtual auto volume() const -> float;
	// -1 (left) .. 1 (right)
	virtual void setStereoPosition(float position);
	virtual auto getStereoPosition() const -> float;
	virtual void mute();
	virtual void unmute();
	virtual auto isMuted() const -> bool;
	virtual auto isPlaying() const -> bool;
	virtual auto isPaused() const -> bool;
	virtual auto duration() const -> std::chrono::microseconds;
	virtual auto seekToTime(std::chrono::microseconds pos) -> bool;

	void setFinishCallback(Callback func);
	void unsetFinishCallback();
	void setLoopCallback(Callback func);
	void unsetLoopCallback();

	// For the mixer (src/aulib/aulib.cpp): adds up to `frames` stereo frames at the output
	// rate to out; returns false when the sound ended. endOfStream then stops the stream
	// and runs the finish callback, which may delete the stream.
	auto mixInto(float *out, int frames) -> bool;
	void endOfStream();

private:
	void finishPlaying();

	SDL_RWops *rwops_;
	std::unique_ptr<Decoder> decoder_;
	bool closeRw_;
	bool open_ = false;
	bool playing_ = false;
	bool paused_ = false;
	bool muted_ = false;
	int iterations_ = 0; // left to play; 0 = loop
	float volume_ = 1.0F;
	float position_ = 0.0F;
	Callback finishCallback_;
	Callback loopCallback_;
	// resampling state: source frames are read in chunks and interpolated
	float step_ = 1.0F;   // source frames per output frame
	float phase_ = 0.0F;  // position between prev_ and the next source frame
	float prev_[2] = {};
	float next_[2] = {};
	bool primed_ = false;
	bool ended_ = false;
	float chunk_[64 * 2]; // decoded source frames (every loaded sound has a stream)
	int chunkFrames_ = 0;
	int chunkPos_ = 0;
};

} // namespace Aulib

#include <aulib_mix.h>
