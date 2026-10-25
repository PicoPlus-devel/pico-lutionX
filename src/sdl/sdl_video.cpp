// sdl_video.cpp - surfaces, palettes, blits and the screen of the pico-lutionX SDL 1.2
// subset.
//
// Only what DevilutionX needs is implemented: 8 bpp palettized surfaces (all of the
// engine's rendering) and plain 32 bpp copies. The screen is a single 8 bpp surface whose
// pixels are allocated by the platform (sdlb_screen_alloc); SDL_Flip passes it to
// sdlb_present(). With SDL_DOUBLEBUF requested, DevilutionX renders straight into it.
#include "SDL.h"
#include "sdl_backend.h"

namespace {

SDL_Surface *Screen;
SDL_VideoInfo VideoInfo;
SDL_PixelFormat ScreenFormatTemplate;
SDL_Rect ModeRect = { 0, 0, 640, 480 };
SDL_Rect *ModeList[] = { &ModeRect, nullptr };
int CursorShown = 1;

int MaskShift(Uint32 mask)
{
	return mask == 0 ? 0 : __builtin_ctz(mask);
}

int MaskLoss(Uint32 mask)
{
	return mask == 0 ? 8 : 8 - __builtin_popcount(mask);
}

SDL_PixelFormat *AllocFormat(int bpp, Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask)
{
	auto *format = static_cast<SDL_PixelFormat *>(calloc(1, sizeof(SDL_PixelFormat)));
	if (format == nullptr)
		return nullptr;
	format->BitsPerPixel = static_cast<Uint8>(bpp);
	format->BytesPerPixel = static_cast<Uint8>((bpp + 7) / 8);
	format->alpha = 255;
	if (bpp <= 8) {
		format->palette = static_cast<SDL_Palette *>(calloc(1, sizeof(SDL_Palette)));
		if (format->palette == nullptr) {
			free(format);
			return nullptr;
		}
		format->palette->ncolors = 1 << bpp;
		format->palette->colors = static_cast<SDL_Color *>(calloc(format->palette->ncolors, sizeof(SDL_Color)));
		if (format->palette->colors == nullptr) {
			free(format->palette);
			free(format);
			return nullptr;
		}
		format->Rloss = format->Gloss = format->Bloss = format->Aloss = 8;
	} else {
		format->Rmask = rmask;
		format->Gmask = gmask;
		format->Bmask = bmask;
		format->Amask = amask;
		format->Rshift = static_cast<Uint8>(MaskShift(rmask));
		format->Gshift = static_cast<Uint8>(MaskShift(gmask));
		format->Bshift = static_cast<Uint8>(MaskShift(bmask));
		format->Ashift = static_cast<Uint8>(MaskShift(amask));
		format->Rloss = static_cast<Uint8>(MaskLoss(rmask));
		format->Gloss = static_cast<Uint8>(MaskLoss(gmask));
		format->Bloss = static_cast<Uint8>(MaskLoss(bmask));
		format->Aloss = static_cast<Uint8>(MaskLoss(amask));
	}
	return format;
}

void FreeFormat(SDL_PixelFormat *format)
{
	if (format == nullptr)
		return;
	if (format->palette != nullptr) {
		free(format->palette->colors);
		free(format->palette);
	}
	free(format);
}

SDL_Surface *AllocSurface(Uint32 flags, int width, int height, int depth,
    Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask, void *pixels, int pitch)
{
	if (depth != 8 && depth != 16 && depth != 24 && depth != 32) {
		SDL_SetError("Unsupported surface depth %d", depth);
		return nullptr;
	}
	auto *surface = static_cast<SDL_Surface *>(calloc(1, sizeof(SDL_Surface)));
	if (surface == nullptr) {
		SDL_OutOfMemory();
		return nullptr;
	}
	surface->format = AllocFormat(depth, rmask, gmask, bmask, amask);
	if (surface->format == nullptr) {
		free(surface);
		SDL_OutOfMemory();
		return nullptr;
	}
	surface->flags = flags & ~SDL_PREALLOC;
	surface->w = width;
	surface->h = height;
	if (pixels != nullptr) {
		surface->pitch = static_cast<Uint16>(pitch);
		surface->pixels = pixels;
		surface->flags |= SDL_PREALLOC;
	} else {
		surface->pitch = static_cast<Uint16>((width * surface->format->BytesPerPixel + 3) & ~3);
		if (width > 0 && height > 0) {
			surface->pixels = calloc(static_cast<size_t>(surface->pitch) * height, 1);
			if (surface->pixels == nullptr) {
				FreeFormat(surface->format);
				free(surface);
				SDL_OutOfMemory();
				return nullptr;
			}
		}
	}
	surface->clip_rect = { 0, 0, static_cast<Uint16>(width), static_cast<Uint16>(height) };
	surface->refcount = 1;
	return surface;
}

// Intersects `rect` with the clip rectangle of `surface`; returns false if empty.
bool ClipToSurface(const SDL_Surface *surface, int &x, int &y, int &w, int &h)
{
	const SDL_Rect &clip = surface->clip_rect;
	int x0 = x > clip.x ? x : clip.x;
	int y0 = y > clip.y ? y : clip.y;
	int x1 = (x + w) < (clip.x + clip.w) ? (x + w) : (clip.x + clip.w);
	int y1 = (y + h) < (clip.y + clip.h) ? (y + h) : (clip.y + clip.h);
	if (x1 <= x0 || y1 <= y0)
		return false;
	x = x0;
	y = y0;
	w = x1 - x0;
	h = y1 - y0;
	return true;
}

void SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int first, int count)
{
	for (int i = 0; i < count; i++) {
		int index = first + i;
		if (index >= 0 && index < palette->ncolors)
			palette->colors[index] = colors[i];
	}
}

} // namespace

