// dx_boot.c -- RP2350 entry point for pico-lutionX.
//
// Early (from a runtime-init hook, before the C++ static constructors of the engine,
// which already use engine globals in PSRAM and the PSRAM heap): clocks (384 MHz, clk_hstx
// 126 MHz), PSRAM, the engine's PSRAM data and the heap. Then main(): the console, HSTX
// video, the SD card and USB, and the engine's DiabloMain.
//
// The clock sequence and the hardfault breadcrumb are taken from pico-quake2's q2_boot.c.

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pico/stdlib.h"
#include "pico/runtime.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "hardware/structs/qmi.h"

#include "dx_heap.h"
#include "SetupPsram.h"

#include "dx_audio.h"
#include "dx_console.h"
#include "dx_display.h"
#include "dx_fatal.h"
#include "dx_fatfs_io.h"
#include "dx_fcache.h"
#include "dx_flash.h"
#include "dx_input.h"
#include "dx_platform.h"

/*
==============================================================================

Hardfault breadcrumb: raw UART writes (no stdio, no locks) of the exception frame
and the fault status registers. The naked trampoline passes the frame pointer in r0.

Core0's stack limit register (MSPLIM, set in main) turns a stack overflow into a
fault. The trampoline clears the limit first, so the report can use the stack below it.

==============================================================================
*/

static void hf_putc(char c)
{
	uart_putc_raw(uart0, c);
}

static void hf_puthex(uint32_t v)
{
	for (int i = 28; i >= 0; i -= 4) {
		int d = (v >> i) & 0xf;
		hf_putc(d < 10 ? '0' + d : 'a' + d - 10);
	}
}

static void hf_field(const char *name, uint32_t v)
{
	for (; *name; name++)
		hf_putc(*name);
	hf_puthex(v);
}

void hf_report(uint32_t *sp)
{
	const uint32_t cfsr = *(volatile uint32_t *)0xE000ED28u;

	if (cfsr & (1u << 20)) {
		// UFSR.STKOF: the exception frame was not stacked, so there is no pc to report
		for (const char *m = "\n!HF core0 stack overflow"; *m; m++)
			hf_putc(*m);
	} else {
		hf_field("\n!HF pc=", sp[6]);
		hf_field(" lr=", sp[5]);
		hf_field(" psr=", sp[7]);
	}
	hf_field(" sp=", (uint32_t)sp);
	hf_field("\n!HF cfsr=", cfsr);
	hf_field(" hfsr=", *(volatile uint32_t *)0xE000ED2Cu);
	hf_field(" mmfar=", *(volatile uint32_t *)0xE000ED34u);
	hf_field(" bfar=", *(volatile uint32_t *)0xE000ED38u);
	hf_putc('\n');
	while (1)
		__asm volatile("nop");
}

__attribute__((naked)) void isr_hardfault(void)
{
	__asm volatile(
	    "movs r0, #0\n"
	    "msr msplim, r0\n"
	    "tst lr, #4\n"
	    "ite eq\n"
	    "mrseq r0, msp\n"
	    "mrsne r0, psp\n"
	    "b hf_report\n");
}

/*
==============================================================================

Clocks

==============================================================================
*/

// DX_CPU_KHZ picks the source of clk_hstx (126 MHz: the 25.2 MHz pixel clock), by the same
// rule as pico_shared's setClocksAndStartStdio():
// - a multiple of 48 MHz, such as 384000 (the default): USB and ADC are divided from
//   PLL_SYS, which leaves PLL_USB free to be a dedicated 126 MHz clk_hstx source;
// - 126000, 252000 or 504000: clk_hstx is clk_sys / 1, 2 or 4, and PLL_USB keeps its
//   48 MHz for USB.
// Any other clock is refused. 378 MHz (clk_sys / 3) gives the TMDS bits an uneven duty
// cycle, because clk_hstx has no duty-cycle correction for odd divisors: some displays
// show that as dots. DX_VREG is the core voltage (VREG_VOLTAGE_*).
#ifndef DX_CPU_KHZ
#define DX_CPU_KHZ 384000
#endif
#ifndef DX_VREG
#define DX_VREG VREG_VOLTAGE_1_50
#endif
#define DX_USB_ON_PLL_SYS (DX_CPU_KHZ % 48000 == 0 && DX_CPU_KHZ / 48000 <= 16)
#if !DX_USB_ON_PLL_SYS && DX_CPU_KHZ != 126000 && DX_CPU_KHZ != 252000 && DX_CPU_KHZ != 504000
#error DX_CPU_KHZ must be a multiple of 48000 (at most 768000), or 126000, 252000 or 504000
#endif

