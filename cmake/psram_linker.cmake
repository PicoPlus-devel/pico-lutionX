# Generates the linker script for pico-lutionX from the SDK's memmap_default.ld.
# Adapted from pico-quake2's cmake/psram_linker.cmake (itself from pico-duke3D). One
# generator owns all patches, because pico_set_linker_script can only be applied once
# per target.
#
# Memory layout:
#
#   FLASH   the application, capped so it ends below the flash asset cache
#           (cmake/BootPartition.cmake), in both the standalone and bootloader builds
#   RAM     SDK, drivers and the platform layer (including the 300 KB front buffer),
#           plus engine objects explicitly placed in SRAM (DX_SRAM_BSS -> .sram_bss,
#           hot code -> .time_critical.*)
#   STACK   core0's stack, carved off the top of SRAM (core1 gets its own array)
#   PSRAM   all of libdx_engine.a's .bss/COMMON (.psram_bss) and .data (.psram_data),
#           then the engine heap (TLSF, src/port/dx_heap.cpp) up to the end of the chip
#
# .psram_bss/.psram_data are only valid after SetupPsram() maps the PSRAM window:
# src/pico/dx_boot.c zeroes and copies them from a runtime-init hook that runs before
# the C++ static constructors, which already use engine globals and the heap.

set(DX_STACK_REGION "16k" CACHE STRING "SRAM reserved at the top for core0's stack")
set(DX_PSRAM_SIZE "8192k" CACHE STRING "PSRAM region size in the linker script")

