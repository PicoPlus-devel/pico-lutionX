# Size probe: links the complete engine (DevilutionX, its libraries, the SDL 1.2 subset and
# the heap) for the RP2350 against a stub platform, to measure code size and static data
# before the firmware exists. Built by tools/arm_size.sh.
#
# The linker map gets a RAM region of 8 MiB because on the board the engine's static data
# goes to PSRAM (as in pico-quake2); the probe only measures sizes and is never flashed.
cmake_minimum_required(VERSION 3.13)
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_BOARD pico2)
include(pico_sdk_import.cmake)
project(dx_sizeprobe C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(PICO_CXX_ENABLE_EXCEPTIONS 0)
set(PICO_CXX_ENABLE_RTTI 0)
pico_sdk_init()

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/devilutionx.cmake)

option(DX_PROBE_LTO "Link-time optimisation" OFF)

set(SDL_SOURCES
  ${PICO_LUTIONX_ROOT}/src/sdl/sdl_core.cpp
  ${PICO_LUTIONX_ROOT}/src/sdl/sdl_events.cpp
  ${PICO_LUTIONX_ROOT}/src/sdl/sdl_rwops.cpp
  ${PICO_LUTIONX_ROOT}/src/sdl/sdl_video.cpp
)

add_executable(dx_sizeprobe
  ${CMAKE_CURRENT_LIST_DIR}/probe_main.cpp
  ${DX_ENGINE_SOURCES} ${DX_DEP_C_SOURCES} ${DX_DEP_CXX_SOURCES} ${SDL_SOURCES}
  ${PICO_LUTIONX_ROOT}/src/port/dx_heap.cpp
  ${PICO_LUTIONX_ROOT}/3rdparty/tlsf/tlsf.c)
target_include_directories(dx_sizeprobe PRIVATE ${DX_INCLUDE_DIRS} ${PICO_LUTIONX_ROOT}/src/sdl ${PICO_LUTIONX_ROOT}/3rdparty/tlsf)
target_compile_definitions(dx_sizeprobe PRIVATE ${DX_DEFINITIONS})
target_compile_options(dx_sizeprobe PRIVATE -ffunction-sections -fdata-sections -fstack-usage
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions> -Wno-deprecated-declarations)
if(DX_PROBE_LTO)
  target_compile_options(dx_sizeprobe PRIVATE -flto)
  target_link_options(dx_sizeprobe PRIVATE -flto)
endif()
target_link_libraries(dx_sizeprobe pico_stdlib)
# The engine heap (dx_heap.cpp) provides operator new/delete.
target_compile_definitions(dx_sizeprobe PRIVATE PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1)
target_link_options(dx_sizeprobe PRIVATE -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/dx_sizeprobe.map)

file(READ ${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld _ld)
string(REPLACE "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k" "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 8192k" _ld "${_ld}")
string(REPLACE "SCRATCH_X(rwx) : ORIGIN = 0x20080000" "SCRATCH_X(rwx) : ORIGIN = 0x20800000" _ld "${_ld}")
string(REPLACE "SCRATCH_Y(rwx) : ORIGIN = 0x20081000" "SCRATCH_Y(rwx) : ORIGIN = 0x20801000" _ld "${_ld}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/memmap_probe.ld "${_ld}")
pico_set_linker_script(dx_sizeprobe ${CMAKE_CURRENT_BINARY_DIR}/memmap_probe.ld)