static void setup_clocks(void)
{
	// Flash clock at most 96 MHz (384 / 4), with the read sample delay scaled to stay at
	// about the same time in ns; set before clk_sys is raised (as pico-quake2 and pico_shared).
	const uint32_t flash_div = (DX_CPU_KHZ + 95999) / 96000 < 4 ? 4 : (DX_CPU_KHZ + 95999) / 96000;
	const uint32_t rxdelay = (3 * DX_CPU_KHZ + 189000) / 378000;

	vreg_disable_voltage_limit();
	vreg_set_voltage(DX_VREG);
	qmi_hw->m[0].timing = 0x60000000u | (14u << 11) | ((rxdelay > 7 ? 7 : rxdelay) << 8) | flash_div;
	busy_wait_us(100000);
	set_sys_clock_khz(DX_CPU_KHZ, true);
	busy_wait_us(100000);

#if DX_USB_ON_PLL_SYS
	// Move USB and ADC to PLL_SYS (an exact integer divider) before PLL_USB is retuned,
	// so neither ever sees it change; the native USB host keeps its 48 MHz. PLL_USB then
	// becomes the 126 MHz clk_hstx source (756 / 6), with no divider at all.
	clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
	    DX_CPU_KHZ * 1000u, USB_CLK_HZ);
	clock_configure(clk_adc, 0, CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
	    DX_CPU_KHZ * 1000u, USB_CLK_HZ);
	pll_deinit(pll_usb);
	pll_init(pll_usb, 1, 756000000, 6, 1);
	clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
	    126000000u, 126000000u);
#else
	// clk_sys / 1, 2 or 4 (126, 252 or 504 MHz): no divider or an even one, so the TMDS
	// bits keep an even duty cycle. PLL_USB is not touched and keeps its 48 MHz.
	clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
	    DX_CPU_KHZ * 1000u, 126000000u);
#endif
	// set_sys_clock_khz left clk_peri on PLL_USB; put it on PLL_SYS (halved above 400 MHz;
	// the SD card SPI and the UART derive their clocks from it)
	clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
	    DX_CPU_KHZ * 1000u, DX_CPU_KHZ > 400000 ? DX_CPU_KHZ * 500u : DX_CPU_KHZ * 1000u);
}

// The core voltage the regulator is set to, in millivolts: VREG_VOLTAGE_* codes run in 50 mV
// steps from 0.55 V to 1.30 V, then in uneven steps above the default limit.
static unsigned vreg_millivolts(void)
{
	static const uint16_t above_limit[16] = {
		1350, 1400, 1500, 1600, 1650, 1700, 1800, 1900, 2000, 2350, 2500, 2650, 2800, 3000, 3150, 3300
	};
	const unsigned vsel = (unsigned)vreg_get_voltage();

	return vsel <= VREG_VOLTAGE_1_30 ? 550 + 50 * vsel : above_limit[(vsel - VREG_VOLTAGE_1_35) & 15];
}

// Prints the clocks as measured by the frequency counter (against clk_ref), the QSPI clocks
// derived from clk_sys, and the core voltage read back from the regulator.
static void report_clocks(void)
{
	const uint32_t sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
	const uint32_t hstx = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_HSTX);
	const uint32_t peri = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
	const uint32_t flash_div = (qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB;
	const uint32_t psram_div = (qmi_hw->m[1].timing & QMI_M1_TIMING_CLKDIV_BITS) >> QMI_M1_TIMING_CLKDIV_LSB;
	const uint32_t flash = flash_div ? sys / flash_div : 0;
	const uint32_t psram = psram_div ? sys / psram_div : 0;
	const unsigned mv = vreg_millivolts();

	printf("clk_sys %lu.%lu MHz, clk_hstx %lu.%lu MHz, clk_peri %lu.%lu MHz (measured), "
	       "flash %lu.%lu MHz (/%lu), PSRAM %lu.%lu MHz (/%lu), core %u.%02u V\n",
	    (unsigned long)(sys / 1000), (unsigned long)(sys % 1000 / 100),
	    (unsigned long)(hstx / 1000), (unsigned long)(hstx % 1000 / 100),
	    (unsigned long)(peri / 1000), (unsigned long)(peri % 1000 / 100),
	    (unsigned long)(flash / 1000), (unsigned long)(flash % 1000 / 100), (unsigned long)flash_div,
	    (unsigned long)(psram / 1000), (unsigned long)(psram % 1000 / 100), (unsigned long)psram_div,
	    mv / 1000, mv % 1000 / 10);
}

