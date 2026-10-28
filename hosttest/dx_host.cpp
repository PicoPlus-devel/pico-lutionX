// dx_host.cpp - Linux host harness for pico-lutionX.
//
// Runs the DevilutionX engine as built for the board (SDL 1.2 subset, engine heap of
// PSRAM size, see src/port/dx_heap.h) and implements the SDL backend with:
//   - a clock that runs in real time, plus a virtual offset that SDL_Delay advances
//     instead of sleeping (host runs are not slowed down by frame limiting);
//   - PPM frame dumps: DX_DUMP_EVERY=N DX_DUMP_DIR=dir;
//   - scripted input: DX_SCRIPT=file, one command per line (see RunScript below);
//   - DX_MAX_FRAMES=N: print a heap report and exit after N presented frames;
//   - DX_FCACHE=1: the flash asset cache on an emulated NOR flash region (DX_FCACHE_KB,
//     default 13312), mapped read-only outside erase/program so that any write into cached
//     data faults; DX_FCACHE_FILE keeps the region between runs (warm cache), and the
//     erase count of every 4 KB sector in <DX_FCACHE_FILE>.wear;
//   - audio: the mixer (src/aulib) is pulled at 22050 Hz as time passes; DX_AUDIO_WAV=file
//     writes what it produced;
//   - DX_SDCARD=image: the SD card mode. The engine's files go through the board's file
//     layer (src/pico/dx_fatfs_io.c on FatFs) on a card image made by dx_sdimg, and the
//     game starts as on the board: dx_fatfs_init, then the board's arguments (dx_boot.c),
//     followed by any given here. DX_SDCARD_FAIL and DX_SDCARD_TRACE: sdcard/sd_disk.c.
//
// dx_host_sdl is this file built with -DDX_HOST_SDL, plus dx_sdl.cpp and the firmware's
// src/pico/dx_input.cpp: the same program in a window, with sound, keyboard, mouse and
// gamepads, on real time. SDL_Delay sleeps and a present waits for the next 60 Hz vertical
// blank, as on the board; the sound is mixed for a model of the board's sound ring and
// queued on the sound device; input goes through the firmware's dx_input_poll. The flash asset cache is on
// unless DX_FCACHE=0. Everything else above applies unchanged; frames and script lines
// count presented frames.
//
// Usage:
//   dx_host [DevilutionX options]                  run the game (e.g. --demo 0 --timedemo)
//   dx_host --timedemo-test <fixture-dir> <data-dir>
//       replay the fixture headless, exactly as upstream's test/timedemo_test.cpp, and
//       compare the resulting hero with the reference (exit 0 = same).
//   dx_sdimg [-t fat32|exfat] [-s MB] [-c cluster-KB] [-f fragments] <image> <dir>=/roms/devilutionx
//       a card image with <dir> (the game data and the assets folder) where the board has it.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <sys/stat.h>

#include <SDL.h>

#include "diablo.h"
#include "Aulib/Stream.h"
#include "dx_fatfs_io.h"
#include "dx_fcache.h"
#include "dx_heap.h"
#include "dx_platform.h"
#include "dx_prof.h"
#include "sd_host.h"
#include "engine/demomode.h"
#include "init.h"
#include "options.h"
#include "pfile.h"
#include "player.h"
#include "sdl_backend.h"
#include "utils/display.h"
#include "utils/paths.h"
#include "utils/ui_fwd.h"

#ifdef DX_HOST_SDL
#include <unistd.h>

#include "dx_input.h"
#include "dx_sdl.h"
#endif

