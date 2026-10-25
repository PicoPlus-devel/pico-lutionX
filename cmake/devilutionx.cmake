# devilutionx.cmake - the DevilutionX 1.5.5 engine and its bundled libraries as built for
# pico-lutionX. Shared by the host harness (hosttest/) and the firmware.
#
# Configuration (fixed): USE_SDL1, NONET, MPQ support, demo mode (timedemo benchmark).
# DX_NOSOUND selects the engine's sound stubs instead of its sound code with the port's
# SDL_audiolib subset (src/aulib).
#
# Provides:
#   DX_ENGINE_SOURCES   engine sources (Source/)
#   DX_DEP_C_SOURCES    C libraries (libmpq)
#   DX_DEP_CXX_SOURCES  C++ libraries (PKWare, fmt, libsmackerdec)
#   DX_INCLUDE_DIRS     include directories for all of the above
#   DX_DEFINITIONS      compile definitions for all of the above

get_filename_component(PICO_LUTIONX_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(DX_SOURCE_DIR "${PICO_LUTIONX_ROOT}/3rdparty/devilutionX/Source")
set(DX_DEPS_DIR "${PICO_LUTIONX_ROOT}/3rdparty/dx-deps")

option(DX_NOSOUND "Build DevilutionX with the sound stubs" OFF)

set(DX_ENGINE_FILES
  appfat.cpp
  automap.cpp
  capture.cpp
  codec.cpp
  control.cpp
  cursor.cpp
  dead.cpp
  debug.cpp
  diablo.cpp
  doom.cpp
  engine.cpp
  error.cpp
  gamemenu.cpp
  gmenu.cpp
  help.cpp
  hwcursor.cpp
  init.cpp
  interfac.cpp
  inv.cpp
  itemdat.cpp
  items.cpp
  lighting.cpp
  loadsave.cpp
  menu.cpp
  minitext.cpp
  misdat.cpp
  missiles.cpp
  monstdat.cpp
  monster.cpp
  movie.cpp
  msg.cpp
  multi.cpp
  nthread.cpp
  objdat.cpp
  objects.cpp
  options.cpp
  pack.cpp
  pfile.cpp
  player.cpp
  playerdat.cpp
  plrmsg.cpp
  portal.cpp
  quests.cpp
  restrict.cpp
  sha.cpp
  spelldat.cpp
  spells.cpp
  stores.cpp
  sync.cpp
  textdat.cpp
  tmsg.cpp
  towners.cpp
  track.cpp
  controls/axis_direction.cpp
  controls/controller.cpp
  controls/controller_buttons.cpp
  controls/controller_motion.cpp
  controls/devices/joystick.cpp
  controls/devices/kbcontroller.cpp
  controls/game_controls.cpp
  controls/menu_controls.cpp
  controls/modifier_hints.cpp
  controls/plrctrls.cpp
  DiabloUI/button.cpp
  DiabloUI/credits.cpp
  DiabloUI/credits_lines.cpp
  DiabloUI/diabloui.cpp
  DiabloUI/dialogs.cpp
  DiabloUI/hero/selhero.cpp
  DiabloUI/mainmenu.cpp
  DiabloUI/multi/selconn.cpp
  DiabloUI/multi/selgame.cpp
  DiabloUI/progress.cpp
  DiabloUI/scrollbar.cpp
  DiabloUI/selok.cpp
  DiabloUI/selstart.cpp
  DiabloUI/selyesno.cpp
  DiabloUI/settingsmenu.cpp
  DiabloUI/support_lines.cpp
  DiabloUI/text_input.cpp
  DiabloUI/title.cpp
  dvlnet/abstract_net.cpp
  dvlnet/base.cpp
  dvlnet/cdwrap.cpp
  dvlnet/frame_queue.cpp
  dvlnet/loopback.cpp
  dvlnet/packet.cpp
  engine/actor_position.cpp
  engine/animationinfo.cpp
  engine/assets.cpp
  engine/backbuffer_state.cpp
  engine/direction.cpp
  engine/dx.cpp
  engine/events.cpp
  engine/load_cel.cpp
  engine/load_cl2.cpp
  engine/load_clx.cpp
  engine/load_pcx.cpp
  engine/palette.cpp
  engine/path.cpp
  engine/random.cpp
  engine/sound_position.cpp
  engine/surface.cpp
  engine/trn.cpp
  engine/render/automap_render.cpp
  engine/render/clx_render.cpp
  engine/render/dun_render.cpp
  engine/render/primitive_render.cpp
  engine/render/scrollrt.cpp
  engine/render/text_render.cpp
  items/validation.cpp
  levels/crypt.cpp
  levels/drlg_l1.cpp
  levels/drlg_l2.cpp
  levels/drlg_l3.cpp
  levels/drlg_l4.cpp
  levels/gendung.cpp
  levels/setmaps.cpp
  levels/themes.cpp
  levels/town.cpp
  levels/trigs.cpp
  panels/charpanel.cpp
  panels/info_box.cpp
  panels/mainpanel.cpp
  panels/spell_book.cpp
  panels/spell_icons.cpp
  panels/spell_list.cpp
  platform/locale.cpp
  qol/autopickup.cpp
  qol/chatlog.cpp
  qol/floatingnumbers.cpp
  qol/itemlabels.cpp
  qol/monhealthbar.cpp
  qol/stash.cpp
  qol/xpbar.cpp
  storm/storm_net.cpp
  storm/storm_svid.cpp
  utils/cel_to_clx.cpp
  utils/cl2_to_clx.cpp
  utils/console.cpp
  utils/display.cpp
  utils/file_util.cpp
  utils/format_int.cpp
  utils/language.cpp
  utils/logged_fstream.cpp
  utils/parse_int.cpp
  utils/paths.cpp
  utils/pcx_to_clx.cpp
  utils/sdl_bilinear_scale.cpp
  utils/sdl_thread.cpp
  utils/str_cat.cpp
  utils/str_case.cpp
  utils/surface_to_clx.cpp
  utils/utf8.cpp  # SUPPORTS_MPQ
  mpq/mpq_reader.cpp
  mpq/mpq_sdl_rwops.cpp
  mpq/mpq_writer.cpp
  encrypt.cpp
  # USE_SDL1
  utils/sdl2_to_1_2_backports.cpp
  # demo mode (timedemo)
  engine/demomode.cpp
)
if(DX_NOSOUND)
  list(APPEND DX_ENGINE_FILES effects_stubs.cpp engine/sound_stubs.cpp)
else()
  list(APPEND DX_ENGINE_FILES effects.cpp engine/sound.cpp utils/push_aulib_decoder.cpp utils/soundsample.cpp)
endif()
list(TRANSFORM DX_ENGINE_FILES PREPEND "${DX_SOURCE_DIR}/" OUTPUT_VARIABLE DX_ENGINE_SOURCES)

set(DX_DEP_C_SOURCES
  # the flash asset cache; part of the engine library, so its directory mirror is in PSRAM
  ${PICO_LUTIONX_ROOT}/src/port/dx_fcache.c
  # the per-pixel colour map loops of engine/render/blit_impl.hpp, in SRAM on the board
  ${PICO_LUTIONX_ROOT}/src/port/dx_blit.c
  ${DX_DEPS_DIR}/libmpq/libmpq/common.c
  ${DX_DEPS_DIR}/libmpq/libmpq/explode.c
  ${DX_DEPS_DIR}/libmpq/libmpq/extract.c
  ${DX_DEPS_DIR}/libmpq/libmpq/huffman.c
  ${DX_DEPS_DIR}/libmpq/libmpq/mpq.c
  ${DX_DEPS_DIR}/libmpq/libmpq/wave.c
  # zlib 1.2.13, inflate only: the spawn.mpq that DevilutionX distributes is
  # zlib-compressed (retail DIABDAT.MPQ uses PKWARE implode).
  ${DX_DEPS_DIR}/zlib/adler32.c
  ${DX_DEPS_DIR}/zlib/crc32.c
  ${DX_DEPS_DIR}/zlib/inffast.c
  ${DX_DEPS_DIR}/zlib/inflate.c
  ${DX_DEPS_DIR}/zlib/inftrees.c
  ${DX_DEPS_DIR}/zlib/zutil.c
)

# libmpq's own config.h only defines VERSION; the engine's config.h shadows it.
set_source_files_properties(${DX_DEPS_DIR}/libmpq/libmpq/mpq.c PROPERTIES COMPILE_DEFINITIONS "VERSION=\"0.4.2\"")

set(DX_DEP_CXX_SOURCES
  ${PICO_LUTIONX_ROOT}/src/aulib/aulib.cpp
  ${PICO_LUTIONX_ROOT}/src/aulib/aulib_mp3.cpp
  ${DX_DEPS_DIR}/PKWare/explode.cpp
  ${DX_DEPS_DIR}/PKWare/implode.cpp
  ${DX_DEPS_DIR}/fmt/src/format.cc
  ${DX_DEPS_DIR}/libsmackerdec/src/BitReader.cpp
  ${DX_DEPS_DIR}/libsmackerdec/src/FileStream.cpp
  ${DX_DEPS_DIR}/libsmackerdec/src/LogError.cpp
  ${DX_DEPS_DIR}/libsmackerdec/src/SmackerDecoder.cpp
)

set(DX_INCLUDE_DIRS
  ${PICO_LUTIONX_ROOT}/src/aulib/include
  ${PICO_LUTIONX_ROOT}/src/port
  ${PICO_LUTIONX_ROOT}/src/port/stubs
  ${PICO_LUTIONX_ROOT}/src/sdl/include
  ${DX_SOURCE_DIR}
  ${DX_DEPS_DIR}/fmt/include
  ${DX_DEPS_DIR}/libsmackerdec/include
  ${DX_DEPS_DIR}/simpleini
  ${DX_DEPS_DIR}/tl
  ${DX_DEPS_DIR}/hoehrmann_utf8
  ${DX_DEPS_DIR}/libmpq
  ${DX_DEPS_DIR}/PKWare
  ${DX_DEPS_DIR}/zlib
  ${DX_DEPS_DIR}/dr_libs
)

set(DX_DEFINITIONS
  USE_SDL1
  NONET
  DISABLE_TCP
  DISABLE_ZERO_TIER
  SDL1_VIDEO_MODE_BPP=8
  SDL1_VIDEO_MODE_FLAGS=SDL_DOUBLEBUF
  DEFAULT_WIDTH=640
  DEFAULT_HEIGHT=480
  DVL_NO_FILESYSTEM
  DEVILUTIONX_PICO=1
  DEVILUTIONX_PICO_SINGLE_THREADED
  # Options code refers to the resampler even without sound.
  DEVILUTIONX_RESAMPLER_SPEEX
  DEVILUTIONX_DEFAULT_RESAMPLER=Speex
)
if(DX_NOSOUND)
  list(APPEND DX_DEFINITIONS NOSOUND)
else()
  # The game's sounds are 22050 Hz: mixing at that rate needs no resampling; the board
  # doubles the rate for HDMI and I2S (src/pico/dx_audio.c).
  list(APPEND DX_DEFINITIONS DEFAULT_AUDIO_SAMPLE_RATE=22050 DEFAULT_AUDIO_CHANNELS=2 DEFAULT_AUDIO_BUFFER_SIZE=512)
endif()