/*
==============================================================================

Early init: runs from the SDK's runtime initialisers, after its own clock and
alarm setup and before the C++ static constructors. No stdio yet, and the FPU is not
enabled yet (the per-core initialisers come later), so this code uses neither;
errors are reported by main().

==============================================================================
*/

extern char __psram_data_start[], __psram_data_end[], __psram_data_load[];
extern char __psram_bss_start[], __psram_bss_end[];
extern char __psram_heap_start[], __psram_heap_end[];

#define DX_PSRAM_REQUIRED (8 * 1024 * 1024)

static int32_t psram_size;
static size_t heap_size;

// Holds the board's peripheral reset line (if it has one) released, as pico-quake2 does.
// Through the SIO registers: gpio_put and gpio_set_dir use the RP2350's GPIO coprocessor,
// which the per-core initialisers only enable after this early init, so they hard-faulted
// here (pico-quake2 calls this from main).
static void release_peripheral_reset(void)
{
#ifdef DX_PERIPH_RESET_PIN
	sio_hw->gpio_set = 1u << DX_PERIPH_RESET_PIN;
	sio_hw->gpio_oe_set = 1u << DX_PERIPH_RESET_PIN;
	gpio_set_function(DX_PERIPH_RESET_PIN, GPIO_FUNC_SIO);
#endif
}

static void dx_early_init(void)
{
	release_peripheral_reset();
	dx_flash_probe(); // before the overclock and the PSRAM
	setup_clocks();

	// SetupPsram drives the QMI in direct mode, which stalls XIP; nothing else runs yet.
	psram_size = SetupPsram(PSRAM_CS_PIN);
	if (psram_size < DX_PSRAM_REQUIRED) {
		// main() reports it; the heap stays uninitialised, so an allocation by a static
		// constructor stops in dx_heap_out_of_memory before main.
		return;
	}
	memcpy(__psram_data_start, __psram_data_load, (size_t)(__psram_data_end - __psram_data_start));
	memset(__psram_bss_start, 0, (size_t)(__psram_bss_end - __psram_bss_start));

	char *heap_end = (char *)0x11000000 + psram_size;
	if (heap_end > __psram_heap_end)
		heap_end = __psram_heap_end;
	heap_size = (size_t)(heap_end - __psram_heap_start);
	dx_heap_init(__psram_heap_start, heap_size);
}
PICO_RUNTIME_INIT_FUNC(dx_early_init, "12000");

/*
==============================================================================

Exit: the engine leaves through exit() (diablo_quit). Quitting resets the board
(back to the pico-bootLoader picker on a bootloader build); an error stays on screen.

==============================================================================
*/

void __attribute__((noreturn)) _exit(int status)
{
	if (status == 0) {
		printf("\npico-lutionX: quit, resetting\n");
		stdio_flush();
		dx_wiipad_shutdown(); // a pad left initialised across a reset can hang the next boot
		watchdog_reboot(0, 0, 1);
		while (1)
			tight_loop_contents();
	}
	dx_fatal("The game stopped with exit code %d.", status);
}

void dx_heap_out_of_memory(size_t bytes)
{
	dx_fcache_print_usage();
	if (bytes == 0)
		dx_fatal("Memory was used before the PSRAM heap was ready.\n"
		         "PSRAM: %ld KB on GPIO %d (8 MB are needed).",
		    (long)(psram_size / 1024), PSRAM_CS_PIN);
	dx_fatal("Out of memory: %u bytes requested,\n%u of %u KB in use.",
	    (unsigned)bytes, (unsigned)(dx_heap_used() / 1024), (unsigned)(dx_heap_size() / 1024));
}

/*
==============================================================================

Status line on the UART every 5 seconds (called from sdlb_present), only with
DX_PERF_LOG=1: printing waits for the UART, and the report held core0 long enough to make
the sound stutter.

==============================================================================
*/

// core0's stack (cmake/psram_linker.cmake) is painted in main, so the status line can
// report how deep it has been used, interrupts included. MSPLIM then makes the hardware
// fault on an overflow instead of writing into the SRAM heap below.
#define STACK_PAINT 0x5a5a5a5au

extern uint32_t __StackLimit[], __StackTop[];

static void __attribute__((noinline)) paint_stack(void)
{
	uint32_t *p = __StackLimit;
	uint32_t *sp = (uint32_t *)__builtin_frame_address(0) - 64;

	while (p < sp)
		*p++ = STACK_PAINT;
	__asm volatile("msr msplim, %0" : : "r"(__StackLimit));
}

