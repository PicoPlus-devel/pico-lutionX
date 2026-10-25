#pragma once

#include <array>
#include "dx_fcache.hpp" // pico-lutionX
#include <cstdint>
#include <cstring>

#include <function_ref.hpp>

#include "appfat.h"
#include "engine/clx_sprite.hpp"
#include "engine/load_file.hpp"
#include "mpq/mpq_common.hpp"
#include "utils/cl2_to_clx.hpp"
#include "utils/endian_write.hpp"
#include "utils/pointer_value_union.hpp"
#include "utils/static_vector.hpp"
#include "utils/str_cat.hpp"

#ifdef UNPACKED_MPQS
#define DEVILUTIONX_CL2_EXT ".clx"
#else
#define DEVILUTIONX_CL2_EXT ".cl2"
#endif

namespace devilution {

OwnedClxSpriteListOrSheet LoadCl2ListOrSheet(const char *pszName, PointerOrValue<uint16_t> widthOrWidths);

template <size_t MaxCount>
OwnedClxSpriteSheet LoadMultipleCl2Sheet(tl::function_ref<const char *(size_t)> filenames, size_t count, uint16_t width)
{
	// pico-lutionX: flash asset cache (the sheet is named after its first file)
	char key[DxCacheKeyMax];
	std::snprintf(key, DxCacheKeyMax, "cl2s:%s:%u:%u", filenames(0), static_cast<unsigned>(count), static_cast<unsigned>(width));
	if (auto cached = DxCacheFind(key, nullptr))
		return OwnedClxSpriteSheet { std::move(cached), static_cast<uint16_t>(count) };
	StaticVector<std::array<char, MaxMpqPathSize>, MaxCount> paths;
	StaticVector<AssetRef, MaxCount> files;
	StaticVector<size_t, MaxCount> fileSizes;
	const size_t sheetHeaderSize = 4 * count;
	size_t totalSize = sheetHeaderSize;
	for (size_t i = 0; i < count; ++i) {
		{
			const char *path = filenames(i);
			paths.emplace_back();
			memcpy(paths.back().data(), path, strlen(path) + 1);
		}
		const char *path = paths.back().data();
		files.emplace_back(FindAsset(path));
		if (!files.back().ok()) {
			FailedToOpenFileError(path, files.back().error());
		}
		const size_t size = files.back().size();
		fileSizes.emplace_back(size);
		totalSize += size;
	}
	auto data = std::unique_ptr<uint8_t[]> { new uint8_t[totalSize] };
#ifndef UNPACKED_MPQS
	const PointerOrValue<uint16_t> frameWidth { width };
#endif
	size_t accumulatedSize = sheetHeaderSize;
	for (size_t i = 0; i < count; ++i) {
		const size_t size = fileSizes[i];
		AssetHandle handle = OpenAsset(std::move(files[i]));
		if (!handle.ok() || !handle.read(&data[accumulatedSize], size)) {
			FailedToOpenFileError(paths[i].data(), handle.error());
		}
		WriteLE32(&data[i * 4], accumulatedSize);
		accumulatedSize += size;
	}
#ifdef UNPACKED_MPQS
	return OwnedClxSpriteSheet { std::move(data), static_cast<uint16_t>(count) };
#else
	// pico-lutionX: the converted sprites go from the conversion buffer straight into the
	// cache, without another copy in PSRAM
	if (DxCacheActive()) {
		std::vector<uint8_t> clxData;
		clxData.reserve(accumulatedSize);
		Cl2ToClx(data.get(), accumulatedSize, frameWidth, clxData);
		data = nullptr;
		if (auto cached = DxCacheStore(key, clxData.data(), clxData.size()))
			return OwnedClxSpriteSheet { std::move(cached), static_cast<uint16_t>(count) };
		auto owned = std::unique_ptr<uint8_t[]>(new uint8_t[clxData.size()]);
		memcpy(owned.get(), clxData.data(), clxData.size());
		return OwnedClxSpriteSheet { std::move(owned), static_cast<uint16_t>(count) };
	}
	return Cl2ToClx(std::move(data), accumulatedSize, frameWidth).sheet();
#endif
}

inline OwnedClxSpriteList LoadCl2(const char *pszName, uint16_t width)
{
	return LoadCl2ListOrSheet(pszName, PointerOrValue<uint16_t> { width }).list();
}

inline OwnedClxSpriteList LoadCl2(const char *pszName, const uint16_t *widths)
{
	return LoadCl2ListOrSheet(pszName, PointerOrValue<uint16_t> { widths }).list();
}

inline OwnedClxSpriteSheet LoadCl2Sheet(const char *pszName, uint16_t width)
{
	return LoadCl2ListOrSheet(pszName, PointerOrValue<uint16_t> { width }).sheet();
}

} // namespace devilution
