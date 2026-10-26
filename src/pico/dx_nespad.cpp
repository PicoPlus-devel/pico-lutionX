// dx_nespad.cpp -- NES/SNES controller ports on PIO (pico_shared nespad).
// From pico-quake2's dx_nespad.cpp.
//
// The USB timer (dx_usb.cpp) asks for the buttons every 4 ms. Each call collects
// the read started on the previous call and starts the next one, so a read
// (about 200 us at the driver's 1 MHz PIO clock) never stalls the interrupt.
// The buttons are translated to io::GamePadState::Button bits per port and
// merged with the USB pads, so a SNES pad in a port acts exactly like the USB
// pad with the same buttons (dx_input.cpp names them by position). A NES pad
// gets a layout of its own, see nes_to_buttons.
//
// A SNES Mouse in either port (NESPAD_SNES_MOUSE in the board header) is read
// by the same reads; dx_usb.cpp merges its buttons and motion with a USB mouse.
//
// From pico-duke3D's duke_usb_input.cpp (nesButtons, nesToButtons), which has the
// two rules learned on hardware:
//   - nespad_begin starts each state machine at 1 MHz, so its first "irq wait"
//     lands about 1 us after enable. A read started before that is lost and the
//     state machine waits forever; hence the wait before the first start.
//   - nespad_read_finish blocks, so it is only called once nespad_read_ready
//     says the words are there. A port that never delivers is given up after 1 s.
//
// Boards without NES_PIN_CLK in their header get empty functions, and the linker
// drops the driver.

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "dx_input.h"

#if defined(NES_PIN_CLK) && NES_PIN_CLK >= 0

#include "nespad.h"
#include "gamepad.h"

#if defined(NES_PIN_CLK_1) && NES_PIN_CLK_1 >= 0
#define HAVE_PORT_1		1
#else
#define HAVE_PORT_1		0
#endif
#define DEAD_AFTER_US	1000000
// Cursor pixels per SNES Mouse count. The driver keeps the mouse at its lowest
// sensitivity, about 50 counts per inch: 4 moves the cursor across the 640
// pixels of the screen in about 3 inches.
#define MOUSE_SCALE		4

static bool		up, dead;
static uint32_t	last;				// io bits of the last completed read
static uint8_t	mouse_buttons;		// SNES Mouse buttons of the last completed read
static uint64_t	not_ready_since;

using Button = io::GamePadState::Button;

static uint32_t dpad_buttons (uint16_t w)
{
	uint32_t	b = 0;

	if (w & 0x0010) b |= Button::UP;
	if (w & 0x0020) b |= Button::DOWN;
	if (w & 0x0040) b |= Button::LEFT;
	if (w & 0x0080) b |= Button::RIGHT;
	return b;
}

// nespad_states_ext is in SNES serial order: bit 0 B, 1 Y, 2 Select, 3 Start,
// 4-7 up, down, left, right, 8 A, 9 X, 10 L, 11 R. A SNES pad's buttons go on
// the io buttons of the same position.
static uint32_t snes_to_buttons (uint16_t w)
{
	uint32_t	b = dpad_buttons (w);

	if (w & 0x0001) b |= Button::B;
	if (w & 0x0002) b |= Button::Y;
	if (w & 0x0004) b |= Button::SELECT;
	if (w & 0x0008) b |= Button::START;
	if (w & 0x0100) b |= Button::A;
	if (w & 0x0200) b |= Button::X;
	if (w & 0x0400) b |= Button::L;
	if (w & 0x0800) b |= Button::R;
	return b;
}

