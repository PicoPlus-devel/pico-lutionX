#include "engine/load_clx.hpp"
#include "dx_fcache.hpp" // pico-lutionX
#include "dx_heap.h" // pico-lutionX

#include <cstdint>

#include <SDL.h>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif

#include "engine/assets.hpp"
#include "engine/load_file.hpp"

namespace devilution {

OptionalOwnedClxSpriteListOrSheet LoadOptionalClxListOrSheet(const char *path)
{
	// pico-lutionX: flash asset cache
	char key[DxCacheKeyMax];
	DxCacheKey(key, "clx", path, 0);
	size_t cachedSize;
	if (auto cached = DxCacheFind(key, &cachedSize))
		return OwnedClxSpriteListOrSheet::FromBuffer(std::move(cached), cachedSize);
	AssetRef ref = FindAsset(path);
	if (!ref.ok())
		return std::nullopt;
	const size_t size = ref.size();
	DxCacheableScope dxCacheable; // pico-lutionX: final sprite buffer
	std::unique_ptr<uint8_t[]> data { new uint8_t[size] };
	{
		AssetHandle handle = OpenAsset(std::move(ref));
		if (!handle.ok() || !handle.read(data.get(), size))
			return std::nullopt;
	}
	return DxCacheAdopt(key, OwnedClxSpriteListOrSheet::FromBuffer(std::move(data), size)); // pico-lutionX
}

OwnedClxSpriteListOrSheet LoadClxListOrSheet(const char *path)
{
	size_t size;
	// pico-lutionX: flash asset cache
	char key[DxCacheKeyMax];
	DxCacheKey(key, "clx", path, 0);
	if (auto cached = DxCacheFind(key, &size))
		return OwnedClxSpriteListOrSheet::FromBuffer(std::move(cached), size);
	DxCacheableScope dxCacheable; // pico-lutionX: final sprite buffer
	std::unique_ptr<uint8_t[]> data = LoadFileInMem<uint8_t>(path, &size);
	return DxCacheAdopt(key, OwnedClxSpriteListOrSheet::FromBuffer(std::move(data), size)); // pico-lutionX
}

} // namespace devilution
