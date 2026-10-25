// dx_prof.c -- where core0's time goes, reported by dx_report_status (dx_boot.c):
//   - the time per frame of each zone (src/port/dx_prof.h);
//   - the hit rate of the 16 KB XIP cache that flash code, the flash asset cache and
//     PSRAM share;
//   - the hottest code: TIMER0 alarm 0 interrupts core0 2000 times a second and counts the
//     interrupted program counter per 16 bytes of code; the report lists the busiest
//     addresses, which the ELF file maps to functions (arm-none-eabi-addr2line -f).

#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/structs/xip_ctrl.h"

#include "dx_prof.h"

static const char *const zone_names[DXP_COUNT] = {
	"other", "logic", "view", "draw", "copy", "vsync", "audio", "sd", "input", "flash"
};

static int current = DXP_OTHER;
static uint32_t since;
static uint32_t total_us[DXP_COUNT];

static int switch_to(int zone)
{
	const uint32_t now = time_us_32();
	const int previous = current;

	total_us[current] += now - since;
	since = now;
	current = zone;
	return previous;
}

int dx_prof_enter(int zone)
{
	return switch_to(zone);
}

void dx_prof_leave(int previous)
{
	switch_to(previous);
}

/*
==============================================================================

Program counter sampling

==============================================================================
*/

#define SAMPLE_ALARM 0      // TIMER0 alarm 0 (dx_usb.cpp has 1, dx_usb_frame.c 2, the SDK 3)
#define SAMPLE_US 500
#define PC_SLOTS 2048       // open addressing, in PSRAM (no SRAM to spare)
#define PC_PROBES 16
#define PC_TOP 40

typedef struct {
	uint32_t key; // pc / 16
	uint32_t count;
} pc_slot_t;

static pc_slot_t pc_table[PC_SLOTS] __attribute__((section(".psram_bss.dx_prof")));
static uint32_t pc_samples, pc_lost, sample_next;

// From the trampoline below with the interrupted exception frame (pc at word 6). Flash
// writes mask this interrupt, so it never touches PSRAM while XIP is off.
void __not_in_flash_func(dx_prof_sample)(const uint32_t *frame)
{
	timer_hw->intr = 1u << SAMPLE_ALARM;
	sample_next += SAMPLE_US;
	const uint32_t now = timer_hw->timerawl;
	if ((int32_t)(sample_next - now) < 20) // fell behind (interrupts were masked): resynchronise
		sample_next = now + SAMPLE_US;
	timer_hw->alarm[SAMPLE_ALARM] = sample_next;

	const uint32_t key = frame[6] >> 4;
	const uint32_t h = (key * 2654435761u) >> 21; // 11 bits: PC_SLOTS
	pc_samples++;
	for (uint32_t i = 0; i < PC_PROBES; i++) {
		pc_slot_t *s = &pc_table[(h + i) & (PC_SLOTS - 1)];
		if (s->count == 0)
			s->key = key;
		if (s->key == key) {
			s->count++;
			return;
		}
	}
	pc_lost++;
}

__attribute__((naked)) static void __not_in_flash_func(sample_isr)(void)
{
	__asm volatile(
	    "tst lr, #4\n"
	    "ite eq\n"
	    "mrseq r0, msp\n"
	    "mrsne r0, psp\n"
	    "b dx_prof_sample\n");
}

void dx_prof_start(void)
{
	const unsigned irq = hardware_alarm_get_irq_num(SAMPLE_ALARM);

	hardware_alarm_claim(SAMPLE_ALARM);
	irq_set_exclusive_handler(irq, sample_isr);
	irq_set_priority(irq, PICO_LOWEST_IRQ_PRIORITY);
	hw_set_bits(&timer_hw->inte, 1u << SAMPLE_ALARM);
	irq_set_enabled(irq, true);
	sample_next = timer_hw->timerawl + SAMPLE_US;
	timer_hw->alarm[SAMPLE_ALARM] = sample_next;
}

// Prints the busiest code addresses since the last report, and clears the counts. One
// pass over the table, which is in PSRAM (a pass per address held core0 for some 25 ms).
static void report_pcs(void)
{
	const unsigned irq = hardware_alarm_get_irq_num(SAMPLE_ALARM);
	const uint32_t samples = pc_samples;
	const uint32_t lost = pc_lost;
	pc_slot_t top[PC_TOP]; // busiest first
	int found = 0;

	if (samples == 0)
		return;
	irq_set_enabled(irq, false);
	for (int i = 0; i < PC_SLOTS; i++) {
		const pc_slot_t s = pc_table[i];
		if (s.count == 0)
			continue;
		pc_table[i].count = 0;
		if (found == PC_TOP && s.count <= top[PC_TOP - 1].count)
			continue;
		int n = found < PC_TOP ? found++ : PC_TOP - 1;
		for (; n > 0 && top[n - 1].count < s.count; n--)
			top[n] = top[n - 1];
		top[n] = s;
	}
	pc_samples = 0;
	pc_lost = 0;
	irq_set_enabled(irq, true);

	printf("hot code (%lu samples, %lu lost):", (unsigned long)samples, (unsigned long)lost);
	for (int n = 0; n < found; n++)
		printf("%s%08lx:%lu", n % 8 ? " " : "\n ", (unsigned long)(top[n].key << 4), (unsigned long)top[n].count);
	printf("\n");
}

/*
==============================================================================

Report

==============================================================================
*/

// Prints the time per frame of each zone since the last report, and clears it; then the
// busiest code addresses.
void dx_prof_report(uint32_t frames)
{
	switch_to(current);
	if (frames == 0)
		frames = 1;
	printf("time per frame (ms):");
	for (int z = 0; z < DXP_COUNT; z++) {
		const uint32_t tenths = (uint32_t)((uint64_t)total_us[z] / 100 / frames);
		printf(" %s %lu.%lu", zone_names[z], (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
		total_us[z] = 0;
	}

	// saturating 32-bit counters; writing clears them
	const uint32_t hits = xip_ctrl_hw->ctr_hit;
	const uint32_t accesses = xip_ctrl_hw->ctr_acc;
	xip_ctrl_hw->ctr_hit = 0;
	xip_ctrl_hw->ctr_acc = 0;
	printf(", XIP cache %lu%% hits of %lu M accesses\n",
	    (unsigned long)(accesses ? (uint64_t)hits * 100 / accesses : 0), (unsigned long)(accesses / 1000000));
	report_pcs();
}
