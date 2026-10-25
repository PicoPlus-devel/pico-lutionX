// Aulib/Resampler.h - resampler interface of the pico-lutionX SDL_audiolib subset. The
// streams resample themselves (linear interpolation, src/aulib/aulib.cpp); resampler
// objects only select that a stream needs it.
#pragma once

namespace Aulib {

class Resampler {
public:
	Resampler() = default;
	virtual ~Resampler() = default;
	Resampler(const Resampler &) = delete;
	auto operator=(const Resampler &) -> Resampler & = delete;
};

} // namespace Aulib
