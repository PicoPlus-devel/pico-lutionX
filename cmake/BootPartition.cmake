# BootPartition.cmake -- flash map of pico-lutionX (adapted from pico-quake2).
#
# 16 MB boards:
#
#   0x10000000  pico-bootLoader (bootloader builds)          512 KB
#   0x10080000  application (bootloader builds)              2.5 MB   DX_APP_BASE / DX_APP_SIZE
#               (standalone builds start at 0x10000000 and may use up to 3 MB)
#   0x10300000  flash asset cache                             13 MB   DX_FCACHE_BASE / DX_FCACHE_SIZE
#   0x11000000  end of flash (also the PSRAM XIP window on QMI CS1)
#
# The application must end below DX_FCACHE_BASE in both variants; the linker script's
# FLASH region is sized so that it does (cmake/psram_linker.cmake). The firmware is about
# 2 MB (-O2, with sound).
#
# NOTE: this file does not call pico_set_linker_script. cmake/psram_linker.cmake owns
# the generated linker script and applies the FLASH region from here.

if(DEFINED _DX_BOOTPARTITION_INCLUDED)
    return()
endif()
set(_DX_BOOTPARTITION_INCLUDED 1)

set(DX_XIP_BASE     "0x10000000" CACHE STRING "RP2350 XIP flash base")
set(DX_APP_BASE     "0x10080000" CACHE STRING "Bootloader app-partition base")
set(DX_FLASH_TOTAL  "0x1000000"  CACHE STRING "Total external flash on the board")
set(DX_FCACHE_BASE  "0x10300000" CACHE STRING "Start of the flash asset cache (XIP address)")
math(EXPR DX_FCACHE_SIZE "${DX_XIP_BASE} + ${DX_FLASH_TOTAL} - ${DX_FCACHE_BASE}" OUTPUT_FORMAT HEXADECIMAL)
if (BUILD_FOR_BOOTLOADER)
    math(EXPR DX_APP_SIZE "${DX_FCACHE_BASE} - ${DX_APP_BASE}" OUTPUT_FORMAT HEXADECIMAL)
else()
    math(EXPR DX_APP_SIZE "${DX_FCACHE_BASE} - ${DX_XIP_BASE}" OUTPUT_FORMAT HEXADECIMAL)
endif()
message(STATUS "pico-lutionX flash: app ${DX_APP_SIZE} bytes, asset cache ${DX_FCACHE_BASE} + ${DX_FCACHE_SIZE}")

# Every build claims the full 16 MB CS0 window (see pico-quake2's BootPartition.cmake:
# the pico2 board header understates the Pico Plus 2's flash, and the SDK's flash
# erase/program asserts check against this value).
add_compile_definitions(PICO_FLASH_SIZE_BYTES=${DX_FLASH_TOTAL})

# Target properties that go with a relinked (bootloader) image.
function(dx_bootloader_target_props TARGET)
    # picotool >= 2.2.0 assumes RP2040 for images that do not start at 0x10000000.
    if(PICO_PLATFORM MATCHES "rp2350")
        find_program(_dx_picotool picotool)
        if(_dx_picotool)
            execute_process(COMMAND ${_dx_picotool} version --semantic
                            OUTPUT_VARIABLE _dx_pt_ver
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_dx_pt_ver VERSION_GREATER_EQUAL 2.2.0)
                set_target_properties(${TARGET} PROPERTIES
                    PICOTOOL_EXTRA_UF2_ARGS "--platform;rp2350")
            endif()
        endif()
    endif()
endfunction()
