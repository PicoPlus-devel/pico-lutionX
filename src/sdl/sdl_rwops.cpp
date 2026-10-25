// sdl_rwops.cpp - SDL_RWops over stdio files and memory for the pico-lutionX SDL 1.2
// subset. On the board, stdio is backed by FatFs.
#include "SDL.h"

namespace {

int StdioSeek(SDL_RWops *context, int offset, int whence)
{
	static const int Whence[] = { SEEK_SET, SEEK_CUR, SEEK_END };
	if (whence < RW_SEEK_SET || whence > RW_SEEK_END)
		return SDL_SetError("Unknown value for 'whence'");
	if (fseek(context->hidden.stdio.fp, offset, Whence[whence]) != 0) {
		SDL_Error(SDL_EFSEEK);
		return -1;
	}
	return static_cast<int>(ftell(context->hidden.stdio.fp));
}

int StdioRead(SDL_RWops *context, void *ptr, int size, int maxnum)
{
	size_t n = fread(ptr, static_cast<size_t>(size), static_cast<size_t>(maxnum), context->hidden.stdio.fp);
	if (n == 0 && ferror(context->hidden.stdio.fp)) {
		SDL_Error(SDL_EFREAD);
		return -1;
	}
	return static_cast<int>(n);
}

int StdioWrite(SDL_RWops *context, const void *ptr, int size, int num)
{
	size_t n = fwrite(ptr, static_cast<size_t>(size), static_cast<size_t>(num), context->hidden.stdio.fp);
	if (n == 0 && ferror(context->hidden.stdio.fp)) {
		SDL_Error(SDL_EFWRITE);
		return -1;
	}
	return static_cast<int>(n);
}

int StdioClose(SDL_RWops *context)
{
	int status = 0;
	if (context->hidden.stdio.autoclose && fclose(context->hidden.stdio.fp) != 0) {
		SDL_Error(SDL_EFWRITE);
		status = -1;
	}
	SDL_FreeRW(context);
	return status;
}

int MemSeek(SDL_RWops *context, int offset, int whence)
{
	Uint8 *base;
	switch (whence) {
	case RW_SEEK_SET:
		base = context->hidden.mem.base;
		break;
	case RW_SEEK_CUR:
		base = context->hidden.mem.here;
		break;
	case RW_SEEK_END:
		base = context->hidden.mem.stop;
		break;
	default:
		return SDL_SetError("Unknown value for 'whence'");
	}
	Uint8 *here = base + offset;
	if (here < context->hidden.mem.base)
		here = context->hidden.mem.base;
	if (here > context->hidden.mem.stop)
		here = context->hidden.mem.stop;
	context->hidden.mem.here = here;
	return static_cast<int>(here - context->hidden.mem.base);
}

int MemRead(SDL_RWops *context, void *ptr, int size, int maxnum)
{
	if (size <= 0 || maxnum <= 0)
		return 0;
	size_t avail = static_cast<size_t>(context->hidden.mem.stop - context->hidden.mem.here);
	size_t num = avail / static_cast<size_t>(size);
	if (num > static_cast<size_t>(maxnum))
		num = static_cast<size_t>(maxnum);
	memcpy(ptr, context->hidden.mem.here, num * static_cast<size_t>(size));
	context->hidden.mem.here += num * static_cast<size_t>(size);
	return static_cast<int>(num);
}

int MemWrite(SDL_RWops *context, const void *ptr, int size, int num)
{
	if (size <= 0 || num <= 0)
		return 0;
	size_t avail = static_cast<size_t>(context->hidden.mem.stop - context->hidden.mem.here);
	size_t n = avail / static_cast<size_t>(size);
	if (n > static_cast<size_t>(num))
		n = static_cast<size_t>(num);
	memcpy(context->hidden.mem.here, ptr, n * static_cast<size_t>(size));
	context->hidden.mem.here += n * static_cast<size_t>(size);
	return static_cast<int>(n);
}

int MemWriteConst(SDL_RWops *context, const void *ptr, int size, int num)
{
	(void)context;
	(void)ptr;
	(void)size;
	(void)num;
	SDL_SetError("Can't write to read-only memory");
	return -1;
}

int MemClose(SDL_RWops *context)
{
	SDL_FreeRW(context);
	return 0;
}

} // namespace

extern "C" {

SDL_RWops *SDL_AllocRW(void)
{
	auto *area = static_cast<SDL_RWops *>(calloc(1, sizeof(SDL_RWops)));
	if (area == nullptr)
		SDL_OutOfMemory();
	return area;
}

void SDL_FreeRW(SDL_RWops *area)
{
	free(area);
}

SDL_RWops *SDL_RWFromFP(FILE *fp, int autoclose)
{
	SDL_RWops *rwops = SDL_AllocRW();
	if (rwops == nullptr)
		return nullptr;
	rwops->seek = StdioSeek;
	rwops->read = StdioRead;
	rwops->write = StdioWrite;
	rwops->close = StdioClose;
	rwops->hidden.stdio.fp = fp;
	rwops->hidden.stdio.autoclose = autoclose;
	return rwops;
}

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode)
{
	if (file == nullptr || *file == '\0' || mode == nullptr || *mode == '\0') {
		SDL_SetError("SDL_RWFromFile(): No file or no mode specified");
		return nullptr;
	}
	FILE *fp = fopen(file, mode);
	if (fp == nullptr) {
		SDL_SetError("Couldn't open %s", file);
		return nullptr;
	}
	SDL_RWops *rwops = SDL_RWFromFP(fp, 1);
	if (rwops == nullptr)
		fclose(fp);
	return rwops;
}

SDL_RWops *SDL_RWFromMem(void *mem, int size)
{
	SDL_RWops *rwops = SDL_AllocRW();
	if (rwops == nullptr)
		return nullptr;
	rwops->seek = MemSeek;
	rwops->read = MemRead;
	rwops->write = MemWrite;
	rwops->close = MemClose;
	rwops->hidden.mem.base = static_cast<Uint8 *>(mem);
	rwops->hidden.mem.here = rwops->hidden.mem.base;
	rwops->hidden.mem.stop = rwops->hidden.mem.base + size;
	return rwops;
}

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
	SDL_RWops *rwops = SDL_RWFromMem(const_cast<void *>(mem), size);
	if (rwops != nullptr)
		rwops->write = MemWriteConst;
	return rwops;
}

} // extern "C"