function(dx_use_psram_linker_script target)
    set(_src "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld")
    set(_dst "${CMAKE_BINARY_DIR}/memmap_lutionx.ld")
    file(READ "${_src}" _ld)

    # 0) FLASH region: start of the image and a cap below the asset cache.
    if (BUILD_FOR_BOOTLOADER)
        set(_origin "${DX_APP_BASE}")
        dx_bootloader_target_props(${target})
    else()
        set(_origin "${DX_XIP_BASE}")
    endif()
    string(REPLACE
        "INCLUDE \"pico_flash_region.ld\""
        "FLASH(rx) : ORIGIN = ${_origin}, LENGTH = ${DX_APP_SIZE}"
        _ld "${_ld}")
    string(REGEX REPLACE
        "FLASH\\(rx\\)[ \t]*:[^\n]*"
        "FLASH(rx) : ORIGIN = ${_origin}, LENGTH = ${DX_APP_SIZE}"
        _ld "${_ld}")
    if (NOT _ld MATCHES "ORIGIN = ${_origin}, LENGTH = ${DX_APP_SIZE}")
        message(FATAL_ERROR "psram_linker: could not set the FLASH region in ${_src}")
    endif()
    message(STATUS "psram_linker: ${target} FLASH ORIGIN=${_origin} LENGTH=${DX_APP_SIZE}")

    # 1) core0's stack -> a dedicated STACK region at the top of SRAM (out of the
    #    4 KB SCRATCH_Y bank, and outside both the .bss range crt0 zeroes and the
    #    heap's end..__StackLimit range).
    string(REPLACE
        "    .stack1_dummy (NOLOAD):\n    {\n        *(.stack1*)\n    } > SCRATCH_X"
        "    .stack1_dummy (NOLOAD):\n    {\n        *(.stack1*)\n    } > STACK"
        _ld "${_ld}")
    string(REPLACE
        "    .stack_dummy (NOLOAD):\n    {\n        KEEP(*(.stack*))\n    } > SCRATCH_Y"
        "    .stack_dummy (NOLOAD):\n    {\n        KEEP(*(.stack*))\n    } > STACK"
        _ld "${_ld}")
    string(REPLACE
        "__StackOneTop = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);"
        "__StackOneTop = ADDR(.stack1_dummy) + SIZEOF(.stack1_dummy);"
        _ld "${_ld}")
    string(REPLACE
        "__StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);"
        "__StackTop = ADDR(.stack_dummy) + SIZEOF(.stack_dummy);"
        _ld "${_ld}")
    if (NOT _ld MATCHES "ADDR\\(\\.stack_dummy\\)")
        message(FATAL_ERROR "psram_linker: could not move the stack out of SCRATCH_Y in ${_src}")
    endif()

    # 2) MEMORY: shorten RAM by the stack region, add STACK and PSRAM (one
    #    replacement: all three match the same RAM line).
    string(REPLACE
        "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k"
        "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k - ${DX_STACK_REGION}
    STACK(rw) : ORIGIN = 0x20080000 - ${DX_STACK_REGION}, LENGTH = ${DX_STACK_REGION}
    PSRAM(rwx) : ORIGIN = 0x11000000, LENGTH = ${DX_PSRAM_SIZE}"
        _ld "${_ld}")
    if (NOT _ld MATCHES "STACK\\(rw\\)")
        message(FATAL_ERROR "psram_linker: MEMORY rewrite failed in ${_src}")
    endif()

    # 3) libdx_engine.a's initialised data -> PSRAM, loaded from flash (copied by dx_boot.c). Placed before the
    #    SDK's .data so its *(.data*) does not claim these input sections first (ld
    #    places each input section at the first output section that matches).
    string(REPLACE "    .data : {"
"    .psram_data : {
        . = ALIGN(4);
        __psram_data_start = .;
        *libdx_engine.a:*(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        __psram_data_end = .;
    } > PSRAM AT> FLASH
    __psram_data_load = LOADADDR(.psram_data);

    .data : {" _ld "${_ld}")
    if (NOT _ld MATCHES "__psram_data_load")
        message(FATAL_ERROR "psram_linker: could not add .psram_data to ${_src}")
    endif()

    # 4) Engine objects placed in SRAM on purpose (DX_SRAM_BSS), then libdx_engine.a's
    #    .bss/COMMON -> PSRAM. .sram_bss lies between .tbss (__bss_start__) and
    #    .bss (__bss_end__), so crt0 zeroes it; .psram_bss is outside that range and
    #    dx_boot.c zeroes it once PSRAM is mapped.
    string(REPLACE "    .bss (NOLOAD) : {"
"    .sram_bss (NOLOAD) : {
        . = ALIGN(8);
        __sram_bss_start = .;
        *(.sram_bss .sram_bss.*)
        . = ALIGN(8);
        __sram_bss_end = .;
    } > RAM

    .psram_bss (NOLOAD) : {
        . = ALIGN(8);
        __psram_bss_start = .;
        *(.psram_bss.*)
        *libdx_engine.a:*(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(32);
        __psram_bss_end = .;
        __psram_heap_start = .;
    } > PSRAM
    __psram_heap_end = ORIGIN(PSRAM) + LENGTH(PSRAM);

    .bss (NOLOAD) : {" _ld "${_ld}")
    if (NOT _ld MATCHES "__psram_bss_start")
        message(FATAL_ERROR "psram_linker: could not add .psram_bss to ${_src}")
    endif()

    # 5) newlib's memcpy/memset/strcmp in SRAM. The SDK script already keeps
    #    *libc.a:*lib_a-mem*.o out of flash so that .data picks them up, but this
    #    toolchain names the objects libc_a-*.o inside libg.a, which the pattern
    #    misses. (In pico-quake2 memcpy alone was 1.4% of a frame from flash.)
    string(REPLACE "*libc.a:*lib_a-mem*.o"
        "*libc.a:*lib_a-mem*.o *libc.a:*libc_a-mem*.o *libg.a:*libc_a-mem*.o *libc.a:*libc_a-strcmp.o *libg.a:*libc_a-strcmp.o"
        _ld "${_ld}")
    if (NOT _ld MATCHES "libg\\.a:\\*libc_a-mem")
        message(FATAL_ERROR "psram_linker: could not move memcpy/memset to SRAM in ${_src}")
    endif()

    file(WRITE "${_dst}" "${_ld}")
    pico_set_linker_script(${target} "${_dst}")
endfunction()
