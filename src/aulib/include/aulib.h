// aulib.h - the subset of the SDL_audiolib interface that DevilutionX 1.5.5 uses, for
// pico-lutionX. Written for this project from the interface DevilutionX calls; it is
// not derived from the SDL_audiolib sources. Implemented in src/aulib/aulib.cpp.
#pragma once

#include <SDL.h>

#include <string>

namespace Aulib {

using AudioFormat = Uint16;

// Records the output format; the platform pulls mixed audio with aulib_mix().
auto init(int freq, AudioFormat format, int channels, int frameSize, const std::string &device = {}) -> bool;
void quit();
auto sampleFormat() noexcept -> AudioFormat;
auto sampleRate() noexcept -> int;
auto channelCount() noexcept -> int;
auto frameSize() noexcept -> int;

} // namespace Aulib