namespace {

// The harness's own files stay on the host in the SD card mode.
struct HostFiles {
	HostFiles() { dx_sdcard_host_files(1); }
	~HostFiles() { dx_sdcard_host_files(-1); }
};

// ---- Clock -----------------------------------------------------------------------------

timespec StartTime;
#ifndef DX_HOST_SDL
Uint32 VirtualOffset;
#endif

Uint32 RealMs()
{
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return static_cast<Uint32>((now.tv_sec - StartTime.tv_sec) * 1000 + (now.tv_nsec - StartTime.tv_nsec) / 1000000);
}

#ifdef DX_HOST_SDL
uint64_t RealUs()
{
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return static_cast<uint64_t>(now.tv_sec - StartTime.tv_sec) * 1000000 + (now.tv_nsec - StartTime.tv_nsec) / 1000;
}
#endif

// ---- Screen ----------------------------------------------------------------------------

SDL_Color Palette[256];
unsigned Frames;
unsigned DumpEvery;
std::string DumpDir = ".";
unsigned MaxFrames;

void DumpFrame(const Uint8 *pixels, int pitch, int width, int height)
{
	HostFiles host;
	char name[512];
	snprintf(name, sizeof(name), "%s/frame_%05u.ppm", DumpDir.c_str(), Frames);
	FILE *f = fopen(name, "wb");
	if (f == nullptr)
		return;
	fprintf(f, "P6\n%d %d\n255\n", width, height);
	std::vector<Uint8> row(static_cast<size_t>(width) * 3);
	for (int y = 0; y < height; y++) {
		const Uint8 *src = pixels + y * pitch;
		for (int x = 0; x < width; x++) {
			const SDL_Color &c = Palette[src[x]];
			row[x * 3] = c.r;
			row[x * 3 + 1] = c.g;
			row[x * 3 + 2] = c.b;
		}
		fwrite(row.data(), 1, row.size(), f);
	}
	fclose(f);
}

// ---- Scripted input --------------------------------------------------------------------
//
// One command per line, executed when the given number of frames has been presented:
//   <frame> key <name> [down|up]      key tap (or only down / up); names: RETURN ESCAPE
//                                     SPACE TAB BACKSPACE UP DOWN LEFT RIGHT F1..F12,
//                                     LSHIFT LCTRL LALT, or a single character
//   <frame> text <characters>         type characters (with unicode)
//   <frame> move <x> <y>              move the mouse
//   <frame> click <x> <y> [right]     move and click
//   <frame> mark <label>              heap report
//   <frame> exit                      heap report and exit

struct ScriptLine {
	unsigned frame;
	std::string command;
	std::string args;
};
std::vector<ScriptLine> Script;
size_t ScriptPos;

SDLKey KeyByName(const std::string &name)
{
	static const struct {
		const char *name;
		SDLKey key;
	} Names[] = {
		{ "RETURN", SDLK_RETURN }, { "ESCAPE", SDLK_ESCAPE }, { "SPACE", SDLK_SPACE },
		{ "TAB", SDLK_TAB }, { "BACKSPACE", SDLK_BACKSPACE }, { "UP", SDLK_UP },
		{ "DOWN", SDLK_DOWN }, { "LEFT", SDLK_LEFT }, { "RIGHT", SDLK_RIGHT },
		{ "LSHIFT", SDLK_LSHIFT }, { "LCTRL", SDLK_LCTRL }, { "LALT", SDLK_LALT },
	};
	for (const auto &n : Names)
		if (name == n.name)
			return n.key;
	if (name.size() >= 2 && name[0] == 'F')
		return static_cast<SDLKey>(SDLK_F1 + atoi(name.c_str() + 1) - 1);
	if (name.size() == 1)
		return static_cast<SDLKey>(tolower(static_cast<unsigned char>(name[0])));
	return SDLK_UNKNOWN;
}

void LoadScript(const char *path)
{
	HostFiles host;
	FILE *f = fopen(path, "r");
	if (f == nullptr) {
		fprintf(stderr, "dx_host: cannot open script %s\n", path);
		exit(2);
	}
	char line[512];
	while (fgets(line, sizeof(line), f) != nullptr) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;
		char command[32] = {};
		unsigned frame = 0;
		int consumed = 0;
		if (sscanf(line, "%u %31s %n", &frame, command, &consumed) < 2)
			continue;
		Script.push_back({ frame, command, line + consumed });
	}
	fclose(f);
}

