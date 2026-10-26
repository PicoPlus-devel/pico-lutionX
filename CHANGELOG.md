# Changelog

pico-lutionX is a port of DevilutionX, the open-source engine for Diablo, to boards built
around the Raspberry Pi RP2350 microcontroller. The picture goes out over HDMI, the game
data is read from an SD card, and the game is played with a USB keyboard and mouse or a
gamepad.

## v0.1-alpha

First test version. It has been tried on the Pico Plus 2 board only, where the game
still runs slower than intended; see the README for what has been tested.

- Runs Diablo with the shareware data (`spawn.mpq`) or the full game (`DIABDAT.MPQ`) on
  the Adafruit Fruit Jam and on a Pimoroni Pico Plus 2 with the Adafruit DVI and microSD
  breakouts.
- 640×480 picture over HDMI, music and sound over HDMI or the headphone jack.
- USB keyboard, mouse and gamepads; NES and SNES controllers and the SNES Mouse on the
  PicoNES PCB; the Wii Classic Controller on the Fruit Jam.
- Every button of Xbox, PlayStation, SNES and NES controllers has a function in the
  game, including the stick clicks. On a NES controller, Select gives access to spells
  and potions.
- Graphics and sounds are copied to the board's flash memory the first time they are
  needed, so later loads are faster.
- Saved games are compatible with DevilutionX 1.5.5 on a PC.
