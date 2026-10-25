# The pico-lutionX firmware image: libdx_engine.a (cmake/dx_engine.cmake) plus the
# RP2350 platform layer in this directory. cmake/psram_linker.cmake generates the linker
# script that places the engine's data in PSRAM.

include(${CMAKE_SOURCE_DIR}/cmake/psram_linker.cmake)

add_executable(pico_lutionx
    dx_audio.c
    dx_backend.cpp
    dx_boot.c
    dx_console.c
    dx_display.c
    dx_fatal.c
    dx_fatfs_io.c
    dx_flash.c
    dx_input.cpp
    dx_nespad.cpp
    dx_prof.c
    dx_usb.cpp
    dx_usb_frame.c
    dx_wiipad.cpp
    # the engine heap (PSRAM), shared with the host harness
    ${CMAKE_SOURCE_DIR}/src/port/dx_heap.cpp
    ${CMAKE_SOURCE_DIR}/3rdparty/tlsf/tlsf.c
)

target_include_directories(pico_lutionx PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/src/port
    ${CMAKE_SOURCE_DIR}/src/aulib/include
    ${CMAKE_SOURCE_DIR}/3rdparty/tlsf
    ${CMAKE_SOURCE_DIR}/3rdparty/pico_shared_drivers/fonts
)

# PICO_STACK_SIZE: core0's stack (cmake/psram_linker.cmake puts it in its own 16 KB
# region). Core1 is launched with its own stack array, so the SDK reserves none for it.
# DX_CPU_KHZ: 378000 (default) or 504000; DX_VREG: core voltage for it.
set(DX_CPU_KHZ "378000" CACHE STRING "System clock in kHz (a multiple of 126000)")
if (DX_CPU_KHZ GREATER 378000)
    set(_dx_vreg_default VREG_VOLTAGE_1_70)		# as the emulators (pico_shared) at 504 MHz
else()
    set(_dx_vreg_default VREG_VOLTAGE_1_50)
endif()
set(DX_VREG "${_dx_vreg_default}" CACHE STRING "Core voltage (VREG_VOLTAGE_*)")
message(STATUS "pico-lutionX clock: ${DX_CPU_KHZ} kHz at ${DX_VREG}")

# DX_COPY_ON_CORE1: copy each frame on core1 instead of core0 (dx_display.c; no gain measured)
option(DX_COPY_ON_CORE1 "Copy the frame to the front buffer on core1" OFF)

# DX_PERF_LOG: a status line with the frame rate, memory and the time per frame of each part
# of the game on the UART every 5 seconds (dx_boot.c, dx_prof.c). Off by default: printing
# waits for the UART, and the report made the sound stutter.
option(DX_PERF_LOG "Status and performance report on the UART every 5 seconds" OFF)

target_compile_definitions(pico_lutionx PRIVATE
    DX_CPU_KHZ=${DX_CPU_KHZ}
    DX_VREG=${DX_VREG}
    DX_COPY_ON_CORE1=$<BOOL:${DX_COPY_ON_CORE1}>
    DX_PERF_LOG=$<BOOL:${DX_PERF_LOG}>
    PICO_STACK_SIZE=16384
    PICO_CORE1_STACK_SIZE=0
    DX_FCACHE_BASE=${DX_FCACHE_BASE}
    DX_FCACHE_SIZE=${DX_FCACHE_SIZE}
    # dx_heap.cpp provides operator new/delete (the PSRAM heap)
    PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1
)

target_link_libraries(pico_lutionx PRIVATE
    dx_engine
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_pll
    hardware_vreg
    hardware_dma
    hardware_pio
    hardware_spi
    hardware_flash
    hardware_xip_cache
    hardware_sync
    pico_hdmi
    pico_audio_i2s_shared
    tlv320dac3100
    pico_fatfs
    pico_psram
    # USB host input: TinyUSB reads tusb_config.h from this directory
    pico_shared_usb_hid
    tinyusb_host
    tinyusb_pico_pio_usb
    # NES/SNES controller ports (dx_nespad.cpp; unused without NES_PIN_CLK)
    pico_shared_nespad
    # Wii extension pad (dx_wiipad.cpp; unused without WII_PIN_SDA)
    pico_shared_wiipad
    i2c_bus_recovery
)

pico_enable_stdio_uart(pico_lutionx 1)
pico_enable_stdio_usb(pico_lutionx 0)

dx_use_psram_linker_script(pico_lutionx)

pico_set_program_name(pico_lutionx "pico-lutionX")
pico_set_program_version(pico_lutionx "${DX_RELEASE_VERSION}")

pico_add_extra_outputs(pico_lutionx)
