// aulib.cpp - the pico-lutionX SDL_audiolib subset (see include/aulib.h): output format,
// streams, a WAV decoder (PCM and IMA ADPCM) and the mixer.
//
// Single-threaded: the platform calls aulib_mix() from the thread that runs the engine
// (on the board from SDL_PollEvent and friends, never from an interrupt or core1), so the
// streams need no locking and decoders may read from the SD card.

#include <aulib.h>

#include <Aulib/Decoder.h>
#include <Aulib/DecoderDrwav.h>
#include <Aulib/Resampler.h>
#include <Aulib/Stream.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "ima_adpcm.h"

namespace Aulib {

namespace {

int OutRate = 22050;
int OutChannels = 2;
int OutFrameSize = 512;
AudioFormat OutFormat = 0x8010; // AUDIO_S16LSB

} // namespace

// ---- Output format ------------------------------------------------------------------------

auto init(int freq, AudioFormat format, int channels, int frameSize, const std::string &) -> bool
{
	OutRate = freq > 0 ? freq : 22050;
	OutFormat = format;
	OutChannels = channels == 1 ? 1 : 2;
	OutFrameSize = frameSize;
	return true;
}

void quit()
{
}

auto sampleFormat() noexcept -> AudioFormat { return OutFormat; }
auto sampleRate() noexcept -> int { return OutRate; }
auto channelCount() noexcept -> int { return OutChannels; }
auto frameSize() noexcept -> int { return OutFrameSize; }

// ---- Mixer --------------------------------------------------------------------------------

struct Mixer {
	// streams that are playing; Stream::play adds, stop/end/destruction remove
	static constexpr int MaxStreams = 32;
	Stream *active[MaxStreams] = {};
	int count = 0;

	void add(Stream *s)
	{
		for (int i = 0; i < count; i++)
			if (active[i] == s)
				return;
		if (count < MaxStreams)
			active[count++] = s;
	}

