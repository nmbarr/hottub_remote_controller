# Building and flashing from WSL

Development happens in WSL2 (Ubuntu 24.04) on a Windows host. The ESP32
DevKitC plugs into the Windows machine, so its USB serial port has to be
forwarded into WSL before `esphome` can see it. This note records that
forwarding setup and an environment gotcha that cost time the first time
around.

## Forwarding the board into WSL

WSL2 has no native USB passthrough; USB devices are attached over the
network with USB/IP. The WSL kernel already ships the pieces needed to
receive them — `vhci-hcd` plus the `cp210x`, `ch341` and `ftdi_sio` serial
drivers — so nothing has to be installed or built on the Linux side. All the
setup is on Windows.

Install [usbipd-win](https://github.com/dorssel/usbipd-win) once, from an
**administrator** PowerShell:

```powershell
winget install --interactive --exact dorssel.usbipd-win
```

Then find the board's bus ID:

```powershell
usbipd list
```

The DevKitC appears as its USB-serial bridge, not as "ESP32" — `Silicon Labs
CP210x UART Bridge` on a genuine board, `USB-SERIAL CH340` on clones. (An
S3/C3 using the SoC's native USB would instead show as `USB JTAG/serial debug
unit`.) Note the `BUSID`, e.g. `2-4`.

Share the device, then attach it:

```powershell
usbipd bind --busid 2-4          # administrator; persists across reboots
usbipd attach --wsl --busid 2-4  # ordinary shell; does NOT persist
```

**`bind` is one-time. `attach` is not** — re-run it after every unplug/replug
and after every Windows reboot. This is the usual reason `/dev/ttyUSB0`
vanishes mid-session.

In WSL the board then shows up as `/dev/ttyUSB0` (CP210x/CH340; a native-USB
part would be `/dev/ttyACM0`).

## Serial port permissions

`/dev/ttyUSB0` is owned by `root:dialout`, so a user outside that group gets
permission denied even after a successful attach:

```bash
sudo usermod -aG dialout $USER
```

Group membership is only picked up by new logins, and in WSL that means
restarting the distro — run `wsl --shutdown` from Windows and reopen the
terminal. (`newgrp dialout` works for a single shell if you'd rather not.)

## Verifying the chain

Before blaming the firmware, confirm the host can reach the chip at all.
`chip-id` only reads:

```console
$ esptool --port /dev/ttyUSB0 chip-id
Chip type:          ESP32-D0WD-V3 (revision v3.1)
Features:           Wi-Fi, BT, Dual Core + LP Core, 240MHz, ...
Crystal frequency:  40MHz
MAC:                44:1d:64:4f:5b:c8
```

A D0WD-V3 is the plain ESP32, matching `board: esp32dev` in
`firmware/esphome-spa/esp32-spa.yaml`. Because the DevKitC wires DTR/RTS to
EN/GPIO0, esptool resets the board into and out of the bootloader by itself —
there's no need to hold BOOT while flashing.

```bash
esphome run firmware/esphome-spa/esp32-spa.yaml -p /dev/ttyUSB0
```

## Troubleshooting

- **`/dev/ttyUSB0` missing** — the attach lapsed. Re-run `usbipd attach --wsl
  --busid 2-4`.
- **Permission denied on the port** — not in `dialout`, or in it but the
  shell predates the change. `groups` tells you which.
- **Attach fails, or the port drops during a flash** — something on the
  Windows side has the COM port open. Close Arduino IDE, PuTTY, or a VS Code
  serial monitor and retry.
- **usbipd reports the device shared but attach errors about drivers** —
  `usbipd unbind --busid 2-4`, then `usbipd bind --force --busid 2-4`.
