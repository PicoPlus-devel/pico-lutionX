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

add_executable(dx_host
  ${CMAKE_CURRENT_LIST_DIR}/dx_host.cpp
  ${PICO_LUTIONX_ROOT}/src/port/dx_heap.cpp
  ${PICO_LUTIONX_ROOT}/3rdparty/tlsf/tlsf.c
  ${CMAKE_CURRENT_BINARY_DIR}/gen/dx_static_bytes.h
  $<TARGET_OBJECTS:dx_engine>)
target_include_directories(dx_host PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_compile_definitions(dx_host PRIVATE DX_HEAP_HOST)
target_link_libraries(dx_host PRIVATE dx_engine)
target_link_options(dx_host PRIVATE -m32 ${HOST_FLAGS}
  -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc,--wrap=memalign,--wrap=aligned_alloc,--wrap=posix_memalign)
set_target_properties(dx_host PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
if(NOT DX_SANITIZE)
  set_target_properties(dx_host PROPERTIES OUTPUT_NAME dx_host_nosan)
endif()
