// Aulib/ResamplerSpeex.h - see Aulib/Resampler.h.
#pragma once

#include <Aulib/Resampler.h>

namespace Aulib {

class ResamplerSpeex : public Resampler {
public:
	explicit ResamplerSpeex(int quality = 5)
	    : quality_(quality)
	{
	}
	auto quality() const noexcept -> int { return quality_; }
	void setQuality(int quality) { quality_ = quality; }

private:
	int quality_;
};

} // namespace Aulib
