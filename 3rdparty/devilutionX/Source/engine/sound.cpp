/**
 * @file sound.cpp
 *
 * Implementation of functions setting up the audio pipeline.
 */
#include "engine/sound.h"
#include "dx_fcache.hpp" // pico-lutionX
#include "dx_heap.h" // pico-lutionX
#ifdef DEVILUTIONX_PICO // pico-lutionX
#include <Aulib/DecoderDrmp3.h> // pico-lutionX
#include <Aulib/DecoderDrwav.h> // pico-lutionX
#endif // pico-lutionX

#include <cstdint>
#include <list>
#include <memory>
#include <mutex>

#include <SDL.h>

#include "engine/assets.hpp"
#include "init.h"
#include "options.h"
#include "utils/log.hpp"
#include "utils/math.h"
#include "utils/sdl_mutex.h"
#include "utils/stdcompat/algorithm.hpp"
#include "utils/stdcompat/optional.hpp"
#include "utils/stdcompat/shared_ptr_array.hpp"
#include "utils/str_cat.hpp"
#include "utils/stubs.h"

namespace devilution {

bool gbSndInited;
/** The active background music track id. */
_music_id sgnMusicTrack = NUM_MUSIC;

bool gbMusicOn = true;
/** Specifies whether sound effects are enabled. */
bool gbSoundOn = true;

namespace {

SoundSample music;

std::string GetMp3Path(const char *path)
{
	std::string mp3Path = path;
	const std::string::size_type dot = mp3Path.find_last_of('.');
	mp3Path.replace(dot + 1, mp3Path.size() - (dot + 1), "mp3");
	return mp3Path;
}

#ifdef DEVILUTIONX_PICO
// pico-lutionX: a sound effect (MP3, or PCM WAV) converted into an IMA ADPCM WAV file in the
// flash asset cache, in pieces (neither the converted file nor a large buffer is held in
// PSRAM). Null when the cache is off or full, or the file is in neither format.
std::unique_ptr<std::uint8_t[]> CacheAsAdpcmWav(const char *key, const char *path, bool isMp3, size_t *sizeOut)
{
	if (!DxCacheActive())
		return nullptr;
	size_t fileSize;
	std::unique_ptr<std::uint8_t[]> file = LoadFileInMem<std::uint8_t>(path, &fileSize);
	if (file == nullptr)
		return nullptr;

	struct CacheSink final : Aulib::WavSink {
		const char *key;
		size_t bytes = 0;
		dx_fcache_writer_t writer;
		auto begin(size_t length) -> bool override
		{
			bytes = length;
			return dx_fcache_write_begin(&writer, key, static_cast<unsigned>(length)) != 0;
		}
		void write(const void *data, size_t length) override
		{
			dx_fcache_write(&writer, data, static_cast<unsigned>(length));
		}
	};
	auto sink = std::make_unique<CacheSink>();
	sink->key = key;
	if (isMp3 ? !Aulib::DecodeMp3ToAdpcmWav(file.get(), fileSize, *sink) : !Aulib::EncodeWavToAdpcmWav(file.get(), fileSize, *sink))
		return nullptr;
	const void *cached = dx_fcache_write_end(&sink->writer);
	if (cached == nullptr)
		return nullptr;
	*sizeOut = sink->bytes;
	return std::unique_ptr<std::uint8_t[]>(static_cast<std::uint8_t *>(const_cast<void *>(cached)));
}
#endif

bool LoadAudioFile(const char *path, bool stream, bool errorDialog, SoundSample &result)
{
	DxHeapScope dxHeapScope(DXH_SOUND); // pico-lutionX: heap accounting
	bool isMp3 = true;
	std::string foundPath = GetMp3Path(path);
	AssetRef ref = FindAsset(foundPath.c_str());
	if (!ref.ok()) {
		ref = FindAsset(path);
		foundPath = path;
		isMp3 = false;
	}
	if (!ref.ok())
		ErrDlg("Audio file not found", StrCat(path, "\n", SDL_GetError(), "\n"), __FILE__, __LINE__);

#ifdef STREAM_ALL_AUDIO_MIN_FILE_SIZE
#if STREAM_ALL_AUDIO_MIN_FILE_SIZE == 0
	stream = true;
#else
	size_t size;
	if (!stream) {
		size = ref.size();
		stream = size >= STREAM_ALL_AUDIO_MIN_FILE_SIZE;
	}
#endif
#endif

	if (stream) {
		if (result.SetChunkStream(foundPath, isMp3, /*logErrors=*/true) != 0) {
			if (errorDialog) {
				ErrDlg("Failed to load audio file", StrCat(foundPath, "\n", SDL_GetError(), "\n"), __FILE__, __LINE__);
			}
			return false;
		}
	} else {
#if !defined(STREAM_ALL_AUDIO_MIN_FILE_SIZE) || STREAM_ALL_AUDIO_MIN_FILE_SIZE == 0
		const size_t size = ref.size();
#endif
#ifdef DEVILUTIONX_PICO
		// pico-lutionX: sound effects are kept in the flash asset cache and played from there,
		// so they take no PSRAM, as IMA ADPCM WAV files converted on the way in:
		//   - MP3 (spawn.mpq): decoding several MP3 streams while they play takes more time
		//     than the board has;
		//   - PCM WAV (DIABDAT.MPQ, 8-bit): half the flash, which the deep levels need.
		// If that fails, the file itself is cached and played as it is.
		{
			char key[DxCacheKeyMax];
			size_t cachedSize;
			DxCacheKey(key, "adpcm", foundPath.c_str(), 0);
			std::unique_ptr<std::uint8_t[]> cached = DxCacheFind(key, &cachedSize);
			if (cached == nullptr)
				cached = CacheAsAdpcmWav(key, foundPath.c_str(), isMp3, &cachedSize);
			if (cached != nullptr)
				return result.SetChunk(ArraySharedPtr<std::uint8_t>(cached.release()), cachedSize, /*isMp3=*/false) == 0;
			DxCacheKey(key, "snd", foundPath.c_str(), 0);
			cached = DxCacheFind(key, &cachedSize);
			if (cached == nullptr) {
				std::unique_ptr<std::byte[]> copied = DxLoadFileCached<std::byte>(foundPath.c_str(), &cachedSize, /*cacheOnly=*/true);
				cached.reset(reinterpret_cast<std::uint8_t *>(copied.release()));
			}
			if (cached != nullptr)
				return result.SetChunk(ArraySharedPtr<std::uint8_t>(cached.release()), cachedSize, isMp3) == 0;
		}
#endif
		AssetHandle handle = OpenAsset(std::move(ref));
		if (!handle.ok()) {
			if (errorDialog)
				ErrDlg("Failed to load audio file", StrCat(foundPath, "\n", SDL_GetError(), "\n"), __FILE__, __LINE__);
			return false;
		}
		auto waveFile = MakeArraySharedPtr<std::uint8_t>(size);
		if (!handle.read(waveFile.get(), size)) {
			if (errorDialog)
				ErrDlg("Failed to read file", StrCat(foundPath, ": ", SDL_GetError()), __FILE__, __LINE__);
			return false;
		}
		const int error = result.SetChunk(waveFile, size, isMp3);
		if (error != 0) {
			if (errorDialog)
				ErrSdl();
			return false;
		}
	}
	return true;
}

std::list<std::unique_ptr<SoundSample>> duplicateSounds;
std::optional<SdlMutex> duplicateSoundsMutex;

SoundSample *DuplicateSound(const SoundSample &sound)
{
	auto duplicate = std::make_unique<SoundSample>();
	if (duplicate->DuplicateFrom(sound) != 0)
		return nullptr;
	auto *result = duplicate.get();
	decltype(duplicateSounds.begin()) it;
	{
		const std::lock_guard<SdlMutex> lock(*duplicateSoundsMutex);
		duplicateSounds.push_back(std::move(duplicate));
		it = duplicateSounds.end();
		--it;
	}
	result->SetFinishCallback([it]([[maybe_unused]] Aulib::Stream &stream) {
		const std::lock_guard<SdlMutex> lock(*duplicateSoundsMutex);
		duplicateSounds.erase(it);
	});
	return result;
}

/** Maps from track ID to track name in spawn. */
const char *const SpawnMusicTracks[NUM_MUSIC] = {
	"music\\stowne.wav",
	"music\\slvla.wav",
	"music\\slvla.wav",
	"music\\slvla.wav",
	"music\\slvla.wav",
	"music\\dlvlf.wav",
	"music\\dlvle.wav",
	"music\\sintro.wav",
};
/** Maps from track ID to track name. */
const char *const MusicTracks[NUM_MUSIC] = {
	"music\\dtowne.wav",
	"music\\dlvla.wav",
	"music\\dlvlb.wav",
	"music\\dlvlc.wav",
	"music\\dlvld.wav",
	"music\\dlvlf.wav",
	"music\\dlvle.wav",
	"music\\dintro.wav",
};

int CapVolume(int volume)
{
	return clamp(volume, VOLUME_MIN, VOLUME_MAX);
}

} // namespace

void ClearDuplicateSounds()
{
	const std::lock_guard<SdlMutex> lock(*duplicateSoundsMutex);
	duplicateSounds.clear();
}

void snd_play_snd(TSnd *pSnd, int lVolume, int lPan)
{
	if (pSnd == nullptr || !gbSoundOn) {
		return;
	}

	uint32_t tc = SDL_GetTicks();
	if (tc - pSnd->start_tc < 80) {
		return;
	}

	SoundSample *sound = &pSnd->DSB;
	if (sound->IsPlaying()) {
		sound = DuplicateSound(*sound);
		if (sound == nullptr)
			return;
	}

	sound->PlayWithVolumeAndPan(lVolume, *sgOptions.Audio.soundVolume, lPan);
	pSnd->start_tc = tc;
}

std::unique_ptr<TSnd> sound_file_load(const char *path, bool stream)
{
	auto snd = std::make_unique<TSnd>();
	snd->start_tc = SDL_GetTicks() - 80 - 1;
#ifndef NOSOUND
	LoadAudioFile(path, stream, /*errorDialog=*/true, snd->DSB);
#endif
	return snd;
}

TSnd::~TSnd()
{
	if (DSB.IsLoaded())
		DSB.Stop();
	DSB.Release();
}

void snd_init()
{
	sgOptions.Audio.soundVolume.SetValue(CapVolume(*sgOptions.Audio.soundVolume));
	gbSoundOn = *sgOptions.Audio.soundVolume > VOLUME_MIN;
	sgbSaveSoundOn = gbSoundOn;

	sgOptions.Audio.musicVolume.SetValue(CapVolume(*sgOptions.Audio.musicVolume));
	gbMusicOn = *sgOptions.Audio.musicVolume > VOLUME_MIN;

	// Initialize the SDL_audiolib library. Set the output sample rate to
	// 22kHz, the audio format to 16-bit signed, use 2 output channels
	// (stereo), and a 2KiB output buffer.
	if (!Aulib::init(*sgOptions.Audio.sampleRate, AUDIO_S16, *sgOptions.Audio.channels, *sgOptions.Audio.bufferSize, *sgOptions.Audio.device)) {
		LogError(LogCategory::Audio, "Failed to initialize audio (Aulib::init): {}", SDL_GetError());
		return;
	}
	LogVerbose(LogCategory::Audio, "Aulib sampleRate={} channels={} frameSize={} format={:#x}",
	    Aulib::sampleRate(), Aulib::channelCount(), Aulib::frameSize(), Aulib::sampleFormat());

	duplicateSoundsMutex.emplace();
	gbSndInited = true;
}

void snd_deinit()
{
	if (gbSndInited) {
		Aulib::quit();
		duplicateSoundsMutex = std::nullopt;
	}

	gbSndInited = false;
}

_music_id GetLevelMusic(dungeon_type dungeonType)
{
	switch (dungeonType) {
	case DTYPE_TOWN:
		return TMUSIC_TOWN;
	case DTYPE_CATHEDRAL:
		return TMUSIC_CATHEDRAL;
	case DTYPE_CATACOMBS:
		return TMUSIC_CATACOMBS;
	case DTYPE_CAVES:
		return TMUSIC_CAVES;
	case DTYPE_HELL:
		return TMUSIC_HELL;
	case DTYPE_NEST:
		return TMUSIC_NEST;
	case DTYPE_CRYPT:
		return TMUSIC_CRYPT;
	default:
		return TMUSIC_INTRO;
	}
}

void music_stop()
{
	music.Release();
	sgnMusicTrack = NUM_MUSIC;
}

void music_start(_music_id nTrack)
{
	const char *trackPath;

	assert(nTrack < NUM_MUSIC);
	music_stop();
	if (!gbMusicOn)
		return;
	if (HaveSpawn())
		trackPath = SpawnMusicTracks[nTrack];
	else
		trackPath = MusicTracks[nTrack];

#ifdef DISABLE_STREAMING_MUSIC
	const bool stream = false;
#else
	const bool stream = true;
#endif
	if (!LoadAudioFile(trackPath, stream, /*errorDialog=*/false, music)) {
		music_stop();
		return;
	}

	music.SetVolume(*sgOptions.Audio.musicVolume, VOLUME_MIN, VOLUME_MAX);
	if (!diablo_is_focused())
		music_mute();
	if (!music.Play(/*numIterations=*/0)) {
		LogError(LogCategory::Audio, "Aulib::Stream::play (from music_start): {}", SDL_GetError());
		music_stop();
		return;
	}

	sgnMusicTrack = nTrack;
}

void sound_disable_music(bool disable)
{
	if (disable) {
		music_stop();
	} else if (sgnMusicTrack != NUM_MUSIC) {
		music_start(sgnMusicTrack);
	}
}

int sound_get_or_set_music_volume(int volume)
{
	if (volume == 1)
		return *sgOptions.Audio.musicVolume;

	sgOptions.Audio.musicVolume.SetValue(volume);

	if (music.IsLoaded())
		music.SetVolume(*sgOptions.Audio.musicVolume, VOLUME_MIN, VOLUME_MAX);

	return *sgOptions.Audio.musicVolume;
}

int sound_get_or_set_sound_volume(int volume)
{
	if (volume == 1)
		return *sgOptions.Audio.soundVolume;

	sgOptions.Audio.soundVolume.SetValue(volume);

	return *sgOptions.Audio.soundVolume;
}

void music_mute()
{
	if (music.IsLoaded())
		music.Mute();
}

void music_unmute()
{
	if (music.IsLoaded())
		music.Unmute();
}

} // namespace devilution
