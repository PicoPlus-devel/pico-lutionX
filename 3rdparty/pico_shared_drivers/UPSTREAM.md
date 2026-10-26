Copied from pico-quake2 (commit fe9e3ed, 2026-10-23) 3rdparty/pico_shared_drivers, without pwm_audio.
Local changes made there are marked pico-quake2:, changes made here pico-lutionX:.

History of the pico-quake2 copy:

Copied from pico-duke3D (commit a7d84b5) 3rdparty/pico_shared_drivers, itself a vendored subset of
pico_shared (https://github.com/fhoedemakers/pico_shared). Local changes are marked pico-quake2:.

nespad/ (nespad.h, nespad.cpp, nespad.pio) was re-copied from pico_shared itself at commit a7cc387;
its one local change is nespad_read_ready().

pico-lutionX: nespad/ re-copied from pico_shared at commit fb9ad91 (SNES Mouse), with
nespad_read_ready() applied again. usb_hid/: the stick clicks of XInput, DualShock 4 and
DualSense pads (Button::LSTICK, RSTICK) and their PS button as Select+Start; keyboards in
boot protocol are read as boot reports (processKeyboardReport) instead of through their
report descriptor, which dropped every report of keyboards with report IDs. Also wrong in
pico_shared.
