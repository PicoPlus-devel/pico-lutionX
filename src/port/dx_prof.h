// dx_prof.h -- where core0's time goes, reported per frame with the status line.
//
// Time is charged to the zone that is running. dx_prof_enter switches to a zone and
// returns the one it interrupted, dx_prof_leave switches back to that, so zones nest:
// the vsync wait inside a present inside DrawAndBlit is charged to the vsync zone only.
// Thread context on core0 only; interrupt time goes to whatever zone it interrupts.
//
// The board implements it in src/pico/dx_prof.c, the test program with no-ops.

#pragma once

enum dx_prof_zone {
	DXP_OTHER,  // menus, level loading, everything not below
	DXP_LOGIC,  // game_loop: one 20 Hz game tick
	DXP_VIEW,   // DrawView: the dungeon or town view
	DXP_DRAW,   // the rest of DrawAndBlit: panels, cursor
	DXP_COPY,   // copying the back buffer (PSRAM) into the front buffer (SRAM)
	DXP_VSYNC,  // waiting for the vertical blank before that copy
	DXP_AUDIO,  // mixing and decoding
	DXP_SD,     // SD card reads and writes
	DXP_INPUT,  // turning USB and pad input into SDL events
	DXP_FLASH,  // erasing and programming the flash asset cache
	DXP_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif

int dx_prof_enter(int zone);
void dx_prof_leave(int previous);

#ifdef __cplusplus
}

struct DxProfZone {
	explicit DxProfZone(int zone)
	    : previous(dx_prof_enter(zone))
	{
	}
	~DxProfZone()
	{
		dx_prof_leave(previous);
	}
	DxProfZone(const DxProfZone &) = delete;
	DxProfZone &operator=(const DxProfZone &) = delete;

private:
	int previous;
};
#endif
