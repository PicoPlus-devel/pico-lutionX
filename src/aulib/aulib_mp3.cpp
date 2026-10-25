// aulib_mp3.cpp - MP3 decoding for the pico-lutionX SDL_audiolib subset, with dr_mp3
// (3rdparty/dx-deps/dr_libs, public domain / MIT-0).

#include <Aulib/DecoderDrmp3.h>

#include <cstring>

#include "dx_sram.h"
#include "ima_adpcm.h"

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
	using namespace ImaAdpcm;

	auto dec = std::make_unique<drmp3>(); // about 16 KB
	if (!drmp3_init_memory(dec.get(), mp3, size, nullptr))
		return false;
	const drmp3_uint64 frames = drmp3_get_pcm_frame_count(dec.get());
	const unsigned channels = dec->channels;
	const unsigned rate = dec->sampleRate;
	const unsigned blockAlign = BlockBytesPerChannel * channels;
	const auto blocks = static_cast<uint32_t>((frames + SamplesPerBlock - 1) / SamplesPerBlock);
	const uint32_t dataBytes = blocks * blockAlign;
	constexpr size_t HeaderBytes = 60; // RIFF, fmt (20 bytes), fact, data
	if (frames == 0 || frames > UINT32_MAX / 2 || channels == 0 || channels > 2
	    || !drmp3_seek_to_pcm_frame(dec.get(), 0) || !sink.begin(HeaderBytes + dataBytes)) {
		drmp3_uninit(dec.get());
		return false;
	}

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

	// decoded as float (dr_mp3's s16 path keeps a 16 KB buffer on the stack), one block at
	// a time; the last block is padded with silence (the fact chunk holds the real length)
	auto f32 = std::make_unique<float[]>(SamplesPerBlock * 2);
	auto pcm = std::make_unique<int16_t[]>(SamplesPerBlock * 2);
	auto block = std::make_unique<uint8_t[]>(BlockBytesPerChannel * 2);
	Channel state[2];
	for (uint32_t b = 0; b < blocks; b++) {
		const drmp3_uint64 got = drmp3_read_pcm_frames_f32(dec.get(), SamplesPerBlock, f32.get());
		const unsigned decoded = static_cast<unsigned>(got) * channels;
		for (unsigned i = 0; i < SamplesPerBlock * channels; i++) {
			const float v = i < decoded ? f32[i] * 32768.0F : 0.0F;
			pcm[i] = static_cast<int16_t>(v >= 32767.0F ? 32767 : v <= -32768.0F ? -32768 : static_cast<int>(v));
		}

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
	drmp3_uninit(dec.get());
	return true;
}

} // namespace Aulib
