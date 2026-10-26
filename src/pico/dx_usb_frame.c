// dx_usb_frame.c -- the 1 ms USB frame for Pico-PIO-USB, and SOFs during flash writes.
// From pico-quake2's dx_usb_frame.c.
//
// PIO-USB is set up with skip_alarm_pool (dx_usb.cpp), and its frame (SOF and
// every transaction, pio_usb_host_frame) is driven from hardware alarm 2 at the
// highest priority, by a handler that is in SRAM and installed directly in the
// vector table rather than going through the SDK's alarm pool, whose interrupt
// handler runs from flash.
//
// Flash erases and programs (the flash asset cache) take tens to hundreds of milliseconds
// with XIP off. Without SOFs for 3 ms the Fruit Jam's hub and the devices behind
// it suspend, and a DualShock 4 did not come back from that: its light went out
// and every later transfer failed. So around a flash write every interrupt on
// core0 is masked except the frame one, which then sends only a bare SOF (or a
// low-speed keep-alive) from SRAM. PIO-USB's full frame calls into flash and
// waits until the write is done.
//
// Boards with the native USB controller (no HAS_USBPIO) never call
// dx_usb_frame_start: the controller sends SOFs by itself, also during a flash
// write, so dx_usb_flash_begin only switches interrupts off there.

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/structs/nvic.h"

#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "usb_crc.h"
#include "usb_definitions.h"

#include "dx_input.h"

#define SOF_ALARM	2		// hardware alarm for the 1 ms USB frame

static uint32_t			sof_next;
static volatile bool	flash_busy;
static bool				sof_started;
static uint16_t			bare_frame;
static uint8_t			bare_sof[4] = { USB_SYNC, USB_PID_SOF, 0, 0 };
static uint8_t			bare_sof_encoded[4 * 2 * 7 / 6 + 2];
static uint8_t			bare_keepalive[1];

static void __not_in_flash_func(send_bare_sof) (void)
{
	root_port_t	*root = PIO_USB_ROOT_PORT (0);
	pio_port_t	*pp = PIO_USB_PIO_PORT (0);
	uint8_t		len;

	if (!(root->initialized && root->connected && !root->suspended))
		return;
	if (!root->is_fullspeed)
	{
		pio_usb_bus_usb_transfer (pp, bare_keepalive, 1);
		return;
	}
	bare_frame = (bare_frame + 1) & 0x7ff;
	bare_sof[2] = bare_frame & 0xff;
	bare_sof[3] = (uint8_t)((calc_usb_crc5 (bare_frame) << 3) | (bare_frame >> 8));
	len = pio_usb_ll_encode_tx_data (bare_sof, sizeof(bare_sof), bare_sof_encoded);
	pio_usb_bus_usb_transfer (pp, bare_sof_encoded, len);
}

static void __not_in_flash_func(sof_isr) (void)
{
	uint32_t	now;

	timer_hw->intr = 1u << SOF_ALARM;
	sof_next += 1000;
	now = timer_hw->timerawl;
	if ((int32_t)(sof_next - now) < 20)		// fell behind: resynchronise
		sof_next = now + 1000;
	timer_hw->alarm[SOF_ALARM] = sof_next;
	if (flash_busy)
		send_bare_sof ();
	else
		pio_usb_host_frame ();
}

void dx_usb_frame_start (void)
{
	unsigned	irq = hardware_alarm_get_irq_num (SOF_ALARM);

	pio_usb_ll_encode_tx_data (NULL, 0, bare_keepalive);
	hardware_alarm_claim (SOF_ALARM);
	irq_set_exclusive_handler (irq, sof_isr);
	irq_set_priority (irq, PICO_HIGHEST_IRQ_PRIORITY);
	hw_set_bits (&timer_hw->inte, 1u << SOF_ALARM);
	irq_set_enabled (irq, true);
	sof_next = timer_hw->timerawl + 1000;
	timer_hw->alarm[SOF_ALARM] = sof_next;
	sof_started = true;
}

dx_irq_state_t dx_usb_flash_begin (void)
{
	dx_irq_state_t	st;

	if (!sof_started)
	{
		st.masked = false;
		st.primask = save_and_disable_interrupts ();
		return st;
	}
	// every interrupt but the frame one: their handlers may run from flash
	st.masked = true;
	st.iser[0] = nvic_hw->iser[0];
	st.iser[1] = nvic_hw->iser[1];
	bare_frame = (uint16_t)pio_usb_host_get_frame_number ();
	nvic_hw->icer[0] = st.iser[0] & ~(1u << hardware_alarm_get_irq_num (SOF_ALARM));
	nvic_hw->icer[1] = st.iser[1];
	__dsb ();
	__isb ();
	flash_busy = true;
	return st;
}

void dx_usb_flash_end (dx_irq_state_t st)
{
	if (!st.masked)
	{
		restore_interrupts (st.primask);
		return;
	}
	flash_busy = false;
	__dsb ();
	nvic_hw->iser[0] = st.iser[0];
	nvic_hw->iser[1] = st.iser[1];
}