extern "C" {

int SDL_VideoInit(const char *driver_name, Uint32 flags)
{
	(void)driver_name;
	(void)flags;
	return 0;
}

void SDL_VideoQuit(void)
{
	if (Screen != nullptr) {
		sdlb_screen_free(Screen->pixels);
		Screen->pixels = nullptr;
		FreeFormat(Screen->format);
		free(Screen);
		Screen = nullptr;
	}
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
	if (width == 0 || height == 0) {
		width = ModeRect.w;
		height = ModeRect.h;
	}
	if (bpp == 0)
		bpp = 8;
	if (bpp != 8 || width != ModeRect.w || height != ModeRect.h) {
		SDL_SetError("Unsupported video mode %dx%dx%d", width, height, bpp);
		return nullptr;
	}
	if (Screen != nullptr)
		return Screen;
	int pitch = 0;
	void *pixels = sdlb_screen_alloc(width, height, &pitch);
	if (pixels == nullptr) {
		SDL_OutOfMemory();
		return nullptr;
	}
	Screen = AllocSurface(0, width, height, 8, 0, 0, 0, 0, pixels, pitch);
	if (Screen == nullptr) {
		sdlb_screen_free(pixels);
		return nullptr;
	}
	Screen->flags = (flags & (SDL_DOUBLEBUF | SDL_HWPALETTE | SDL_FULLSCREEN)) | SDL_PREALLOC;
	return Screen;
}

SDL_Surface *SDL_GetVideoSurface(void)
{
	return Screen;
}

const SDL_VideoInfo *SDL_GetVideoInfo(void)
{
	ScreenFormatTemplate.BitsPerPixel = 8;
	ScreenFormatTemplate.BytesPerPixel = 1;
	VideoInfo.vfmt = Screen != nullptr ? Screen->format : &ScreenFormatTemplate;
	VideoInfo.current_w = ModeRect.w;
	VideoInfo.current_h = ModeRect.h;
	return &VideoInfo;
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags)
{
	(void)format;
	(void)flags;
	return ModeList;
}

int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags)
{
	(void)bpp;
	(void)flags;
	return (width == ModeRect.w && height == ModeRect.h) ? 8 : 0;
}

void SDL_WM_SetCaption(const char *title, const char *icon)
{
	(void)title;
	(void)icon;
}

SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode)
{
	return mode == SDL_GRAB_QUERY ? SDL_GRAB_OFF : mode;
}

int SDL_Flip(SDL_Surface *screen)
{
	if (screen == nullptr || screen != Screen)
		return SDL_SetError("SDL_Flip: not the screen surface");
	sdlb_present(static_cast<const Uint8 *>(screen->pixels), screen->pitch, screen->w, screen->h);
	return 0;
}

void SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects)
{
	(void)numrects;
	(void)rects;
	SDL_Flip(screen);
}

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	SDL_Flip(screen);
}

int SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors, int firstcolor, int ncolors)
{
	if (surface == nullptr || surface->format->palette == nullptr)
		return 0;
	if ((flags & SDL_LOGPAL) != 0 || surface != Screen)
		SetPaletteColors(surface->format->palette, colors, firstcolor, ncolors);
	if (surface == Screen && (flags & SDL_PHYSPAL) != 0)
		sdlb_set_palette(colors, firstcolor, ncolors);
	return 1;
}