#if DX_PERF_LOG
static unsigned stack_used(void)
{
	const uint32_t *p = __StackLimit;

	while (p < __StackTop && *p == STACK_PAINT)
		p++;
	return (unsigned)((__StackTop - p) * 4);
}

// Free SRAM for the SDK's malloc (drivers): free blocks plus what sbrk has not handed out.
static unsigned sram_heap_free(void)
{
	const struct mallinfo mi = mallinfo();
	const char *brk = (const char *)sbrk(0);

	return (unsigned)mi.fordblks + (unsigned)((const char *)__StackLimit - brk);
}

void dx_report_status(void)
{
	static uint32_t last_ms, frames;
	uint32_t now = to_ms_since_boot(get_absolute_time());

	frames++;
	if (now - last_ms < 5000)
		return;
	dx_fcache_stats_t fc;
	dx_fcache_get_stats(&fc);
	uint32_t copy_us, copy_max_us;
	dx_display_copy_us(&copy_us, &copy_max_us);
	printf("status: %lu.%lu fps, heap %u/%u KB (peak %u), SRAM %u KB free, stack %u/%u B core0 %u/%u B core1, "
	       "frame copy %lu.%lu ms (max %lu.%lu, %u torn), "
	       "cache %u assets %u KB, %u hits %u stored, %u KB written\n",
	    (unsigned long)(frames * 1000 / (now - last_ms)), (unsigned long)((frames * 10000 / (now - last_ms)) % 10),
	    (unsigned)(dx_heap_used() / 1024), (unsigned)(dx_heap_size() / 1024), (unsigned)(dx_heap_peak() / 1024),
	    sram_heap_free() / 1024,
	    stack_used(), (unsigned)((__StackTop - __StackLimit) * 4), dx_display_core1_stack_used(), 4096u,
	    (unsigned long)(copy_us / 1000), (unsigned long)(copy_us % 1000 / 100),
	    (unsigned long)(copy_max_us / 1000), (unsigned long)(copy_max_us % 1000 / 100), dx_display_torn(),
	    fc.entries, fc.used_bytes / 1024, fc.hits, fc.stores, fc.bytes_programmed / 1024);
	dx_prof_report(frames);
	frames = 0;
	last_ms = now;
}
#else
void dx_report_status(void)
{
}
#endif

/*
==============================================================================

main

==============================================================================
*/

int devilution_main(int argc, char **argv); // dx_backend.cpp: devilution::DiabloMain

int main(void)
{
	static char *argv[] = {
		"devilutionx",
		"--data-dir", DX_DATA_DIR,
		"--save-dir", DX_DATA_DIR,
		"--config-dir", DX_DATA_DIR,
		"-n", // no intro videos
		NULL
	};

	paint_stack();
	stdio_init_all();
	dx_console_attach_stdio();
	printf("pico-lutionX on %s, built %s %s\n", DX_BOARD_NAME, __DATE__, __TIME__);
	report_clocks();

	dx_display_init();
	// The Wii pad port shares its I2C bus with the Fruit Jam's codec: bring the pad up
	// (and quiet) before the codec is set up in dx_audio_init.
	dx_wiipad_init();
	if (psram_size <= 0)
		dx_fatal("No PSRAM found on GPIO %d.\nDevilutionX needs 8 MB of PSRAM.", PSRAM_CS_PIN);
	if (psram_size < DX_PSRAM_REQUIRED)
		dx_fatal("PSRAM too small: %ld KB.\nDevilutionX needs 8 MB of PSRAM.", (long)(psram_size / 1024));
	printf("PSRAM %ld KB: engine data %u KB, heap %u KB (%u KB used by constructors)\n",
	    (long)(psram_size / 1024),
	    (unsigned)((__psram_bss_end - __psram_data_start) / 1024),
	    (unsigned)(heap_size / 1024), (unsigned)(dx_heap_used() / 1024));

	dx_fatfs_init();
	dx_flash_init(dx_fatfs_data_id());
	dx_audio_init();
	dx_display_set_idle(dx_audio_idle); // mix ahead instead of waiting for the vertical blank
	dx_nespad_init(); // before the USB timer, which polls the controller ports
	dx_usb_init();
#if DX_PERF_LOG
	dx_prof_start();
#endif

	return devilution_main((int)(sizeof(argv) / sizeof(argv[0])) - 1, argv);
}