void RunScript()
{
	while (ScriptPos < Script.size() && Script[ScriptPos].frame <= Frames) {
		const ScriptLine &s = Script[ScriptPos++];
		if (s.command == "key") {
			char name[32] = {}, mode[8] = {};
			sscanf(s.args.c_str(), "%31s %7s", name, mode);
			SDLKey key = KeyByName(name);
			if (strcmp(mode, "up") != 0)
				sdlp_key(1, key, static_cast<Uint16>(key < 128 ? key : 0));
			if (strcmp(mode, "down") != 0)
				sdlp_key(0, key, 0);
		} else if (s.command == "text") {
			for (char ch : s.args) {
				SDLKey key = static_cast<SDLKey>(tolower(static_cast<unsigned char>(ch)));
				sdlp_key(1, key, static_cast<Uint16>(ch));
				sdlp_key(0, key, 0);
			}
		} else if (s.command == "move" || s.command == "click") {
			int x = 0, y = 0;
			char which[8] = {};
			sscanf(s.args.c_str(), "%d %d %7s", &x, &y, which);
			sdlp_mouse_position(x, y);
			if (s.command == "click") {
				int button = strcmp(which, "right") == 0 ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
				sdlp_mouse_button(1, button);
				sdlp_mouse_button(0, button);
			}
		} else if (s.command == "mark") {
			dx_heap_mark(s.args.c_str());
		} else if (s.command == "exit") {
			dx_heap_mark("script exit");
			exit(0);
		} else {
			fprintf(stderr, "dx_host: unknown script command '%s'\n", s.command.c_str());
		}
	}
}

// ---- Audio --------------------------------------------------------------------------------

FILE *AudioWav;
uint32_t AudioFrames;
uint32_t AudioLastMs; // dx_host
uint32_t AudioPeak;

// Mixes frames (22050 Hz stereo) for DX_AUDIO_WAV and, in dx_host_sdl, the sound device.
void MixAudio(uint32_t frames)
{
	static int16_t buf[2 * 4096];
	while (frames > 0) {
		uint32_t n = frames < 4096 ? frames : 4096;
		aulib_mix(buf, static_cast<int>(n));
		for (uint32_t i = 0; i < 2 * n; i++) {
			uint32_t a = static_cast<uint32_t>(buf[i] < 0 ? -buf[i] : buf[i]);
			if (a > AudioPeak)
				AudioPeak = a;
		}
		if (AudioWav != nullptr)
			fwrite(buf, 4, n, AudioWav);
#ifdef DX_HOST_SDL
		dx_sdl_queue_audio(buf, static_cast<int>(n));
#endif
		AudioFrames += n;
		frames -= n;
	}
}

#ifdef DX_HOST_SDL
// dx_host_sdl models the board's sound ring (src/pico/dx_audio.c): it is played at
// 22050 Hz but never past the last mixed frame (a stall such as a level load delays the
// sound), and kept MixAhead frames mixed ahead of the play position. The sound device is
// kept filled as well, but it does not set the pace: a device that plays slowly or not at
// all (WSLg's sound server, for a while after it has been idle) cannot hold up the engine,
// whose menus wait for their sounds to end.
constexpr uint32_t MixAhead = 2048;             // frames (93 ms), MIX_AHEAD in dx_audio.c
constexpr int DeviceMaxQueued = 4 * MixAhead; // more queued on the device: it is behind
uint32_t RingPlayed;                            // frames, of AudioFrames mixed
uint64_t RingLastUs;
uint64_t RingRemainder; // the part of a frame played, in millionths

void UpdateAudio()
{
	const uint64_t now = RealUs();
	RingRemainder += (now - RingLastUs) * 22050;
	RingLastUs = now;
	RingPlayed = static_cast<uint32_t>(std::min<uint64_t>(AudioFrames, RingPlayed + RingRemainder / 1000000));
	RingRemainder %= 1000000;
	uint32_t frames = MixAhead - std::min(MixAhead, AudioFrames - RingPlayed);

	int queued = dx_sdl_audio_queued();
	if (queued > DeviceMaxQueued) {
		dx_sdl_clear_audio(); // keeps the device's delay bounded
		queued = 0;
	}
	if (queued >= 0 && queued < static_cast<int>(MixAhead))
		frames = std::max(frames, MixAhead - static_cast<uint32_t>(queued));
	MixAudio(frames);
}
#else
// dx_host mixes as much as the clock has advanced since the last call.
void UpdateAudio()
{
	const uint32_t nowMs = sdlb_ticks();
	if (AudioLastMs == 0 || nowMs < AudioLastMs) {
		AudioLastMs = nowMs;
		return;
	}
	uint32_t frames = (nowMs - AudioLastMs) * 22050 / 1000;
	if (frames == 0)
		return;
	AudioLastMs += frames * 1000 / 22050;
	MixAudio(frames);
}
#endif