// A NES pad shifts out only 8 bits: A and B arrive where a SNES pad has B and
// Y. Two buttons cannot reach the game's actions one to one, so Select is a
// shift key on a NES pad, and is not passed on by itself:
//
//   A            primary action (io A: attack, talk, take an item)
//   B            secondary action (io X: open, pick up; selects in the menus)
//   Select+A     cast the spell (io Y)
//   Select+B     speedbook, or close the open panels (io B)
//   Select+Up    health potion (io L)
//   Select+Down  mana potion (io R)
//   Start        held, the game's gamepad menu: with the d-pad game menu, map,
//                character and inventory, with A the spellbook (io B) and
//                with B the quest log (io Y)
//   Select+Start game menu
//
// A port the driver has not identified (no SNES ID yet, which includes clone
// NES pads) is read as a NES pad: a SNES pad that does not identify itself does
// so the first time A, X, L or R is down.
static uint32_t nes_to_buttons (uint16_t w)
{
	const bool	a = w & 0x0001, b = w & 0x0002;
	const bool	select = w & 0x0004, start = w & 0x0008;
	uint32_t	io;

	if (start)
	{
		io = Button::START | dpad_buttons (w);
		if (select) io |= Button::SELECT;
		if (a) io |= Button::B;
		if (b) io |= Button::Y;
		return io;
	}
	if (select)
	{
		io = 0;
		if (a) io |= Button::Y;
		if (b) io |= Button::B;
		if (w & 0x0010) io |= Button::L;
		if (w & 0x0020) io |= Button::R;
		return io;
	}
	io = dpad_buttons (w);
	if (a) io |= Button::A;
	if (b) io |= Button::X;
	return io;
}

static uint32_t to_buttons (int port)
{
	switch (nespad_padtype[port])
	{
	case NESPAD_TYPE_SNES:
		return snes_to_buttons (nespad_states_ext[port]);
	case NESPAD_TYPE_MOUSE:
		return 0;
	default:
		return nes_to_buttons (nespad_states_ext[port]);
	}
}

void dx_nespad_init (void)
{
	uint32_t	cpu_khz = clock_get_hz (clk_sys) / 1000;
	bool		ok = nespad_begin (0, cpu_khz, NES_PIN_CLK, NES_PIN_DATA, NES_PIN_LAT, NES_PIO);

#if HAVE_PORT_1
	ok = nespad_begin (1, cpu_khz, NES_PIN_CLK_1, NES_PIN_DATA_1, NES_PIN_LAT_1, NES_PIO_1) && ok;
#endif
	if (!ok)
	{
		printf ("dx_nespad: no PIO state machine free, ports disabled\n");
		return;
	}
	busy_wait_us (100);		// let the state machines reach their irq wait
	nespad_read_start ();
	up = true;
#if HAVE_PORT_1
	printf ("dx_nespad: NES/SNES ports on GPIO %d-%d and %d-%d\n",
		NES_PIN_CLK, NES_PIN_LAT, NES_PIN_CLK_1, NES_PIN_LAT_1);
#else
	printf ("dx_nespad: NES/SNES port on GPIO %d-%d\n", NES_PIN_CLK, NES_PIN_LAT);
#endif
}

uint32_t dx_nespad_buttons (void)
{
	if (!up || dead)
		return 0;
	if (!nespad_read_ready ())
	{
		uint64_t	now = time_us_64 ();

		if (!not_ready_since)
			not_ready_since = now;
		else if (now - not_ready_since > DEAD_AFTER_US)
		{
			printf ("dx_nespad: a read never completed, ports disabled\n");
			dead = true;
			last = 0;
			mouse_buttons = 0;
		}
		return last;
	}
	not_ready_since = 0;
	nespad_read_finish ();
	last = to_buttons (0) | to_buttons (1);
	// 0 on a port without a mouse
	mouse_buttons = nespad_mouse[0].buttons | nespad_mouse[1].buttons;
	nespad_read_start ();
	return last;
}

uint8_t dx_nespad_mouse_buttons (void)
{
	return mouse_buttons;
}

void dx_nespad_mouse_motion (int *dx, int *dy)
{
	for (int i = 0; i < 2; i++)
	{
		*dx += nespad_mouse[i].dx * MOUSE_SCALE;
		*dy += nespad_mouse[i].dy * MOUSE_SCALE;
		nespad_mouse[i].dx = nespad_mouse[i].dy = 0;
	}
}

#else

void dx_nespad_init (void)
{
}

uint32_t dx_nespad_buttons (void)
{
	return 0;
}

uint8_t dx_nespad_mouse_buttons (void)
{
	return 0;
}

void dx_nespad_mouse_motion (int *, int *)
{
}

#endif
