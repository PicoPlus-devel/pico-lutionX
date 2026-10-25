// dx_usb.cpp -- USB host for keyboards, mice and gamepads on Pico-PIO-USB.
//
// The pico_shared HID driver (3rdparty/pico_shared_drivers/usb_hid) keeps the latest
// state of every device in io::. This file brings the host stack up and runs it from a
// repeating 4 ms timer on core0 at the lowest interrupt priority (as pico-quake2's
// q2_usb.cpp). Each tick compares the devices with the previous tick and records the
// changes as events in a ring (key, mouse button and pad button presses and releases),
// so a tap shorter than a frame is not lost; the engine reads them in thread context
// (dx_input.cpp). Mouse motion and the analog sticks are read directly.
//
// The 1 ms USB frame runs from its own SRAM-resident interrupt (dx_usb_frame.c).

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/dma.h"
#include "tusb.h"
#include "pio_usb_configuration.h"
#include "gamepad.h"

#include "dx_input.h"

#ifndef HAS_USBPIO
#error "dx_usb.cpp needs a board with Pico-PIO-USB (HAS_USBPIO)"
#endif

#define USB_TICK_US 4000
#define USB_TICK_ALARM 1 // hardware alarm for the sampling timer
#define TRIGGER_ON 100   // analog trigger threshold, 0..255

static alarm_pool_t *tick_pool;
static repeating_timer_t tick_timer;

// ---- Event ring: written by the timer interrupt, read in thread context --------------

#define RING_SIZE 256 // power of two
static dx_input_event_t ring[RING_SIZE];
static volatile uint32_t ring_head; // written by the interrupt
static volatile uint32_t ring_tail; // written by the reader

static void push(uint8_t type, uint8_t code, bool down)
{
	uint32_t head = ring_head;
	if (head - ring_tail >= RING_SIZE)
		return; // full: drop
	ring[head % RING_SIZE] = { type, code, (uint8_t)(down ? 1 : 0) };
	__dmb();
	ring_head = head + 1;
}

bool dx_input_pop(dx_input_event_t *ev)
{
	uint32_t tail = ring_tail;
	if (tail == ring_head)
		return false;
	__dmb();
	*ev = ring[tail % RING_SIZE];
	ring_tail = tail + 1;
	return true;
}

// ---- Sampling ---------------------------------------------------------------------------

// The HID driver's slot for a keyboard has short name "KB" (a keyboard is also
// translated into pad buttons there, which this port does not want).
static bool slot_is_keyboard(const io::GamePadState &gp)
{
	return gp.GamePadShortName && strcmp(gp.GamePadShortName, "KB") == 0;
}

// A slot is in use from mount (which names it) to unmount (which clears the name).
static bool slot_in_use(const io::GamePadState &gp)
{
	return gp.GamePadShortName != nullptr;
}

static bool contains(const uint8_t *keys, uint8_t usage)
{
	for (int i = 0; i < 6; i++)
		if (keys[i] == usage)
			return true;
	return false;
}

static void sample_keyboard(bool present)
{
	static uint8_t last_mod, last_keys[6];
	uint8_t mod = 0, keys[6] = { 0 };

	if (present) {
		const io::KeyboardState &kb = io::getCurrentKeyboardState();
		mod = kb.modifier;
		memcpy(keys, kb.keycode, sizeof(keys));
	}
	// Usage 1 is ErrorRollOver (too many keys down): keep the previous state.
	if (contains(keys, 1))
		return;
	for (int bit = 0; bit < 8; bit++)
		if (((mod ^ last_mod) >> bit) & 1)
			push(DX_EV_KEY, (uint8_t)(0xe0 + bit), (mod >> bit) & 1);
	for (int i = 0; i < 6; i++)
		if (last_keys[i] > 3 && !contains(keys, last_keys[i]))
			push(DX_EV_KEY, last_keys[i], false);
	for (int i = 0; i < 6; i++)
		if (keys[i] > 3 && !contains(last_keys, keys[i]))
			push(DX_EV_KEY, keys[i], true);
	last_mod = mod;
	memcpy(last_keys, keys, sizeof(keys));
}

static void sample_mouse(void)
{
	static uint8_t last_buttons;
	io::MouseState &m = io::getCurrentMouseState();
	uint8_t buttons = m.connected ? m.buttons : 0;

	for (int bit = 0; bit < 3; bit++)
		if (((buttons ^ last_buttons) >> bit) & 1)
			push(DX_EV_MOUSE_BUTTON, (uint8_t)bit, (buttons >> bit) & 1);
	last_buttons = buttons;
}