#ifdef DX_HOST_SDL
// As dx_display_present on the board: a frame is shown at the next vertical blank of the
// 60 Hz output, and the sound is mixed meanwhile.
void WaitVerticalBlank()
{
	constexpr uint64_t PeriodUs = 1000000 / 60;
	const uint64_t vblank = (RealUs() / PeriodUs + 1) * PeriodUs;
	for (uint64_t now = RealUs(); now < vblank; now = RealUs()) {
		UpdateAudio();
		usleep(static_cast<useconds_t>(std::min<uint64_t>(vblank - now, 1000)));
	}
}
#endif

void OpenAudioWav()
{
	HostFiles host;
	if (const char *file = getenv("DX_AUDIO_WAV")) {
		AudioWav = fopen(file, "wb");
		if (AudioWav != nullptr)
			fseek(AudioWav, 44, SEEK_SET);
	}
}

void CloseAudioWav()
{
	fprintf(stderr, "dx_host: audio %u frames (%.1f s), peak %u\n", AudioFrames, AudioFrames / 22050.0, AudioPeak);
	if (AudioWav == nullptr)
		return;
	const uint32_t dataBytes = AudioFrames * 4;
	uint8_t h[44] = { 'R', 'I', 'F', 'F' };
	auto le32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) h[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
	auto le16 = [&](int at, uint16_t v) { h[at] = static_cast<uint8_t>(v); h[at + 1] = static_cast<uint8_t>(v >> 8); };
	le32(4, 36 + dataBytes);
	memcpy(h + 8, "WAVEfmt ", 8);
	le32(16, 16);
	le16(20, 1);
	le16(22, 2);
	le32(24, 22050);
	le32(28, 22050 * 4);
	le16(32, 4);
	le16(34, 16);
	memcpy(h + 36, "data", 4);
	le32(40, dataBytes);
	fseek(AudioWav, 0, SEEK_SET);
	fwrite(h, 1, 44, AudioWav);
	fclose(AudioWav);
}

// ---- Emulated flash for the asset cache -------------------------------------------------

unsigned char *FlashBase;
unsigned FlashSize;
unsigned FlashErased, FlashProgrammed;
double FlashSeconds; // estimated time the board would spend erasing and programming
std::vector<uint32_t> FlashWear; // erases per 4 KB sector, over all runs on one DX_FCACHE_FILE
dx_fcache_flash_t FlashOps;

// The wear counts are kept next to the flash region file.
std::string FlashWearFile(const char *flashFile)
{
	return std::string(flashFile) + ".wear";
}

void FlashWritable(bool writable)
{
	mprotect(FlashBase, FlashSize, writable ? PROT_READ | PROT_WRITE : PROT_READ);
}

// as on the board (src/pico/dx_flash.c): a write outside the region would hit the firmware
void CheckFlashRegion(const char *what, unsigned offset, unsigned len)
{
	if (offset > FlashSize || len > FlashSize - offset) {
		fprintf(stderr, "dx_host: flash %s outside the region: offset %u, %u bytes, region %u bytes\n", what, offset, len, FlashSize);
		abort();
	}
}

void FlashErase(unsigned offset, unsigned len)
{
	CheckFlashRegion("erase", offset, len);
	FlashWritable(true);
	memset(FlashBase + offset, 0xff, len);
	FlashWritable(false);
	for (unsigned s = offset / 4096; s < (offset + len) / 4096; s++)
		FlashWear[s]++;
	FlashErased += len;
	// typical NOR timings: 64 KB block erase 150 ms, 4 KB sector erase 45 ms
	FlashSeconds += (len / 65536) * 0.150 + ((len % 65536) / 4096) * 0.045;
}

void FlashProgram(unsigned offset, const void *data, unsigned len)
{
	const unsigned char *src = static_cast<const unsigned char *>(data);
	CheckFlashRegion("write", offset, len);
	FlashWritable(true);
	for (unsigned i = 0; i < len; i++)
		FlashBase[offset + i] &= src[i]; // programming can only clear bits
	FlashWritable(false);
	FlashProgrammed += len;
	FlashSeconds += (len / 256) * 0.0004; // 0.4 ms per 256-byte page
}

