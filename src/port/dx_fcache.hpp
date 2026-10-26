// dx_fcache.hpp -- the flash asset cache (dx_fcache.h) for the engine's loaders.
//
// Pattern in a loader:
//
//   char key[DxCacheKeyMax];
//   DxCacheKey(key, "cel", path, width);
//   if (auto cached = DxCacheFind(key, &size))
//       return OwnedClxSpriteListOrSheet::FromBuffer(std::move(cached), size);
//   ... load and convert into PSRAM as before ...
//   return DxCacheAdopt(key, std::move(result));
//
// The owner of cached memory frees it like any other buffer; dx_heap.cpp turns that into
// a release of the cache entry. Data that the engine changes after loading (colour
// translations) must be adopted only after the change; DxCacheSuspend keeps the
// loaders from caching meanwhile.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "dx_fcache.h"
#include "engine/assets.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/load_file.hpp"

namespace devilution {

constexpr size_t DxCacheKeyMax = 128;

inline int &DxCacheSuspendDepth()
{
	static int depth;
	return depth;
}

// While one exists, DxCacheFind finds nothing and DxCacheAdopt keeps data in PSRAM.
struct DxCacheSuspend {
	DxCacheSuspend() { ++DxCacheSuspendDepth(); }
	~DxCacheSuspend() { --DxCacheSuspendDepth(); }
	DxCacheSuspend(const DxCacheSuspend &) = delete;
	DxCacheSuspend &operator=(const DxCacheSuspend &) = delete;
};

inline bool DxCacheActive()
{
	return dx_fcache_enabled() && DxCacheSuspendDepth() == 0;
}

// The main menu's art (ui_art\) is not cached: the menus run while the game's memory is
// free, and in a long game the cache would evict it and write it again at the next start.
inline bool DxCacheActive(const char *key)
{
	const char *path = std::strchr(key, ':');
	if (path != nullptr && std::strncmp(path + 1, "ui_art", 6) == 0 && (path[7] == '\\' || path[7] == '/'))
		return false;
	return DxCacheActive();
}

// "<kind>:<path>:<param>"
inline void DxCacheKey(char *key, const char *kind, const char *path, unsigned param)
{
	std::snprintf(key, DxCacheKeyMax, "%s:%s:%u", kind, path, param);
}

// The cached bytes for key, owned like a heap buffer, or null.
template <typename T = uint8_t>
std::unique_ptr<T[]> DxCacheFind(const char *key, size_t *size)
{
	if (!DxCacheActive(key))
		return nullptr;
	unsigned length;
	const void *p = dx_fcache_find(key, &length);
	if (p == nullptr)
		return nullptr;
	if (size != nullptr)
		*size = length;
	return std::unique_ptr<T[]>(static_cast<T *>(const_cast<void *>(p)));
}

// A cached copy of the bytes, owned like a heap buffer, or null (data stays where it is).
template <typename T = uint8_t>
std::unique_ptr<T[]> DxCacheStore(const char *key, const void *data, size_t size)
{
	if (!DxCacheActive(key))
		return nullptr;
	const void *p = dx_fcache_store(key, data, static_cast<unsigned>(size));
	if (p == nullptr)
		return nullptr;
	return std::unique_ptr<T[]>(static_cast<T *>(const_cast<void *>(p)));
}

// Moves a buffer of `size` bytes into the cache; returns the cached copy, or the buffer.
template <typename T>
std::unique_ptr<T[]> DxCacheAdoptBuffer(const char *key, std::unique_ptr<T[]> &&data, size_t size)
{
	auto cached = DxCacheStore<T>(key, data.get(), size);
	if (cached == nullptr)
		return std::move(data);
	return cached;
}

inline OwnedClxSpriteListOrSheet DxCacheAdopt(const char *key, OwnedClxSpriteListOrSheet &&owned)
{
	ClxSpriteListOrSheet view { owned };
	const uint8_t *base = view.isSheet() ? view.sheet().data() : view.list().data();
	const size_t size = owned.dataSize();
	auto cached = DxCacheStore(key, base, size);
	if (cached == nullptr)
		return std::move(owned);
	return OwnedClxSpriteListOrSheet::FromBuffer(std::move(cached), size);
}

inline OwnedClxSpriteList DxCacheAdopt(const char *key, OwnedClxSpriteList &&owned)
{
	ClxSpriteList view { owned };
	auto cached = DxCacheStore(key, view.data(), view.dataSize());
	if (cached == nullptr)
		return std::move(owned);
	return OwnedClxSpriteList { std::move(cached) };
}

inline OwnedClxSpriteSheet DxCacheAdopt(const char *key, OwnedClxSpriteSheet &&owned)
{
	ClxSpriteSheet view { owned };
	auto cached = DxCacheStore(key, view.data(), view.dataSize());
	if (cached == nullptr)
		return std::move(owned);
	return OwnedClxSpriteSheet { std::move(cached), view.numLists() };
}

// A whole file through the cache (dungeon tiles, sound effects). A file that is not cached
// yet is copied from the MPQ straight into flash in pieces, so it never needs a large block
// of PSRAM. Without room in the cache it is loaded into PSRAM as before, or with cacheOnly
// null is returned. size (optional) receives the length in bytes.
template <typename T = byte>
std::unique_ptr<T[]> DxLoadFileCached(const char *path, size_t *sizeOut = nullptr, bool cacheOnly = false)
{
	char key[DxCacheKeyMax];
	DxCacheKey(key, cacheOnly ? "snd" : "file", path, 0);
	if (auto cached = DxCacheFind<T>(key, sizeOut))
		return cached;
	if (DxCacheActive(key)) {
		size_t size;
		AssetHandle handle = OpenAsset(path, size);
		auto writer = std::make_unique<dx_fcache_writer_t>();
		if (handle.ok() && size % sizeof(T) == 0 && dx_fcache_write_begin(writer.get(), key, static_cast<unsigned>(size))) {
			constexpr size_t Chunk = 16 * 1024;
			auto chunk = std::make_unique<uint8_t[]>(Chunk);
			for (size_t done = 0; done < size;) {
				const size_t n = size - done < Chunk ? size - done : Chunk;
				if (!handle.read(chunk.get(), n))
					break;
				dx_fcache_write(writer.get(), chunk.get(), static_cast<unsigned>(n));
				done += n;
			}
			if (const void *p = dx_fcache_write_end(writer.get())) {
				if (sizeOut != nullptr)
					*sizeOut = size;
				return std::unique_ptr<T[]>(static_cast<T *>(const_cast<void *>(p)));
			}
		}
	}
	if (cacheOnly)
		return nullptr;
	size_t count;
	std::unique_ptr<T[]> data = LoadFileInMem<T>(path, &count);
	if (sizeOut != nullptr)
		*sizeOut = count * sizeof(T);
	return data;
}

} // namespace devilution
