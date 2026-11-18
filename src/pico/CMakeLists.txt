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
    dx_settings.c
    dx_usb.cpp
    dx_usb_frame.c
    pico_bootcfg.c
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
# DX_CPU_KHZ: 384000 (default) or 504000, see dx_boot.c; DX_VREG: core voltage for it.
set(DX_CPU_KHZ "384000" CACHE STRING "System clock in kHz (a multiple of 48000, or 126000, 252000 or 504000)")
if (DX_CPU_KHZ GREATER 384000)
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

# DX_HOT_CODE: with DX_PERF_LOG, sample core0's program counter 2000 times a second and add
# the busiest code addresses to each report (dx_prof.c). Off by default: the list takes
# some 45 ms to print.
option(DX_HOT_CODE "Hot-code list in the DX_PERF_LOG report" OFF)

target_compile_definitions(pico_lutionx PRIVATE
    DX_CPU_KHZ=${DX_CPU_KHZ}
    DX_VREG=${DX_VREG}
    DX_COPY_ON_CORE1=$<BOOL:${DX_COPY_ON_CORE1}>
    DX_PERF_LOG=$<BOOL:${DX_PERF_LOG}>
    DX_HOT_CODE=$<BOOL:${DX_HOT_CODE}>
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

# FatFs's calls into the SD driver go through dx_fatfs_io.c (it holds the USB timer off)
target_link_options(pico_lutionx PRIVATE -Wl,--wrap=disk_read,--wrap=disk_write,--wrap=disk_ioctl)

pico_enable_stdio_uart(pico_lutionx 1)
pico_enable_stdio_usb(pico_lutionx 0)

dx_use_psram_linker_script(pico_lutionx)

# Pico SDK 2.3 adds two small SRAM users that this port does not need; both
# options are unknown to, and ignored by, older SDKs.
#   PICO_THREAD_LOCAL_CORE1_REINITIALIZE=0: core 1's thread-local variables are
#     not reset when core 1 is launched again. Core 1 is launched once here, and
#     the only thread_local is devilutionX's dwLastError, an error code (saves 8
#     bytes on Arm).
#   PICO_SYNC_EXCLUSIVE_ACCESS_EVENT_WORKAROUND=0: RP2350 mutex waits keep
#     spinning as with SDK 2.2 instead of sleeping in __wfe; this only affects
#     power use, and the workaround's RAM code costs 28 (Arm) / 72 (RISC-V) bytes.
target_compile_definitions(pico_lutionx PRIVATE
    PICO_THREAD_LOCAL_CORE1_REINITIALIZE=0
    PICO_SYNC_EXCLUSIVE_ACCESS_EVENT_WORKAROUND=0
)

# Pico SDK 2.3 brings its own __cxa_guard_acquire, the function that guards the
# first use of a C++ function-local static. With pico_multicore linked it claims
# the guard with an atomic compare-and-swap (LDREX/STREX). The linker script puts
# the engine's .bss, guard variables included, in PSRAM, where exclusive
# accesses are not supported: the store lands, the STREX reports failure, and
# the core then waits forever for "another" initializer (seen in devilutionX's
# GetIni(), which hung right after USB start-up). With this option the SDK
# checks the guard with plain loads and stores, as libstdc++ did with SDK 2.2.
# C++ code only runs on core 0 here (core 1 runs the C display loop). Unknown
# to, and ignored by, older SDKs.
target_compile_definitions(pico_lutionx PRIVATE
    PICO_CXX_THREAD_SAFE_STATIC_INIT=0
)

pico_set_program_name(pico_lutionx "pico-lutionX")
pico_set_program_version(pico_lutionx "${DX_RELEASE_VERSION}")

pico_add_extra_outputs(pico_lutionx)