	void remove(Stream *s)
	{
		for (int i = 0; i < count; i++) {
			if (active[i] == s) {
				active[i] = active[--count];
				return;
			}
		}
	}
};

namespace {
Mixer TheMixer;
}

// ---- Stream -------------------------------------------------------------------------------

Stream::Stream(SDL_RWops *rwops, std::unique_ptr<Decoder> decoder, std::unique_ptr<Resampler>, bool closeRw)
    : rwops_(rwops)
    , decoder_(std::move(decoder))
    , closeRw_(closeRw)
{
}

Stream::Stream(SDL_RWops *rwops, std::unique_ptr<Decoder> decoder, bool closeRw)
    : Stream(rwops, std::move(decoder), nullptr, closeRw)
{
}

Stream::~Stream()
{
	TheMixer.remove(this);
	decoder_ = nullptr;
	if (closeRw_ && rwops_ != nullptr)
		SDL_RWclose(rwops_);
}

auto Stream::open() -> bool
{
	if (open_)
		return true;
	if (decoder_ == nullptr)
		return false;
	if (!decoder_->isOpen() && !decoder_->open(rwops_))
		return false;
	if (decoder_->getChannels() < 1 || decoder_->getChannels() > 2 || decoder_->getRate() <= 0)
		return false;
	step_ = static_cast<float>(decoder_->getRate()) / static_cast<float>(OutRate);
	open_ = true;
	return true;
}

auto Stream::play(int iterations, std::chrono::microseconds) -> bool
{
	if (!open())
		return false;
	if (playing_)
		rewind();
	iterations_ = iterations;
	playing_ = true;
	paused_ = false;
	ended_ = false;
	TheMixer.add(this);
	return true;
}

void Stream::stop(std::chrono::microseconds)
{
	if (!playing_)
		return;
	playing_ = false;
	paused_ = false;
	TheMixer.remove(this);
	rewind();
	decoder_->release();
}

void Stream::pause(std::chrono::microseconds)
{
	if (playing_)
		paused_ = true;
}

void Stream::resume(std::chrono::microseconds)
{
	paused_ = false;
}

auto Stream::rewind() -> bool
{
	chunkFrames_ = chunkPos_ = 0;
	primed_ = false;
	phase_ = 0.0F;
	ended_ = false;
	return decoder_ != nullptr && decoder_->rewind();
}

void Stream::setVolume(float volume) { volume_ = volume < 0.0F ? 0.0F : volume; }
auto Stream::volume() const -> float { return volume_; }
void Stream::setStereoPosition(float position) { position_ = std::clamp(position, -1.0F, 1.0F); }
auto Stream::getStereoPosition() const -> float { return position_; }
void Stream::mute() { muted_ = true; }
void Stream::unmute() { muted_ = false; }
auto Stream::isMuted() const -> bool { return muted_; }
auto Stream::isPlaying() const -> bool { return playing_; }
auto Stream::isPaused() const -> bool { return paused_; }

auto Stream::duration() const -> std::chrono::microseconds
{
	return decoder_ != nullptr ? decoder_->duration() : std::chrono::microseconds {};
}

auto Stream::seekToTime(std::chrono::microseconds pos) -> bool
{
	chunkFrames_ = chunkPos_ = 0;
	primed_ = false;
	return decoder_ != nullptr && decoder_->seekToTime(pos);
}

void Stream::setFinishCallback(Callback func) { finishCallback_ = std::move(func); }
void Stream::unsetFinishCallback() { finishCallback_ = nullptr; }
void Stream::setLoopCallback(Callback func) { loopCallback_ = std::move(func); }
void Stream::unsetLoopCallback() { loopCallback_ = nullptr; }

void Stream::finishPlaying()
{
	playing_ = false;
	paused_ = false;
	ended_ = false;
	rewind();
	decoder_->release();
}

void Stream::endOfStream()
{
	TheMixer.remove(this);
	finishPlaying();
	if (finishCallback_) {
		Callback callback = finishCallback_; // the callback may delete this stream
		callback(*this);
	}
}

auto Stream::mixInto(float *out, int frames) -> bool
{
	const int channels = decoder_->getChannels();

	// Reads one source frame into f; false at the end of the sound.
	auto readFrame = [&](float *f) -> bool {
		while (chunkPos_ >= chunkFrames_) {
			if (ended_)
				return false;
			bool callAgain = false;
			const int samples = decoder_->decode(chunk_, static_cast<int>(sizeof(chunk_) / sizeof(chunk_[0])) / channels * channels, callAgain);
			chunkFrames_ = samples / channels;
			chunkPos_ = 0;
			if (chunkFrames_ > 0)
				break;
			// end of the data: loop or stop
			if (iterations_ == 1) {
				ended_ = true;
				return false;
			}
			if (iterations_ > 1)
				iterations_--;
			if (!decoder_->rewind()) {
				ended_ = true;
				return false;
			}
			if (loopCallback_)
				loopCallback_(*this);
		}
		const float *src = &chunk_[chunkPos_ * channels];
		f[0] = src[0];
		f[1] = channels == 2 ? src[1] : src[0];
		chunkPos_++;
		return true;
	};

	const float left = muted_ ? 0.0F : volume_ * (position_ > 0.0F ? 1.0F - position_ : 1.0F);
	const float right = muted_ ? 0.0F : volume_ * (position_ < 0.0F ? 1.0F + position_ : 1.0F);

	if (!primed_) {
		if (!readFrame(prev_))
			return false;
		if (!readFrame(next_)) {
			next_[0] = prev_[0];
			next_[1] = prev_[1];
		}
		phase_ = 0.0F;
		primed_ = true;
	}
	for (int i = 0; i < frames; i++) {
		const float l = prev_[0] + (next_[0] - prev_[0]) * phase_;
		const float r = prev_[1] + (next_[1] - prev_[1]) * phase_;
		out[2 * i] += l * left;
		out[2 * i + 1] += r * right;
		phase_ += step_;
		while (phase_ >= 1.0F) {
			phase_ -= 1.0F;
			prev_[0] = next_[0];
			prev_[1] = next_[1];
			if (!readFrame(next_))
				return false; // the last source frame has been played
		}
	}
	return true;
}

// ---- WAV decoder: PCM and IMA ADPCM ---------------------------------------------------------

namespace {

uint32_t ReadLE32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t ReadLE16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

} // namespace

auto DecoderDrwav::open(SDL_RWops *rwops) -> bool
{
	uint8_t header[12];
	if (rwops == nullptr || SDL_RWseek(rwops, 0, RW_SEEK_SET) < 0 || SDL_RWread(rwops, header, 12, 1) != 1)
		return false;
	if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
		return false;
	bool haveFormat = false;
	uint32_t pos = 12;
	frames_ = 0;
	for (;;) {
		uint8_t chunk[8];
		if (SDL_RWseek(rwops, static_cast<int>(pos), RW_SEEK_SET) < 0 || SDL_RWread(rwops, chunk, 8, 1) != 1)
			return false;
		const uint32_t size = ReadLE32(chunk + 4);
		if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
			uint8_t fmt[16];
			if (SDL_RWread(rwops, fmt, 16, 1) != 1)
				return false;
			const uint16_t format = ReadLE16(fmt);
			channels_ = ReadLE16(fmt + 2);
			rate_ = static_cast<int>(ReadLE32(fmt + 4));
			const uint16_t bits = ReadLE16(fmt + 14);
			if (channels_ < 1 || channels_ > 2)
				return false;
			if (format == 0x11 && bits == 4) {
				// IMA ADPCM: whole groups of 4 bytes per channel after the block headers
				blockAlign_ = ReadLE16(fmt + 12);
				const uint32_t header = 4 * static_cast<uint32_t>(channels_);
				if (blockAlign_ <= header || blockAlign_ > 1024 || blockAlign_ % header != 0)
					return false;
				samplesPerBlock_ = (blockAlign_ - header) * 2 / static_cast<uint32_t>(channels_) + 1;
				bytesPerSample_ = 0;
			} else if ((format == 1 || format == 0xfffe) && (bits == 8 || bits == 16)) {
				bytesPerSample_ = bits / 8;
			} else {
				return false;
			}
			haveFormat = true;
		} else if (memcmp(chunk, "fact", 4) == 0 && size >= 4) {
			uint8_t fact[4];
			if (SDL_RWread(rwops, fact, 4, 1) != 1)
				return false;
			frames_ = ReadLE32(fact);
		} else if (memcmp(chunk, "data", 4) == 0) {
			if (!haveFormat)
				return false;
			dataStart_ = pos + 8;
			dataSize_ = size;
			break;
		}
		pos += 8 + size + (size & 1);
	}
	if (bytesPerSample_ == 0) {
		const uint32_t whole = dataSize_ / blockAlign_ * samplesPerBlock_;
		if (frames_ == 0 || frames_ > whole)
			frames_ = whole; // no fact chunk: every block counts as full
	}
	rwops_ = rwops;
	position_ = 0;
	SDL_RWseek(rwops, static_cast<int>(dataStart_), RW_SEEK_SET);
	setIsOpen(true);
	return true;
}

