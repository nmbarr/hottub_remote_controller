# Hot Tub Remote Controller

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

## Docs

- [`docs/wifi.md`](docs/wifi.md) — plan for exposing live sensor data over
  WiFi, from a v1 (local network, single client, polled HTTP) through a
  future v2 (remote access, auth, backend service).

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