static void sample_pads(uint32_t extra_buttons)
{
	static uint32_t last_buttons;
	static bool last_trigger[2];
	uint32_t buttons = extra_buttons;
	uint8_t trigger[2] = { 0, 0 };

	for (int i = 0; i < 2; i++) {
		const io::GamePadState &gp = io::getCurrentGamePadState(i);
		if (!slot_in_use(gp) || slot_is_keyboard(gp))
			continue;
		buttons |= gp.buttons;
		for (int t = 0; t < 2; t++)
			if (gp.trigger[t] > trigger[t])
				trigger[t] = gp.trigger[t];
	}
	uint32_t changed = buttons ^ last_buttons;
	for (int bit = 0; bit < 32; bit++)
		if ((changed >> bit) & 1)
			push(DX_EV_PAD_BUTTON, (uint8_t)bit, (buttons >> bit) & 1);
	last_buttons = buttons;
	for (int t = 0; t < 2; t++) {
		bool on = trigger[t] >= TRIGGER_ON;
		if (on != last_trigger[t])
			push(DX_EV_TRIGGER, (uint8_t)t, on);
		last_trigger[t] = on;
	}
}

static bool usb_tick(repeating_timer_t *)
{
	bool keyboard = false;

	tuh_task();
	for (int i = 0; i < 2; i++)
		if (slot_is_keyboard(io::getCurrentGamePadState(i)))
			keyboard = true;
	sample_keyboard(keyboard);
	sample_mouse();
	sample_pads(dx_nespad_buttons());
	return true;
}

void dx_input_mouse(int *dx, int *dy, int *wheel)
{
	io::MouseState &m = io::getCurrentMouseState();
	uint32_t irq = save_and_disable_interrupts();

	*dx = m.dx;
	*dy = m.dy;
	*wheel = m.wheel;
	m.dx = m.dy = m.wheel = 0;
	restore_interrupts(irq);
}

void dx_input_sticks(int16_t axes[4])
{
	int best_left = 0, best_right = 0;

	for (int i = 0; i < 4; i++)
		axes[i] = 0;
	for (int i = 0; i < 2; i++) {
		const io::GamePadState &gp = io::getCurrentGamePadState(i);
		if (!slot_in_use(gp) || slot_is_keyboard(gp))
			continue;
		int lx = (gp.axis[0] - 128) * 256, ly = (gp.axis[1] - 128) * 256;
		int rx = gp.look[0], ry = -gp.look[1]; // look: +y up; SDL: +y down
		int ml = (lx < 0 ? -lx : lx) + (ly < 0 ? -ly : ly);
		int mr = (rx < 0 ? -rx : rx) + (ry < 0 ? -ry : ry);
		if (ml > best_left) {
			best_left = ml;
			axes[0] = (int16_t)(lx > 32767 ? 32767 : lx);
			axes[1] = (int16_t)(ly > 32767 ? 32767 : ly);
		}
		if (mr > best_right) {
			best_right = mr;
			axes[2] = (int16_t)(rx > 32767 ? 32767 : (rx < -32768 ? -32768 : rx));
			axes[3] = (int16_t)(ry > 32767 ? 32767 : (ry < -32768 ? -32768 : ry));
		}
	}
}

void dx_usb_init(void)
{
#ifdef PIN_USB_HOST_VBUS
	gpio_init(PIN_USB_HOST_VBUS);
	gpio_set_dir(PIN_USB_HOST_VBUS, GPIO_OUT);
	gpio_put(PIN_USB_HOST_VBUS, 1);
#endif

	pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
	static_assert(PIN_USB_HOST_DP + 1 == PIN_USB_HOST_DM, "D+/D- must be adjacent");
	pio_cfg.pinout = PIO_USB_PINOUT_DPDM;
	pio_cfg.pin_dp = PIN_USB_HOST_DP;
	// As pico_shared does: PIO2 for both directions (PIO0/1 stay free for the audio and
	// SD drivers) and a free DMA channel
	pio_cfg.pio_tx_num = 2;
	pio_cfg.pio_rx_num = 2;
	pio_cfg.tx_ch = dma_claim_unused_channel(true);
	dma_channel_unclaim(pio_cfg.tx_ch); // pio_usb_bus_init claims it again
	pio_cfg.skip_alarm_pool = true;     // frames come from dx_usb_frame.c
	tuh_configure(CFG_TUH_RPI_PIO_USB, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

	const tusb_rhport_init_t host_init = {
		.role = TUSB_ROLE_HOST,
		.speed = TUSB_SPEED_AUTO,
	};
	tusb_init(CFG_TUH_RPI_PIO_USB, &host_init);
	dx_usb_frame_start();

	// Devices enumerate in the background once the timer runs.
	tick_pool = alarm_pool_create(USB_TICK_ALARM, 1);
	irq_set_priority(hardware_alarm_get_irq_num(USB_TICK_ALARM), PICO_LOWEST_IRQ_PRIORITY);
	alarm_pool_add_repeating_timer_us(tick_pool, -USB_TICK_US, usb_tick, NULL, &tick_timer);
	printf("dx_usb: PIO-USB host on GPIO %d/%d\n", PIN_USB_HOST_DP, PIN_USB_HOST_DM);
}
