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
//     data faults; DX_FCACHE_FILE keeps the region between runs (warm cache);
//   - audio: the mixer (src/aulib) is pulled at 22050 Hz as time passes; DX_AUDIO_WAV=file
//     writes what it produced.
//
// Usage:
//   dx_host [DevilutionX options]                  run the game (e.g. --demo 0 --timedemo)
//   dx_host --timedemo-test <fixture-dir> <data-dir>
//       replay the fixture headless, exactly as upstream's test/timedemo_test.cpp, and
//       compare the resulting hero with the reference (exit 0 = same).
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
#include "dx_fcache.h"
#include "dx_heap.h"
#include "dx_prof.h"
#include "engine/demomode.h"
#include "init.h"
#include "options.h"
#include "pfile.h"
#include "player.h"
#include "sdl_backend.h"
#include "utils/display.h"
#include "utils/paths.h"
#include "utils/ui_fwd.h"

namespace {

// ---- Clock -----------------------------------------------------------------------------

timespec StartTime;
Uint32 VirtualOffset;

Uint32 RealMs()
{
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return static_cast<Uint32>((now.tv_sec - StartTime.tv_sec) * 1000 + (now.tv_nsec - StartTime.tv_nsec) / 1000000);
}

// ---- Screen ----------------------------------------------------------------------------

SDL_Color Palette[256];
unsigned Frames;
unsigned DumpEvery;
std::string DumpDir = ".";
unsigned MaxFrames;

void DumpFrame(const Uint8 *pixels, int pitch, int width, int height)
{
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
uint32_t AudioLastMs;
uint32_t AudioPeak;

void PullAudio(uint32_t nowMs)
{
	if (AudioLastMs == 0 || nowMs < AudioLastMs) {
		AudioLastMs = nowMs;
		return;
	}
	uint32_t frames = (nowMs - AudioLastMs) * 22050 / 1000;
	if (frames == 0)
		return;
	AudioLastMs += frames * 1000 / 22050;
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
		AudioFrames += n;
		frames -= n;
	}
}

void OpenAudioWav()
{
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
dx_fcache_flash_t FlashOps;

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

void SetupFlash(const std::string &dataDir)
{
	const char *enable = getenv("DX_FCACHE");
	if (enable == nullptr || enable[0] != '1')
		return;
	unsigned kb = 13312;
	if (const char *env = getenv("DX_FCACHE_KB"))
		kb = static_cast<unsigned>(atoi(env));
	FlashSize = kb * 1024;
	FlashBase = static_cast<unsigned char *>(mmap(nullptr, FlashSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
	memset(FlashBase, 0xff, FlashSize);
	if (const char *file = getenv("DX_FCACHE_FILE")) {
		if (FILE *f = fopen(file, "rb")) {
			size_t n = fread(FlashBase, 1, FlashSize, f);
			fclose(f);
			fprintf(stderr, "dx_host: flash region loaded from %s (%zu bytes)\n", file, n);
		}
	}
	FlashWritable(false);
	FlashOps = { FlashBase, FlashSize, FlashErase, FlashProgram };
	dx_fcache_set_flash(&FlashOps, GameDataId(dataDir));
}

void ReportFlash()
{
	if (FlashBase == nullptr)
		return;
	dx_fcache_stats_t st;
	dx_fcache_get_stats(&st);
	fprintf(stderr, "dx_fcache: %u entries, %u KB used of %u KB, %u hits, %u misses, %u stored, %u failed, %u evicted; "
	                "%u KB erased, %u KB programmed (about %.1f s on the board)\n",
	    st.entries, st.used_bytes / 1024, st.capacity / 1024, st.hits, st.misses, st.stores, st.store_failures,
	    st.evictions, FlashErased / 1024, FlashProgrammed / 1024, FlashSeconds);
	if (const char *file = getenv("DX_FCACHE_FILE")) {
		if (FILE *f = fopen(file, "wb")) {
			fwrite(FlashBase, 1, FlashSize, f);
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
	return RealMs() + VirtualOffset;
}

void sdlb_delay(Uint32 ms)
{
	VirtualOffset += ms;
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
	PullAudio(sdlb_ticks());
	Frames++;
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
	PullAudio(sdlb_ticks());
	RunScript();
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
		SetupFlash(argv[3]);
		return RunTimedemoTest(argv[2], argv[3]);
	}
	// The engine looks for its loose assets next to the executable (as SDL_GetBasePath()
	// returns it); like on the board, use <data-dir>/assets/ instead.
	for (int i = 1; i + 1 < argc; i++) {
		if (strcmp(argv[i], "--data-dir") == 0) {
			devilution::paths::SetAssetsPath(std::string(argv[i + 1]) + "/assets/");
			SetupFlash(argv[i + 1]);
		}
	}
	return devilution::DiabloMain(argc, argv);
}