uint32_t GameDataId(const std::string &dataDir)
{
	struct stat st;
	for (const char *name : { "DIABDAT.MPQ", "diabdat.mpq", "spawn.mpq" })
		if (stat((dataDir + "/" + name).c_str(), &st) == 0)
			return static_cast<uint32_t>(st.st_size) ^ 0x5a17c0deu;
	return 0;
}

void SetupFlash(uint32_t dataId)
{
	const char *enable = getenv("DX_FCACHE");
	if (enable == nullptr || enable[0] != '1')
		return;
	HostFiles host;
	unsigned kb = 13312;
	if (const char *env = getenv("DX_FCACHE_KB"))
		kb = static_cast<unsigned>(atoi(env));
	FlashSize = kb * 1024;
	FlashBase = static_cast<unsigned char *>(mmap(nullptr, FlashSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
	memset(FlashBase, 0xff, FlashSize);
	FlashWear.assign(FlashSize / 4096, 0);
	if (const char *file = getenv("DX_FCACHE_FILE")) {
		if (FILE *f = fopen(file, "rb")) {
			size_t n = fread(FlashBase, 1, FlashSize, f);
			fclose(f);
			fprintf(stderr, "dx_host: flash region loaded from %s (%zu bytes)\n", file, n);
		}
		if (FILE *f = fopen(FlashWearFile(file).c_str(), "rb")) {
			if (fread(FlashWear.data(), sizeof(uint32_t), FlashWear.size(), f) != FlashWear.size())
				FlashWear.assign(FlashWear.size(), 0);
			fclose(f);
		}
	}
	FlashWritable(false);
	FlashOps = { FlashBase, FlashSize, FlashErase, FlashProgram };
	dx_fcache_set_flash(&FlashOps, dataId);
}

void ReportFlash()
{
	if (FlashBase == nullptr)
		return;
	HostFiles host;
	dx_fcache_stats_t st;
	dx_fcache_get_stats(&st);
	fprintf(stderr, "dx_fcache: %u entries, %u KB used of %u KB, %u hits, %u misses, %u stored, %u failed, %u evicted (%u recent); "
	                "%u KB erased, %u KB programmed (about %.1f s on the board)\n",
	    st.entries, st.used_bytes / 1024, st.capacity / 1024, st.hits, st.misses, st.stores, st.store_failures,
	    st.evictions, st.recent_evictions, FlashErased / 1024, FlashProgrammed / 1024, FlashSeconds);
	dx_fcache_print_usage();

	// the first 256 KB are the cache's two directory slots (src/port/dx_fcache.c)
	constexpr unsigned DirSectors = 256 * 1024 / 4096;
	uint32_t dirMax = 0, dataMax = 0;
	double dataSum = 0;
	for (size_t s = 0; s < FlashWear.size(); s++) {
		if (s < DirSectors) {
			dirMax = std::max(dirMax, FlashWear[s]);
		} else {
			dataMax = std::max(dataMax, FlashWear[s]);
			dataSum += FlashWear[s];
		}
	}
	if (FlashWear.size() > DirSectors)
		fprintf(stderr, "dx_fcache wear (erases per 4 KB sector, all runs on this region): directory max %u, data max %u mean %.1f\n",
		    dirMax, dataMax, dataSum / (FlashWear.size() - DirSectors));

	if (const char *file = getenv("DX_FCACHE_FILE")) {
		if (FILE *f = fopen(file, "wb")) {
			fwrite(FlashBase, 1, FlashSize, f);
			fclose(f);
		}
		if (FILE *f = fopen(FlashWearFile(file).c_str(), "wb")) {
			fwrite(FlashWear.data(), sizeof(uint32_t), FlashWear.size(), f);
			fclose(f);
		}
	}
}

// ---- Headless timedemo (upstream test/timedemo_test.cpp) --------------------------------

bool DummyGetHeroInfo(devilution::_uiheroinfo *)
{
	return true;
}

int RunTimedemoTest(const char *fixtureDir, const char *dataDir)
{
	using namespace devilution;
	std::string data = std::string(dataDir) + "/";
	std::string fixture = std::string(fixtureDir) + "/";
	paths::SetBasePath(data);
	paths::SetAssetsPath(data + "assets/");
	LoadCoreArchives();
	LoadGameArchives();
	if (!HaveSpawn() && !HaveDiabdat()) {
		fprintf(stderr, "dx_host: spawn.mpq or diabdat.mpq not found in %s\n", dataDir);
		return 2;
	}
	paths::SetPrefPath(fixture);
	paths::SetConfigPath(fixture);
	InitKeymapActions();
	LoadOptions();

	const int demoNumber = 0;
	Players.resize(1);
	MyPlayerId = demoNumber;
	MyPlayer = &Players[MyPlayerId];
	*MyPlayer = {};

	gbIsSpawn = true;
	gbIsHellfire = false;
	gbMusicOn = false;
	gbSoundOn = false;
	HeadlessMode = true;
	demo::InitPlayBack(demoNumber, true);
	pfile_ui_set_hero_infos(DummyGetHeroInfo);
	gbLoadGame = true;
	demo::OverrideOptions();
	AdjustToScreenGeometry(forceResolution);
	StartGame(false, true);

	HeroCompareResult result = pfile_compare_hero_demo(demoNumber, true);
	dx_heap_mark("timedemo-test end");
	if (result.status == HeroCompareResult::Same) {
		printf("TIMEDEMO: Same\n");
		return 0;
	}
	printf("TIMEDEMO: %s\n%s\n", result.status == HeroCompareResult::ReferenceNotFound ? "ReferenceNotFound" : "Different",
	    result.message.c_str());
	return 1;
}

void AtExit()
{
	dx_heap_mark("exit");
	ReportFlash();
	CloseAudioWav();
}

} // namespace

// ---- SDL backend -----------------------------------------------------------------------

extern "C" {

// time per frame (src/port/dx_prof.h): only measured on the board
int dx_prof_enter(int zone)
{
	(void)zone;
	return DXP_OTHER;
}

void dx_prof_leave(int previous)
{
	(void)previous;
}

Uint32 sdlb_ticks(void)
{
#ifdef DX_HOST_SDL
	return RealMs();
#else
	return RealMs() + VirtualOffset;
#endif
}

void sdlb_delay(Uint32 ms)
{
#ifdef DX_HOST_SDL
	// as on the board: waiting keeps the sound mixed
	const Uint32 start = RealMs();
	do {
		UpdateAudio();
		usleep(1000);
	} while (RealMs() - start < ms);
#else
	VirtualOffset += ms;
#endif
}

// The board copies a presented frame in the background (src/pico/dx_display.c), so the
// engine must not draw into the screen before sdlb_screen_wait(). The harness checks that:
// from each present until the next wait the screen's pages are read-only, and a write
// that skipped the wait faults with its stack. In the PSRAM pool like on the board, page
// aligned inside an allocation one page larger.
void *ScreenBase;
unsigned char *ScreenPixels;
size_t ScreenBytes;

void ProtectScreen(bool readOnly)
{
	if (ScreenPixels != nullptr)
		mprotect(ScreenPixels, ScreenBytes, readOnly ? PROT_READ : PROT_READ | PROT_WRITE);
}

void *sdlb_screen_alloc(int width, int height, int *pitch)
{
	const size_t page = 4096;
	*pitch = width;
	ScreenBytes = (static_cast<size_t>(width) * height + page - 1) & ~(page - 1);
	DxHeapScope scope(DXH_SCREEN);
	ScreenBase = calloc(ScreenBytes + page, 1);
	if (ScreenBase == nullptr)
		return nullptr;
	ScreenPixels = reinterpret_cast<unsigned char *>((reinterpret_cast<uintptr_t>(ScreenBase) + page - 1) & ~(page - 1));
	return ScreenPixels;
}

void sdlb_screen_free(void *pixels)
{
	(void)pixels;
	ProtectScreen(false);
	free(ScreenBase);
	ScreenBase = nullptr;
	ScreenPixels = nullptr;
}

void sdlb_present(const Uint8 *pixels, int pitch, int width, int height)
{
	UpdateAudio();
	Frames++;
#ifdef DX_HOST_SDL
	WaitVerticalBlank();
	dx_sdl_present(pixels, pitch, width, height, reinterpret_cast<const uint8_t *>(Palette));
	dx_sdl_pump(); // keeps the window responsive while a level loads (no polls then)
#endif
	if (DumpEvery != 0 && Frames % DumpEvery == 0)
		DumpFrame(pixels, pitch, width, height);
	if (MaxFrames != 0 && Frames >= MaxFrames) {
		dx_heap_mark("max frames");
		exit(0);
	}
	ProtectScreen(true);
}

void sdlb_screen_wait(void)
{
	ProtectScreen(false);
}

void sdlb_set_palette(const SDL_Color *colors, int first, int count)
{
	for (int i = 0; i < count; i++)
		if (first + i >= 0 && first + i < 256)
			Palette[first + i] = colors[i];
}

void sdlb_poll(void)
{
	UpdateAudio();
#ifdef DX_HOST_SDL
	dx_sdl_pump();
	if (dx_sdl_quit_requested())
		sdlp_quit();
	int x, y;
	if (dx_sdl_mouse_position(&x, &y))
		sdlp_mouse_position(x, y);
#endif
	RunScript();
#ifdef DX_HOST_SDL
	dx_input_poll();
#endif
}

} // extern "C"

int main(int argc, char **argv)
{
	clock_gettime(CLOCK_MONOTONIC, &StartTime);
	if (const char *env = getenv("DX_DUMP_EVERY"))
		DumpEvery = static_cast<unsigned>(atoi(env));
	if (const char *env = getenv("DX_DUMP_DIR"))
		DumpDir = env;
	if (const char *env = getenv("DX_MAX_FRAMES"))
		MaxFrames = static_cast<unsigned>(atoi(env));
	if (const char *env = getenv("DX_SCRIPT"))
		LoadScript(env);
	OpenAudioWav();
	atexit(AtExit);

	if (argc >= 4 && strcmp(argv[1], "--timedemo-test") == 0) {
		SetupFlash(GameDataId(argv[3]));
		return RunTimedemoTest(argv[2], argv[3]);
	}
#ifdef DX_HOST_SDL
	// as on the board, the flash asset cache is on unless DX_FCACHE says otherwise (the
	// full game does not fit in PSRAM without it)
	setenv("DX_FCACHE", "1", 0);
#endif
	const char *image = getenv("DX_SDCARD");
	// The engine looks for its loose assets next to the executable (as SDL_GetBasePath()
	// returns it); like on the board, use <data-dir>/assets/ instead.
	for (int i = 1; image == nullptr && i + 1 < argc; i++) {
		if (strcmp(argv[i], "--data-dir") == 0) {
			const uint32_t dataId = GameDataId(argv[i + 1]);
			if (dataId == 0) {
				// the engine's own message does not say where it looked
				fprintf(stderr, "dx_host: no DIABDAT.MPQ, diabdat.mpq or spawn.mpq in %s\n", argv[i + 1]);
				return 2;
			}
			devilution::paths::SetAssetsPath(std::string(argv[i + 1]) + "/assets/");
			SetupFlash(dataId);
		}
	}
#ifdef DX_HOST_SDL
	if (!dx_sdl_init())
		return 2;
	dx_input_init(); // as devilution_main on the board: joystick 0, the merged gamepads
#endif
	if (image != nullptr) {
		if (dx_sdcard_attach(image) != 0)
			return 2;
		setvbuf(stdout, nullptr, _IOLBF, 0); // dx_fatfs_init's printf in order with the engine's log
		// as src/pico/dx_boot.c main()
		dx_fatfs_init();
		SetupFlash(dx_fatfs_data_id());
		// the board's MPQ search paths: the data folder and the current directory (no XDG ones)
		setenv("XDG_DATA_DIRS", "", 1);
		// the board's SDL_GetBasePath() is the data folder (DEVILUTIONX_PICO_DATA_DIR)
		devilution::paths::SetAssetsPath(DX_DATA_DIR "/assets/");
		static std::vector<char *> boardArgs = {
			argv[0],
			const_cast<char *>("--data-dir"), const_cast<char *>(DX_DATA_DIR),
			const_cast<char *>("--save-dir"), const_cast<char *>(DX_DATA_DIR),
			const_cast<char *>("--config-dir"), const_cast<char *>(DX_DATA_DIR),
			const_cast<char *>("-n"),
		};
		boardArgs.insert(boardArgs.end(), argv + 1, argv + argc);
		boardArgs.push_back(nullptr);
		return devilution::DiabloMain(static_cast<int>(boardArgs.size()) - 1, boardArgs.data());
	}
	return devilution::DiabloMain(argc, argv);
}