int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor, int ncolors)
{
	return SDL_SetPalette(surface, SDL_LOGPAL | SDL_PHYSPAL, colors, firstcolor, ncolors);
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (format->palette != nullptr) {
		// Nearest palette entry.
		Uint32 best = 0;
		int bestDistance = 1 << 30;
		for (int i = 0; i < format->palette->ncolors; i++) {
			const SDL_Color &c = format->palette->colors[i];
			int dr = c.r - r, dg = c.g - g, db = c.b - b;
			int distance = dr * dr + dg * dg + db * db;
			if (distance < bestDistance) {
				bestDistance = distance;
				best = static_cast<Uint32>(i);
				if (distance == 0)
					break;
			}
		}
		return best;
	}
	return ((static_cast<Uint32>(r) >> format->Rloss) << format->Rshift)
	    | ((static_cast<Uint32>(g) >> format->Gloss) << format->Gshift)
	    | ((static_cast<Uint32>(b) >> format->Bloss) << format->Bshift)
	    | (((static_cast<Uint32>(a) >> format->Aloss) << format->Ashift) & format->Amask);
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
	return SDL_MapRGBA(format, r, g, b, 255);
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
    Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
	return AllocSurface(flags, width, height, depth, Rmask, Gmask, Bmask, Amask, nullptr, 0);
}

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth, int pitch,
    Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
	return AllocSurface(0, width, height, depth, Rmask, Gmask, Bmask, Amask, pixels, pitch);
}

void SDL_FreeSurface(SDL_Surface *surface)
{
	if (surface == nullptr || surface == Screen)
		return;
	if (--surface->refcount > 0)
		return;
	if ((surface->flags & SDL_PREALLOC) == 0)
		free(surface->pixels);
	FreeFormat(surface->format);
	free(surface);
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, SDL_PixelFormat *fmt, Uint32 flags)
{
	SDL_Surface *dst = AllocSurface(flags, src->w, src->h, fmt->BitsPerPixel,
	    fmt->Rmask, fmt->Gmask, fmt->Bmask, fmt->Amask, nullptr, 0);
	if (dst == nullptr)
		return nullptr;
	if (fmt->palette != nullptr && dst->format->palette != nullptr)
		SetPaletteColors(dst->format->palette, fmt->palette->colors, 0, fmt->palette->ncolors);
	if (src->format->BitsPerPixel == dst->format->BitsPerPixel) {
		for (int y = 0; y < src->h; y++)
			memcpy(static_cast<Uint8 *>(dst->pixels) + y * dst->pitch,
			    static_cast<const Uint8 *>(src->pixels) + y * src->pitch,
			    static_cast<size_t>(src->w) * src->format->BytesPerPixel);
		return dst;
	}
	if (src->format->BitsPerPixel == 8 && dst->format->BytesPerPixel == 4) {
		const SDL_Color *colors = src->format->palette->colors;
		for (int y = 0; y < src->h; y++) {
			const Uint8 *s = static_cast<const Uint8 *>(src->pixels) + y * src->pitch;
			auto *d = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(dst->pixels) + y * dst->pitch);
			for (int x = 0; x < src->w; x++) {
				const SDL_Color &c = colors[s[x]];
				d[x] = SDL_MapRGB(dst->format, c.r, c.g, c.b);
			}
		}
		return dst;
	}
	SDL_FreeSurface(dst);
	SDL_SetError("SDL_ConvertSurface: unsupported conversion");
	return nullptr;
}

int SDL_LockSurface(SDL_Surface *surface)
{
	if (surface == Screen)
		sdlb_screen_wait();
	surface->locked++;
	return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface)
{
	if (surface->locked > 0)
		surface->locked--;
}

void SDLDX_WaitScreen(void)
{
	sdlb_screen_wait();
}

int SDL_LowerBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (dst == Screen)
		sdlb_screen_wait();
	// The rectangles are already clipped by SDL_UpperBlit.
	const int bpp = src->format->BytesPerPixel;
	if (bpp != dst->format->BytesPerPixel)
		return SDL_SetError("SDL_LowerBlit: pixel formats differ");
	const Uint8 *s = static_cast<const Uint8 *>(src->pixels) + srcrect->y * src->pitch + srcrect->x * bpp;
	Uint8 *d = static_cast<Uint8 *>(dst->pixels) + dstrect->y * dst->pitch + dstrect->x * bpp;
	const int w = srcrect->w;
	if ((src->flags & SDL_SRCCOLORKEY) != 0 && bpp == 1) {
		const Uint8 key = static_cast<Uint8>(src->format->colorkey);
		for (int y = 0; y < srcrect->h; y++, s += src->pitch, d += dst->pitch)
			for (int x = 0; x < w; x++)
				if (s[x] != key)
					d[x] = s[x];
	} else {
		for (int y = 0; y < srcrect->h; y++, s += src->pitch, d += dst->pitch)
			memmove(d, s, static_cast<size_t>(w) * bpp);
	}
	return 0;
}

