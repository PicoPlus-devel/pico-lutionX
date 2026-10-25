// aulib_mp3.cpp - MP3 decoding for the pico-lutionX SDL_audiolib subset, with dr_mp3
// (3rdparty/dx-deps/dr_libs, public domain / MIT-0).

#include <Aulib/DecoderDrmp3.h>

#include <cstring>

#include "adpcm_wav.h"
#include "dx_sram.h"

#define DRMP3_DATA_CHUNK_SIZE 16384 // the minimum (default 64 KB per decoder)
// drmp3dec_decode_frame's 17 KB of scratch space would not fit on core0's 16 KB stack: it
// is static instead (decoding only runs on core0, in thread context), in SRAM on the board.
#define DRMP3_SCRATCH_STORAGE static DX_SRAM_BSS
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_FLOAT_OUTPUT
#include "dr_mp3.h"

namespace Aulib {

namespace {

size_t ReadRw(void *user, void *buf, size_t bytes)
{
	const int got = SDL_RWread(static_cast<SDL_RWops *>(user), buf, 1, static_cast<int>(bytes));
	return got > 0 ? static_cast<size_t>(got) : 0;
}

drmp3_bool32 SeekRw(void *user, int offset, drmp3_seek_origin origin)
{
	const int whence = origin == drmp3_seek_origin_current ? RW_SEEK_CUR : RW_SEEK_SET;
	return SDL_RWseek(static_cast<SDL_RWops *>(user), offset, whence) >= 0 ? DRMP3_TRUE : DRMP3_FALSE;
}

} // namespace

struct DecoderDrmp3::Impl {
	drmp3 mp3;
};

DecoderDrmp3::DecoderDrmp3() = default;

DecoderDrmp3::~DecoderDrmp3()
{
	release();
}

auto DecoderDrmp3::ensureDecoder() -> bool
{
	if (impl_ != nullptr)
		return true;
	if (rwops_ == nullptr || SDL_RWseek(rwops_, 0, RW_SEEK_SET) < 0)
		return false;
	auto impl = std::make_unique<Impl>();
	if (!drmp3_init(&impl->mp3, ReadRw, SeekRw, rwops_, nullptr))
		return false;
	impl_ = std::move(impl);
	return true;
}

void DecoderDrmp3::release()
{
	if (impl_ != nullptr) {
		drmp3_uninit(&impl_->mp3);
		impl_ = nullptr;
	}
}

auto DecoderDrmp3::open(SDL_RWops *rwops) -> bool
{
	if (isOpen())
		return true;
	rwops_ = rwops;
	if (!ensureDecoder())
		return false;
	channels_ = static_cast<int>(impl_->mp3.channels);
	rate_ = static_cast<int>(impl_->mp3.sampleRate);
	release();
	setIsOpen(true);
	return true;
}

auto DecoderDrmp3::getChannels() const -> int
{
	return channels_;
}

auto DecoderDrmp3::getRate() const -> int
{
	return rate_;
}

auto DecoderDrmp3::rewind() -> bool
{
	if (impl_ == nullptr)
		return true; // a new decoder starts at the beginning
	return drmp3_seek_to_pcm_frame(&impl_->mp3, 0);
}

auto DecoderDrmp3::duration() const -> std::chrono::microseconds
{
	if (rate_ == 0)
		return {};
	if (frames_ == 0 && rwops_ != nullptr) {
		// counted with a temporary decoder, which reads the whole stream once; on the heap,
		// because a decoder is about 16 KB
		auto mp3 = std::make_unique<drmp3>();
		if (SDL_RWseek(rwops_, 0, RW_SEEK_SET) >= 0 && drmp3_init(mp3.get(), ReadRw, SeekRw, rwops_, nullptr)) {
			frames_ = drmp3_get_pcm_frame_count(mp3.get());
			drmp3_uninit(mp3.get());
		}
		if (impl_ != nullptr)
			drmp3_seek_to_pcm_frame(&impl_->mp3, impl_->mp3.currentPCMFrame); // the stream position moved
	}
	return std::chrono::microseconds { frames_ * 1000000 / static_cast<uint64_t>(rate_) };
}

auto DecoderDrmp3::seekToTime(std::chrono::microseconds pos) -> bool
{
	if (!ensureDecoder())
		return false;
	return drmp3_seek_to_pcm_frame(&impl_->mp3, static_cast<drmp3_uint64>(pos.count()) * impl_->mp3.sampleRate / 1000000);
}

auto DecoderDrmp3::doDecoding(float buf[], int len, bool &callAgain) -> int
{
	callAgain = false;
	if (channels_ == 0 || !ensureDecoder())
		return 0;
	const drmp3_uint64 frames = drmp3_read_pcm_frames_f32(&impl_->mp3, static_cast<drmp3_uint64>(len) / static_cast<unsigned>(channels_), buf);
	return static_cast<int>(frames * static_cast<unsigned>(channels_));
}

auto DecodeMp3ToAdpcmWav(const uint8_t *mp3, size_t size, WavSink &sink) -> bool
{
	auto dec = std::make_unique<drmp3>(); // about 16 KB
	if (!drmp3_init_memory(dec.get(), mp3, size, nullptr))
		return false;
	const drmp3_uint64 frames = drmp3_get_pcm_frame_count(dec.get());
	const unsigned channels = dec->channels;
	// decoded as float (dr_mp3's s16 path keeps a 16 KB buffer on the stack) and converted
	auto f32 = std::make_unique<float[]>(ImaAdpcm::SamplesPerBlock * 2);
	const bool ok = channels >= 1 && channels <= 2 && drmp3_seek_to_pcm_frame(dec.get(), 0)
	    && WriteAdpcmWav(channels, dec->sampleRate, frames, sink, [&](int16_t *pcm, unsigned max) {
		       const auto got = static_cast<unsigned>(drmp3_read_pcm_frames_f32(dec.get(), max, f32.get()));
		       for (unsigned i = 0; i < got * channels; i++) {
			       const float v = f32[i] * 32768.0F;
			       pcm[i] = static_cast<int16_t>(v >= 32767.0F ? 32767 : v <= -32768.0F ? -32768 : static_cast<int>(v));
		       }
		       return got;
	       });
	drmp3_uninit(dec.get());
	return ok;
}

} // namespace Aulib
