# dx_engine.cmake - libdx_engine.a for the firmware: DevilutionX with its libraries
# (cmake/devilutionx.cmake) and the SDL 1.2 subset (src/sdl).
#
# cmake/psram_linker.cmake places the static data of everything in this library in
# PSRAM. After the library is built, its calls to malloc, calloc, realloc, free,
# memalign and posix_memalign are renamed to dx_malloc etc. (src/port/dx_heap.cpp), so
# the engine allocates from the PSRAM heap while the SDK, the drivers and the platform
# layer keep the C library's malloc in SRAM. C++ new/delete go to the PSRAM heap for
# all code (dx_heap.cpp defines them).

include(${CMAKE_CURRENT_LIST_DIR}/devilutionx.cmake)

add_library(dx_engine STATIC
    ${DX_ENGINE_SOURCES}
    ${DX_DEP_C_SOURCES}
    ${DX_DEP_CXX_SOURCES}
    ${PICO_LUTIONX_ROOT}/src/sdl/sdl_core.cpp
    ${PICO_LUTIONX_ROOT}/src/sdl/sdl_events.cpp
    ${PICO_LUTIONX_ROOT}/src/sdl/sdl_rwops.cpp
    ${PICO_LUTIONX_ROOT}/src/sdl/sdl_video.cpp
)

target_include_directories(dx_engine PUBLIC ${DX_INCLUDE_DIRS} ${PICO_LUTIONX_ROOT}/src/sdl)

# The gamepads are presented to the engine as SDL joystick 0 (src/pico/dx_input.cpp):
# buttons 0-15 and the two sticks on axes 0-3.
set(DX_JOYSTICK_DEFINITIONS
    JOY_BUTTON_A=0                  # bottom face button (Xbox A, PlayStation cross, SNES B)
    JOY_BUTTON_B=1                  # right (Xbox B, circle, SNES A)
    JOY_BUTTON_X=2                  # left (Xbox X, square, SNES Y)
    JOY_BUTTON_Y=3                  # top (Xbox Y, triangle, SNES X)
    JOY_BUTTON_LEFTSHOULDER=4
    JOY_BUTTON_RIGHTSHOULDER=5
    JOY_BUTTON_TRIGGERLEFT=6
    JOY_BUTTON_TRIGGERRIGHT=7
    JOY_BUTTON_BACK=8
    JOY_BUTTON_START=9
    JOY_BUTTON_LEFTSTICK=10
    JOY_BUTTON_RIGHTSTICK=11
    JOY_BUTTON_DPAD_UP=12
    JOY_BUTTON_DPAD_DOWN=13
    JOY_BUTTON_DPAD_LEFT=14
    JOY_BUTTON_DPAD_RIGHT=15
    JOY_AXIS_LEFTX=0
    JOY_AXIS_LEFTY=1
    JOY_AXIS_RIGHTX=2
    JOY_AXIS_RIGHTY=3
)

target_compile_definitions(dx_engine PUBLIC
    ${DX_DEFINITIONS}
    ${DX_JOYSTICK_DEFINITIONS}
    DEVILUTIONX_PICO_DATA_DIR="/roms/devilutionx/"   # src/pico/dx_platform.h DX_DATA_DIR
    # a hero name is filled in, so a new game can be started with a gamepad only
    PREFILL_PLAYER_NAME
    # Zoom (Settings, Graphics) on by default: the full view runs below 20 fps on the board
    # (not in the test program, whose reference demo assumes the stock settings)
    DEVILUTIONX_PICO_ZOOM_DEFAULT
)

target_compile_options(dx_engine PRIVATE
    -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions>
    -Wno-deprecated-declarations
)

# The malloc family of the engine and its libraries -> the PSRAM heap.
set(_dx_redefine "${CMAKE_BINARY_DIR}/dx_engine_redefine_syms.txt")
file(WRITE "${_dx_redefine}"
"malloc dx_malloc
calloc dx_calloc
realloc dx_realloc
free dx_free
memalign dx_memalign
posix_memalign dx_posix_memalign
")
add_custom_command(TARGET dx_engine POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} --redefine-syms=${_dx_redefine} $<TARGET_FILE:dx_engine>
    COMMENT "libdx_engine.a: malloc family -> PSRAM heap (dx_malloc etc.)"
    VERBATIM)
