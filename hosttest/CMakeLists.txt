# Host harness: the engine as built for the board, on Linux (-m32 keeps pointer-heavy
# structures the size they have on the ILP32 Cortex-M33). Built by hosttest/build.sh.
cmake_minimum_required(VERSION 3.16)
project(dx_host C CXX)

option(DX_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" ON)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/devilutionx.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)

set(HOST_FLAGS -m32 -g -O1 -fno-omit-frame-pointer -fno-strict-aliasing -D_DEBUG)
if(DX_SANITIZE)
  list(APPEND HOST_FLAGS -fsanitize=address,undefined)
endif()

set(SDL_SOURCES
  ${PICO_LUTIONX_ROOT}/src/sdl/sdl_core.cpp
  ${PICO_LUTIONX_ROOT}/src/sdl/sdl_events.cpp
  ${PICO_LUTIONX_ROOT}/src/sdl/sdl_rwops.cpp
  ${PICO_LUTIONX_ROOT}/src/sdl/sdl_video.cpp
)

add_library(dx_engine OBJECT ${DX_ENGINE_SOURCES} ${DX_DEP_C_SOURCES} ${DX_DEP_CXX_SOURCES} ${SDL_SOURCES})
target_include_directories(dx_engine PUBLIC ${DX_INCLUDE_DIRS} ${PICO_LUTIONX_ROOT}/src/sdl ${PICO_LUTIONX_ROOT}/3rdparty/tlsf)
target_compile_definitions(dx_engine PUBLIC ${DX_DEFINITIONS} _POSIX_C_SOURCE=200809L _GNU_SOURCE)
target_compile_options(dx_engine PUBLIC ${HOST_FLAGS}
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions>
  -Wno-deprecated-declarations)

# Static data of the engine objects; the PSRAM pool of the harness gets the rest of 8 MiB.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/dx_static_bytes.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen
  COMMAND python3 -I ${PICO_LUTIONX_ROOT}/tools/static_bytes.py --header nm $<TARGET_OBJECTS:dx_engine> > ${CMAKE_CURRENT_BINARY_DIR}/gen/dx_static_bytes.h
  DEPENDS $<TARGET_OBJECTS:dx_engine>
  COMMAND_EXPAND_LISTS
  VERBATIM)

# SD card mode (DX_SDCARD=<card image>, sdcard/sd_host.c): the board's file layer
# src/pico/dx_fatfs_io.c on the board's FatFs and ffconf.h, on a card image from dx_sdimg.
set(FATFS_DIR ${PICO_LUTIONX_ROOT}/3rdparty/pico_shared_drivers/drivers/pico_fatfs/fatfs)
set(SDCARD_DIR ${CMAKE_CURRENT_LIST_DIR}/sdcard)
set(FATFS_SOURCES ${FATFS_DIR}/ff.c ${FATFS_DIR}/ffsystem.c ${FATFS_DIR}/ffunicode.c)

add_library(dx_sdcard OBJECT
  ${FATFS_SOURCES}
  ${PICO_LUTIONX_ROOT}/src/pico/dx_fatfs_io.c
  ${SDCARD_DIR}/sd_disk.c
  ${SDCARD_DIR}/sd_host.c)
target_include_directories(dx_sdcard PRIVATE ${SDCARD_DIR}/shim ${SDCARD_DIR} ${FATFS_DIR} ${FATFS_DIR}/conf
  ${PICO_LUTIONX_ROOT}/src/pico ${PICO_LUTIONX_ROOT}/src/port)
target_compile_options(dx_sdcard PRIVATE ${HOST_FLAGS} -U_FORTIFY_SOURCE)
set_source_files_properties(${PICO_LUTIONX_ROOT}/src/pico/dx_fatfs_io.c PROPERTIES
  COMPILE_OPTIONS "-include;${SDCARD_DIR}/dx_fatfs_io_host.h")

add_executable(dx_host
  ${CMAKE_CURRENT_LIST_DIR}/dx_host.cpp
  ${PICO_LUTIONX_ROOT}/src/port/dx_heap.cpp
  ${PICO_LUTIONX_ROOT}/3rdparty/tlsf/tlsf.c
  ${CMAKE_CURRENT_BINARY_DIR}/gen/dx_static_bytes.h
  $<TARGET_OBJECTS:dx_engine>
  $<TARGET_OBJECTS:dx_sdcard>)
target_include_directories(dx_host PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen ${SDCARD_DIR} ${PICO_LUTIONX_ROOT}/src/pico)
target_compile_definitions(dx_host PRIVATE DX_HEAP_HOST)
target_link_libraries(dx_host PRIVATE dx_engine)
target_link_options(dx_host PRIVATE -m32 ${HOST_FLAGS}
  -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc,--wrap=memalign,--wrap=aligned_alloc,--wrap=posix_memalign
  -Wl,--wrap=fopen,--wrap=stat,--wrap=access,--wrap=mkdir,--wrap=remove,--wrap=rename,--wrap=truncate,--wrap=readlink
  -Wl,--wrap=disk_read,--wrap=disk_write,--wrap=disk_ioctl)
set_target_properties(dx_host PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
if(NOT DX_SANITIZE)
  set_target_properties(dx_host PROPERTIES OUTPUT_NAME dx_host_nosan)
endif()

# Card images for the SD card mode (sdcard/sd_mkimg.c): FatFs with the board's ffconf.h and f_mkfs.
add_executable(dx_sdimg ${SDCARD_DIR}/sd_mkimg.c ${SDCARD_DIR}/sd_disk.c ${FATFS_SOURCES})
target_include_directories(dx_sdimg PRIVATE ${SDCARD_DIR}/mkimg_conf ${SDCARD_DIR} ${FATFS_DIR})
target_compile_definitions(dx_sdimg PRIVATE BOARD_FFCONF="${FATFS_DIR}/conf/ffconf.h")
target_compile_options(dx_sdimg PRIVATE -O2 -g)
set_target_properties(dx_sdimg PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
