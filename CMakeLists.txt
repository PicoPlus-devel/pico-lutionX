# pico-lutionX -- DevilutionX (Diablo) for RP2350 boards with HSTX video, 8 MB PSRAM,
# 16 MB flash and an SD card.
#
# The engine (DevilutionX 1.5.5 in 3rdparty/devilutionX, libdx_engine.a) is built by
# cmake/dx_engine.cmake; the RP2350 platform layer and the link step are in src/pico.
# The structure follows pico-quake2. The Linux host harness is in hosttest/.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Board selection --------------------------------------------------------
# DX_BOARD names the <tag>_cflags.h header that is force-included into every
# translation unit; everything board-specific comes from that header.
#
#   fruitjam      Adafruit Fruit Jam   (HW_CONFIG 8, PIO-USB, TLV320)
#   adafruitdvisd Pico Plus 2 + Adafruit DVI and SD breakouts
#                                      (HW_CONFIG 2, PIO-USB, PCM5100A, NES/SNES ports)
#
set(DX_BOARD "fruitjam" CACHE STRING "Board tag: selects <tag>_cflags.h")
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${DX_BOARD}_cflags.h")
    message(FATAL_ERROR "DX_BOARD=${DX_BOARD} but ${DX_BOARD}_cflags.h does not exist.")
endif()
message(STATUS "pico-lutionX board: ${DX_BOARD}")

# Version stamp recorded in the UF2's binary info (the release build passes the tag).
set(DX_RELEASE_VERSION "dev" CACHE STRING "Version stamp recorded in the UF2 binary info")

# --- Defaults, so a plain `cmake -S . -B build` works -------------------------
if (NOT PICO_BOARD)
    set(PICO_BOARD adafruit_fruit_jam CACHE STRING "Target board")
endif()
if (NOT PICO_PLATFORM)
    set(PICO_PLATFORM rp2350-arm-s CACHE STRING "RP2350 ARM secure")
endif()
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()

if (NOT PICO_SDK_PATH)
    if (DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    else()
        message(FATAL_ERROR "Set PICO_SDK_PATH (env var or -DPICO_SDK_PATH=...)")
    endif()
endif()
get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH)
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

# Pico-PIO-USB (USB host via PIO). The SDK's tinyusb reads this variable in
# check_and_add_pico_pio_usb_support().
if (NOT PICO_PIO_USB_PATH AND DEFINED ENV{PICO_PIO_USB_PATH})
    set(PICO_PIO_USB_PATH $ENV{PICO_PIO_USB_PATH})
endif()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(pico_lutionx C CXX ASM)

# Bootloader variant: link into the pico-bootLoader app partition (see
# cmake/BootPartition.cmake). Visible to C as BUILD_FOR_BOOTLOADER.
option(BUILD_FOR_BOOTLOADER "Link for the pico-bootLoader app partition" OFF)
if (BUILD_FOR_BOOTLOADER)
    add_compile_definitions(BUILD_FOR_BOOTLOADER=1)
endif()
include(cmake/BootPartition.cmake)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(PICO_CXX_ENABLE_EXCEPTIONS 0)
set(PICO_CXX_ENABLE_RTTI 0)

# Force-include the board header into every C/C++ translation unit (not ASM).
set(DX_BOARD_CFLAGS "${CMAKE_CURRENT_SOURCE_DIR}/${DX_BOARD}_cflags.h")
if (NOT CMAKE_C_FLAGS MATCHES "${DX_BOARD}_cflags.h")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${DX_BOARD_CFLAGS} -g3 -ggdb")
endif()
if (NOT CMAKE_CXX_FLAGS MATCHES "${DX_BOARD}_cflags.h")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${DX_BOARD_CFLAGS} -g3 -ggdb")
endif()

pico_sdk_init()

# --- Vendored pico_shared drivers (3rdparty/pico_shared_drivers) -------------
# i2c_bus_recovery before tlv320dac3100, which links it
add_subdirectory(3rdparty/pico_shared_drivers/drivers/i2c_bus_recovery)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/tlv320dac3100)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_audio_i2s)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_hdmi)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_fatfs)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_psram)

# USB host input (keyboard, mouse, gamepads incl. XInput) on Pico-PIO-USB. The call
# inside tinyusb's family.cmake ran before pico_sdk_init() and did nothing; this one
# creates tinyusb_pico_pio_usb.
check_and_add_pico_pio_usb_support()
if (NOT TARGET tinyusb_pico_pio_usb)
    message(FATAL_ERROR "tinyusb_pico_pio_usb is missing: set PICO_PIO_USB_PATH (now '${PICO_PIO_USB_PATH}')")
endif()
add_subdirectory(3rdparty/tusb_xinput)
add_subdirectory(3rdparty/pico_shared_drivers/usb_hid)
# NES/SNES controller ports. Linked on every board: dx_nespad.cpp only uses it when the
# board header defines NES_PIN_CLK, and the linker drops it otherwise.
add_subdirectory(3rdparty/pico_shared_drivers/nespad)
# Wii extension pad (Fruit Jam); dx_wiipad.cpp compiles to nothing without WII_PIN_SDA.
add_subdirectory(3rdparty/pico_shared_drivers/wiipad)

include(cmake/dx_engine.cmake)

add_subdirectory(src/pico)
