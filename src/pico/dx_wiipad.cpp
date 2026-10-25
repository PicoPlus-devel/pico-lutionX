// dx_wiipad.cpp -- Wii extension pad (Classic Controller and compatibles) on the Fruit Jam's
// I2C port, which it shares with the TLV320 codec. From pico-duke3D's duke_wiipad.cpp.
//
// Read in thread context (dx_input_poll), like the codec's headphone polling
// (dx_audio_update), so the two never interleave on the bus. Returns
// io::GamePadState::Button bits.
#include "pico.h" // PICO_ON_DEVICE, before the test below

#include "dx_input.h"

#if PICO_ON_DEVICE && WII_PIN_SDA >= 0 && WII_PIN_SCL >= 0

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"

#include "wiipad.h"
#include "i2c_bus_recovery.h"
#include "audio_i2s.h"   // PICO_AUDIO_I2S_DRIVER_TLV320

#include "gamepad.h"

// Same default as dx_audio.c: the TLV320 codec unless the board header says otherwise.
#ifndef DX_AUDIO_I2S_DRIVER
#define DX_AUDIO_I2S_DRIVER PICO_AUDIO_I2S_DRIVER_TLV320
#endif

// The TLV320 board (Fruit Jam) puts the codec on the very same SDA/SCL as the
// Wii extension port, and the codec has a reset pin we can use to keep it out of
// the way while the pad is brought up.
#if DX_AUDIO_I2S_DRIVER == PICO_AUDIO_I2S_DRIVER_TLV320 && \
    defined(PICO_AUDIO_I2S_RESET_PIN) && PICO_AUDIO_I2S_RESET_PIN >= 0
#define WIIPAD_SHARES_DAC_BUS 1
#else
#define WIIPAD_SHARES_DAC_BUS 0
#endif

namespace
{
    // wiipad_read() is blocking and not cheap: a register-pointer write, a
    // 200 us settle sleep and a 6-byte read, ~400 us in all. The engine polls for
    // events many times per frame and runs its logic at 20 Hz, so 10 ms between
    // reads loses nothing.
    constexpr uint64_t MIN_POLL_US = 10000;

    // A pad plugged in after boot (or one that was not ready in time — the
    // SNES-Classic pads are slow to wake) gets picked up by retrying the
    // handshake. A failed probe NACKs out in ~300 us so this is close to free; a
    // *successful* one costs ~200 ms once, from the two sleep_ms(100) inside
    // wiipad_begin(). One hitch at plug-in time is a fair price for hot-plug.
    constexpr uint64_t RECONNECT_US = 1000000;
}

void dx_wiipad_init(void)
{
#if WIIPAD_SHARES_DAC_BUS
    // Mirrors initVintageControllers() in pico-infonesPlus
    // (pico_shared/FrensHelpers.cpp). An uninitialised SNES-Classic pad attached
    // at power-on wedges the shared bus outright, and every DAC transaction then
    // times out. Clear the bus, give an attached pad its extension-init so it
    // goes quiet, and clear again before handing over. The DAC is held in reset
    // meanwhile so it cannot observe the pad traffic; tlv320_hardware_reset()
    // releases it later, from inside audio_i2s_setup().
    gpio_init(PICO_AUDIO_I2S_RESET_PIN);
    gpio_put(PICO_AUDIO_I2S_RESET_PIN, 0);
    gpio_set_dir(PICO_AUDIO_I2S_RESET_PIN, GPIO_OUT);
    i2c_bus_clear(WII_PIN_SDA, WII_PIN_SCL, "pad-preinit");
    wiipad_begin(); // fast NACK, no delays, when no pad is attached
    i2c_bus_clear(WII_PIN_SDA, WII_PIN_SCL, "pre-DAC");
#else
    wiipad_begin();
#endif
}

static uint16_t wiipad_read_raw(void)
{
    static uint16_t last = 0;
    static uint64_t last_poll = 0;

    const uint64_t now = time_us_64();
    if (now - last_poll < MIN_POLL_US)
        return last;
    last_poll = now;

    if (!wiipad_is_connected())
    {
        static uint64_t last_try = 0;
        last = 0;
        if (now - last_try < RECONNECT_US)
            return 0;
        last_try = now;
        // Re-running i2c_init() on a bus the codec shares is safe: the only other
        // user is the headphone poll in dx_audio_update(), which runs on core0 in
        // thread context as this does, so the two can never interleave. Core1 only
        // pumps audio samples; it never touches I2C.
        wiipad_begin();
        if (!wiipad_is_connected())
            return 0;
    }

    last = wiipad_read();
    return last;
}

void dx_wiipad_shutdown(void)
{
    // Only when a pad actually answered: wiipad_end() deinits the I2C instance,
    // which on the Fruit Jam is the codec's too.
    if (wiipad_is_connected())
        wiipad_end();
}

// wiipad_read(): bit0=A, 1=B, 2=Select, 3=Start, 4-7 up/down/left/right, 8=X, 9=Y,
// 10=L, 11=R. A and B are the right and bottom buttons, as io A and io B.
uint32_t dx_wiipad_buttons(void)
{
    using Button = io::GamePadState::Button;
    static const uint32_t Map[12] = {
        Button::A, Button::B, Button::SELECT, Button::START,
        (uint32_t)Button::UP, (uint32_t)Button::DOWN, (uint32_t)Button::LEFT, (uint32_t)Button::RIGHT,
        Button::X, Button::Y, Button::L, Button::R,
    };
    uint16_t w = wiipad_read_raw();
    uint32_t b = 0;
    for (int i = 0; i < 12; i++)
        if (w & (1u << i))
            b |= Map[i];
    return b;
}

#else

void dx_wiipad_init(void)
{
}

uint32_t dx_wiipad_buttons(void)
{
    return 0;
}

void dx_wiipad_shutdown(void)
{
}

#endif // PICO_ON_DEVICE && WII_PIN_SDA >= 0 && WII_PIN_SCL >= 0
