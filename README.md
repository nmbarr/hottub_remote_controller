# Hot Tub Remote Controller

[![KiCad checks](https://github.com/nmbarr/hottub_remote_controller/actions/workflows/kicad-checks.yml/badge.svg)](https://github.com/nmbarr/hottub_remote_controller/actions/workflows/kicad-checks.yml)

Hardware and firmware for a remote monitor/controller that taps into a hot
tub's control system, aiming to expose status and control over WiFi instead
of only via the tub's built-in panel.

## Status

Early hardware design phase, centered on an ESP32-based board that taps the
spa pack's control bus over RS485. Firmware for the new hardware has not
been written yet.

## Hardware

`hardware/v1` contains the KiCad project for the v1 board:

- **ESP32-DevKitC** as the main controller.
- **MAX3485** RS485 transceiver, tapped onto the hot tub's control bus.
- Direction control (DE/RE~) tied to a GPIO driven automatically by
  ESP-IDF's `UART_MODE_RS485_HALF_DUPLEX`, rather than bit-banged in
  software.

See `docs/Architecture.drawio` for the system architecture diagram.

### Hardware roadmap

The architecture diagram lays out three planned revisions:

- **V1 — ESP32 DevKitC (initial build).** An ESP32 DevKitC (using its
  onboard USB-UART and 3.3V regulator) driving a MAX3485 RS485 transceiver.
  The transceiver taps into the hot tub's existing RS485 wiring between the
  front panel and the Mach-7 control board through an off-the-shelf RJ45
  breakout board and screw terminals — no cutting or splicing of the
  existing harness.
- **V1.5 — + water chemistry sensors.** Same V1 hardware and RS485 tap,
  with off-the-shelf DFRobot pH and ORP probes (each with its own analog
  conditioning board) added on, wired into the DevKitC's analog inputs via
  Gravity/JST connectors.
- **V2 — custom PCB.** Replaces the DevKitC with a purpose-built board: a
  bare ESP32 as the processor, a dedicated buck-converter power supply, and
  the MAX3485 transceiver wired directly to two onboard RJ45 jacks (one to
  the front panel, one to the Mach-7 control board), removing the external
  breakout/screw-terminal board. The purchased DFRobot conditioning boards
  are also replaced by a custom analog front-end that conditions the raw pH
  and ORP electrodes directly for the ESP32's ADC.

## Docs

- [`docs/wifi.md`](docs/wifi.md) — plan for exposing the tub over WiFi via
  MQTT to a Raspberry Pi running Mosquitto and Node-RED, from a v1
  (local network, read-only telemetry) through a v2 that adds remote access
  over a VPN and a command path for setpoints.

## Repository layout

```
hardware/v1/       KiCad schematic and project for the v1 board
Drivers/libdrivers  Submodule of shared, vendor-agnostic sensor drivers
docs/               Architecture diagram and design notes
```

## Submodules

This repo uses a git submodule for shared drivers:

```
git submodule update --init --recursive
```

## CI

`.github/workflows/kicad-checks.yml` runs KiCad's headless checks on every
push and PR (it's a required status check on `main`, so it always runs
rather than being skipped by a path filter), plus weekly against `main`
(Mondays) to catch drift even when nothing's changed recently: ERC on each
project's schematic, and DRC
on its board once one exists (v1 and v1.5 are schematic/wiring-only, no
custom PCB — that starts with v2). The workflow seeds KiCad's default
global library tables before running so stock libraries resolve the same
as on a normal install; only error-severity findings fail the build, and
remaining warnings (currently just `PCM_Espressif`, a library installed
locally via KiCad's Plugin & Content Manager rather than vendored into the
repo) print to the log for visibility but don't block.
