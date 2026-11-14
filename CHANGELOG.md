# Changelog

pico-lutionX is a port of DevilutionX, the open-source engine for Diablo, to boards built
around the Raspberry Pi RP2350 microcontroller. The picture goes out over HDMI, the game
data is read from an SD card, and the game is played with a USB keyboard and mouse or a
gamepad.

## v0.2-alpha

### Added

- **DVI display mode**, for monitors and TVs that do not accept an HDMI signal. Choose
  HDMI or DVI under *Settings → Graphics → Display Mode*, or during a game under
  *Options → Display*; the picture switches at once and the choice is kept on the SD
  card. DVI carries no sound: it plays on the board's own audio output instead (on the
  Fruit Jam the speaker, or the headphones when plugged in; on the Pico Plus 2 board only
  when a PCM5100A DAC is fitted).
- **Waveshare RP2350-PiZero on the PCB by Liu Szuping** (@szuping) is now supported
  (`pico-lutionX_pizeroliupcb.uf2`). Picture over the PCB's
  HDMI port, sound over HDMI and the audio jack at the same time, a USB keyboard, mouse
  or gamepad on the PiZero's PIO-USB port, and two NES/SNES controller ports. The game
  needs 8 MB of PSRAM soldered onto the PiZero. Tested on this board. The PCB
  design is not yet available.
  **Known issue:** on this board, some TVs and monitors lose the picture for a few seconds
  at a time during play; others show it without interruption. This is a limitation of the
  RP2350-PiZero at 384 MHz, the clock the game needs.

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
- Can also be started from the menu of
  [pico-bootLoader](https://github.com/PicoPlus-devel/pico-bootLoader), where it is
  listed as *Diablo* among the Ports.
- The board runs at 384 MHz. At this clock the picture has no dots or lines on TVs and
  monitors that show them at 378 MHz, and the USB port of the Pico Plus 2 board keeps
  working. Thanks to @szuping for the hint that led to this.
