# Changelog

## v0.1.0 - 2026-10-18

First HercuFork360 release with native steering wheel input and force feedback.

### Added

- Raw SDL2 wheel input for player 1: steering, accelerator, brake, buttons,
  paddles, and the first POV hat.
- English **HID > Input setup...** window with a **Controller / Steering wheel**
  selector. Controller mode offers Automatic, XInput (Windows), and SDL.
- Guided steering/pedal calibration, live axis display, and button assignment.
- Switching to controller mode disables raw wheel input and FFB while retaining
  the wheel selection, calibration, buttons, and FFB preferences.
- Native Xbox 360 `XInputdFF` effect creation, updates, activation, stopping,
  reset, and global gain, without game-specific executable hooks.
- Constant, spring, damper, inertia, and friction effects through SDL2.
- User gain limit (default 10%), steering force inversion, and output timeout
  on loss of focus or guest activity.
- Native request and SDL submission diagnostics; optional automated wheel tests.

### Fixed

- FM2's native operation 2 now activates the five loaded effect slots. Treating
  it as stop left all forces inactive despite successful parameter updates.

### Confirmed setup

The user reports working wheel input and native FFB with a **MOZA R3 on Windows**
in **Forza Motorsport 2, 3, and 4**. Game editions/title updates and driver
versions were not recorded. Other configurations require individual testing.

### Usage and limitations

Open **HID > Input setup...**, choose the input method, and save with no game
running. Restart the emulator to apply the selection. In wheel mode, calibrate
steering and both pedals, enable native FFB, and start at **10% gain**.

FFB is experimental and depends on game and wheel-driver support. Periodic and
ramp effects, proportional clutch input, and separate USB pedals are not yet
implemented. Wheel input uses player 1; rumble is not converted to steering torque.

### Validation and package

- Windows x64 Release build with MSVC.
- Three automated test suites passed: calibration, virtual wheel/effect
  translation (including controller-mode disabling), and native protocol/PPC JIT.
- The release ZIP includes `xenia_canary.exe`, setup documentation, this changelog,
  the project license, and build/checksum information.
- Personal configuration, profiles, game data, caches, logs, debug symbols, and
  development/test executables are excluded.