int SDL_UpperBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
	if (src == nullptr || dst == nullptr)
		return SDL_SetError("SDL_UpperBlit: passed a NULL surface");
	int sx = 0, sy = 0, w = src->w, h = src->h;
	if (srcrect != nullptr) {
		sx = srcrect->x;
		sy = srcrect->y;
		w = srcrect->w;
		h = srcrect->h;
	}
	// Clip against the source surface.
	if (sx < 0) {
		w += sx;
		sx = 0;
	}
	if (sy < 0) {
		h += sy;
		sy = 0;
	}
	if (sx + w > src->w)
		w = src->w - sx;
	if (sy + h > src->h)
		h = src->h - sy;
	int dx = dstrect != nullptr ? dstrect->x : 0;
	int dy = dstrect != nullptr ? dstrect->y : 0;
	// Clip against the destination clip rectangle, moving the source along.
	int cx = dx, cy = dy, cw = w, ch = h;
	if (w <= 0 || h <= 0 || !ClipToSurface(dst, cx, cy, cw, ch)) {
		if (dstrect != nullptr)
			dstrect->w = dstrect->h = 0;
		return 0;
	}
	SDL_Rect s = { static_cast<Sint16>(sx + cx - dx), static_cast<Sint16>(sy + cy - dy), static_cast<Uint16>(cw), static_cast<Uint16>(ch) };
	SDL_Rect d = { static_cast<Sint16>(cx), static_cast<Sint16>(cy), static_cast<Uint16>(cw), static_cast<Uint16>(ch) };
	if (dstrect != nullptr)
		*dstrect = d;
	return SDL_LowerBlit(src, &s, dst, &d);
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
	int x = 0, y = 0, w = dst->w, h = dst->h;
	if (dstrect != nullptr) {
		x = dstrect->x;
		y = dstrect->y;
		w = dstrect->w;
		h = dstrect->h;
	}
	if (!ClipToSurface(dst, x, y, w, h))
		return 0;
	if (dst == Screen)
		sdlb_screen_wait();
	const int bpp = dst->format->BytesPerPixel;
	Uint8 *row = static_cast<Uint8 *>(dst->pixels) + y * dst->pitch + x * bpp;
	for (int j = 0; j < h; j++, row += dst->pitch) {
		switch (bpp) {
		case 1:
			memset(row, static_cast<int>(color), static_cast<size_t>(w));
			break;
		case 2:
			for (int i = 0; i < w; i++)
				reinterpret_cast<Uint16 *>(row)[i] = static_cast<Uint16>(color);
			break;
		case 4:
			for (int i = 0; i < w; i++)
				reinterpret_cast<Uint32 *>(row)[i] = color;
			break;
		default:
			for (int i = 0; i < w; i++)
				memcpy(row + i * 3, &color, 3);
			break;
		}
	}
	return 0;
}

SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect)
{
	if (surface == nullptr)
		return SDL_FALSE;
	if (rect == nullptr) {
		surface->clip_rect = { 0, 0, static_cast<Uint16>(surface->w), static_cast<Uint16>(surface->h) };
		return SDL_TRUE;
	}
	int x = rect->x, y = rect->y, w = rect->w, h = rect->h;
	SDL_Rect full = { 0, 0, static_cast<Uint16>(surface->w), static_cast<Uint16>(surface->h) };
	surface->clip_rect = full;
	if (!ClipToSurface(surface, x, y, w, h)) {
		surface->clip_rect = { 0, 0, 0, 0 };
		return SDL_FALSE;
	}
	surface->clip_rect = { static_cast<Sint16>(x), static_cast<Sint16>(y), static_cast<Uint16>(w), static_cast<Uint16>(h) };
	return SDL_TRUE;
}

void SDL_GetClipRect(SDL_Surface *surface, SDL_Rect *rect)
{
	if (surface != nullptr && rect != nullptr)
		*rect = surface->clip_rect;
}

int SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key)
{
	if ((flag & SDL_SRCCOLORKEY) != 0) {
		surface->flags |= SDL_SRCCOLORKEY;
		surface->format->colorkey = key;
	} else {
		surface->flags &= ~SDL_SRCCOLORKEY;
		surface->format->colorkey = 0;
	}
	return 0;
}

int SDL_ShowCursor(int toggle)
{
	int previous = CursorShown;
	if (toggle >= 0)
		CursorShown = toggle;
	return previous;
}

} // extern "C"
