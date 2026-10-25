#include "engine/load_cl2.hpp"
#include "dx_fcache.hpp" // pico-lutionX

#include <cstdint>
#include <memory>
#include <utility>

#include "mpq/mpq_common.hpp"
#include "utils/str_cat.hpp"

#ifdef UNPACKED_MPQS
#include "engine/load_clx.hpp"
#else
#include "engine/load_file.hpp"
#include "utils/cl2_to_clx.hpp"
#endif

namespace devilution {

OwnedClxSpriteListOrSheet LoadCl2ListOrSheet(const char *pszName, PointerOrValue<uint16_t> widthOrWidths)
{
	char path[MaxMpqPathSize];
	*BufCopy(path, pszName, DEVILUTIONX_CL2_EXT) = '\0';
#ifdef UNPACKED_MPQS
	return LoadClxListOrSheet(path);
#else
	size_t size;
	// pico-lutionX: flash asset cache
	char key[DxCacheKeyMax];
	DxCacheKey(key, "cl2", path, widthOrWidths.HoldsPointer() ? 0u : widthOrWidths.AsValue());
	if (auto cached = DxCacheFind(key, &size))
		return OwnedClxSpriteListOrSheet::FromBuffer(std::move(cached), size);
	std::unique_ptr<uint8_t[]> data = LoadFileInMem<uint8_t>(path, &size);
	// pico-lutionX: the converted sprites go from the conversion buffer straight into the
	// cache, without another copy in PSRAM
	if (DxCacheActive()) {
		std::vector<uint8_t> clxData;
		clxData.reserve(size);
		const uint16_t numLists = Cl2ToClx(data.get(), size, widthOrWidths, clxData);
		data = nullptr;
		if (auto cached = DxCacheStore(key, clxData.data(), clxData.size()))
			return OwnedClxSpriteListOrSheet { std::move(cached), numLists };
		auto owned = std::unique_ptr<uint8_t[]>(new uint8_t[clxData.size()]);
		memcpy(owned.get(), clxData.data(), clxData.size());
		return OwnedClxSpriteListOrSheet { std::move(owned), numLists };
	}
	return Cl2ToClx(std::move(data), size, widthOrWidths);
#endif
}

} // namespace devilution