auto DecoderDrwav::rewind() -> bool
{
	if (rwops_ == nullptr)
		return false;
	position_ = 0;
	return SDL_RWseek(rwops_, static_cast<int>(dataStart_), RW_SEEK_SET) >= 0;
}

auto DecoderDrwav::duration() const -> std::chrono::microseconds
{
	if (rate_ == 0 || channels_ == 0)
		return {};
	const uint64_t frames = bytesPerSample_ == 0 ? frames_ : dataSize_ / static_cast<uint32_t>(channels_ * bytesPerSample_);
	return std::chrono::microseconds { frames * 1000000 / static_cast<uint64_t>(rate_) };
}

auto DecoderDrwav::seekToTime(std::chrono::microseconds pos) -> bool
{
	if (rwops_ == nullptr)
		return false;
	if (bytesPerSample_ == 0) {
		// to the start of the block that holds the position (a block restarts the decoder)
		const uint64_t frame = static_cast<uint64_t>(pos.count()) * static_cast<uint64_t>(rate_) / 1000000;
		position_ = static_cast<uint32_t>(frame < frames_ ? frame : frames_);
		position_ -= position_ % samplesPerBlock_;
		return true;
	}
	const uint32_t frameBytes = static_cast<uint32_t>(channels_ * bytesPerSample_);
	uint64_t frame = static_cast<uint64_t>(pos.count()) * static_cast<uint64_t>(rate_) / 1000000;
	uint64_t offset = frame * frameBytes;
	if (offset > dataSize_)
		offset = dataSize_ - dataSize_ % frameBytes;
	position_ = static_cast<uint32_t>(offset);
	return SDL_RWseek(rwops_, static_cast<int>(dataStart_ + position_), RW_SEEK_SET) >= 0;
}

auto DecoderDrwav::doDecoding(float buf[], int len, bool &callAgain) -> int
{
	callAgain = false;
	if (rwops_ == nullptr)
		return 0;
	if (bytesPerSample_ == 0)
		return decodeAdpcm(buf, len);
	uint8_t raw[512];
	int done = 0;
	while (done < len && position_ < dataSize_) {
		uint32_t want = static_cast<uint32_t>(len - done) * static_cast<uint32_t>(bytesPerSample_);
		want = std::min<uint32_t>({ want, static_cast<uint32_t>(sizeof(raw)), dataSize_ - position_ });
		want -= want % static_cast<uint32_t>(bytesPerSample_);
		if (want == 0)
			break;
		const int got = SDL_RWread(rwops_, raw, 1, static_cast<int>(want));
		if (got <= 0)
			break;
		const int samples = got / bytesPerSample_;
		if (bytesPerSample_ == 2) {
			for (int i = 0; i < samples; i++)
				buf[done + i] = static_cast<int16_t>(ReadLE16(raw + 2 * i)) * (1.0F / 32768.0F);
		} else {
			for (int i = 0; i < samples; i++)
				buf[done + i] = (static_cast<int>(raw[i]) - 128) * (1.0F / 128.0F);
		}
		done += samples;
		position_ += static_cast<uint32_t>(samples * bytesPerSample_);
	}
	return done;
}

// Decodes from position_ (a frame number). The block that holds it is read again on every
// call, so a decoder keeps no buffer between calls: every loaded sound effect has one.
auto DecoderDrwav::decodeAdpcm(float buf[], int len) -> int
{
	using namespace ImaAdpcm;

	const auto channels = static_cast<unsigned>(channels_);
	uint8_t block[1024];
	uint32_t loaded = UINT32_MAX;
	int done = 0;
	while (done + channels_ <= len && position_ < frames_) {
		const uint32_t b = position_ / samplesPerBlock_;
		const uint32_t k = position_ % samplesPerBlock_;
		if (b != loaded) {
			const uint32_t offset = b * blockAlign_;
			const uint32_t n = std::min(blockAlign_, dataSize_ - std::min(offset, dataSize_));
			if (SDL_RWseek(rwops_, static_cast<int>(dataStart_ + offset), RW_SEEK_SET) < 0
			    || (n != 0 && SDL_RWread(rwops_, block, static_cast<int>(n), 1) != 1))
				break;
			memset(block + n, 0, blockAlign_ - n);
			loaded = b;
		}
		for (unsigned c = 0; c < channels; c++) {
			Channel ch { predictor_[c], stepIndex_[c] };
			int16_t sample;
			if (k == 0) {
				sample = static_cast<int16_t>(ReadLE16(block + 4 * c));
				ch.predictor = sample;
				ch.index = std::min<int>(block[4 * c + 2], 88);
			} else {
				sample = Decode(ch, (block[NibbleByte(k, c, channels)] >> NibbleShift(k)) & 15);
			}
			predictor_[c] = ch.predictor;
			stepIndex_[c] = ch.index;
			buf[done++] = sample * (1.0F / 32768.0F);
		}
		position_++;
	}
	return done;
}

} // namespace Aulib

// ---- Output -------------------------------------------------------------------------------

extern "C" void aulib_mix(int16_t *stereo, int frames)
{
	using Aulib::Stream;
	constexpr int Chunk = 128;
	float acc[Chunk * 2];

	while (frames > 0) {
		const int n = frames < Chunk ? frames : Chunk;
		std::fill(acc, acc + 2 * n, 0.0F);

		// The finish callbacks run after mixing: they may destroy streams or start others.
		Stream *ended[Aulib::Mixer::MaxStreams];
		int numEnded = 0;
		Stream *playing[Aulib::Mixer::MaxStreams];
		const int numPlaying = Aulib::TheMixer.count;
		std::copy(Aulib::TheMixer.active, Aulib::TheMixer.active + numPlaying, playing);
		for (int i = 0; i < numPlaying; i++) {
			Stream *s = playing[i];
			if (s->isPaused())
				continue;
			if (!s->mixInto(acc, n))
				ended[numEnded++] = s;
		}
		for (int i = 0; i < numEnded; i++)
			ended[i]->endOfStream(); // may delete the stream

		for (int i = 0; i < 2 * n; i++) {
			float v = acc[i] * 32767.0F;
			v = v > 32767.0F ? 32767.0F : (v < -32768.0F ? -32768.0F : v);
			stereo[i] = static_cast<int16_t>(v);
		}
		stereo += 2 * n;
		frames -= n;
	}
}
