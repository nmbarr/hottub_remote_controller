# ESPHome fork (experimental)

Replaces `firmware/v1`'s custom `esp-mqtt` firmware (now deleted): instead of
writing the Balboa display decode from scratch, this vendors
[kgstorm/Balboa-GS100-with-VL260-topside][kg] — the same reference
implementation cited in [`docs/wifi.md`](../../docs/wifi.md#reference-implementation) —
and runs it as upstream intended: talking to Home Assistant, controlled from
the HA phone app, rather than through this project's Mosquitto/Node-RED
stack.

**Status: unconfirmed, do not flash yet.** kgstorm's decode is verified
against a GS100 pack with VL200/VL400-series panels. This tub's pack is a
Mach 7 (RS-81); whether it speaks the same protocol as the GS100 is still an
open question — see
[`docs/wifi.md#still-unconfirmed`](../../docs/wifi.md#still-unconfirmed).
Do the scope/logic-analyzer check on RJ45 pins 5/6 before relying on
anything here.

## Provenance

- Upstream: <https://github.com/kgstorm/Balboa-GS100-with-VL260-topside>
- Pinned commit: `044e87d746f30bd3202a3a62683cf3b95865d6b5`
- License: MIT — see `LICENSE.upstream` (copied verbatim from upstream)

Vendored, not pulled live via `external_components: source: github://...`,
so the panel-decode logic (`esp32-spa/inputs/esp32-spa.h`) stays put even if
upstream changes, and this fork's edits are easy to diff against a known
base.

## What was changed vs. upstream

- **`api:` left as-is.** Entities are controlled through Home Assistant and
  the vendored `spa-control-card.js` Lovelace card (see Frontend, below) —
  not MQTT. Home Assistant runs on the Pi alongside Mosquitto (which the
  water-chemistry sensors below still don't need HA for, if that stack ever
  wants them independently).
- **Removed the `update:`/second `ota:` platform** that polled kgstorm's
  hosted `manifest.json` for firmware updates — meaningless once this fork
  diverges from upstream, and undesirable to leave pointed at someone else's
  release feed.
- **Added water chemistry sensors** (`i2c:` + `ads1115` platform entries in
  `sensor:`) for this project's planned pH/ORP probes. These are new
  additions alongside kgstorm's decode, not modifications to it — the
  custom `esp32_spa` component (`esp32-spa/inputs/`) is untouched.
- **GPIO pins left as upstream's** (CLK=35, DATA=34, WARM=25, COOL=26,
  LIGHT=27, PUMP=32) — and these turn out to already match
  `hardware/esp32-spa`'s wiring table (see the root README's Firmware
  section), so no pin remapping was actually needed here.

## What still needs real values, not placeholders

- **pH/ORP calibration** (`calibrate_linear` in `esp32-spa.yaml`) — the
  current `0.0 -> 0.0, 3.0 -> 14.0` mapping is a stand-in shape, not a
  calibration. Needs real two-point buffer-solution readings once a probe
  is on hand.
- **I2C pins** (`sda: GPIO21` / `scl: GPIO22`) — arbitrary defaults, not
  checked against `hardware/esp32-spa`'s actual wiring (the v1.5 chemistry
  sensors aren't on that schematic yet).

## Building

```
pip install esphome   # or the Docker image, see project notes
cp secrets.yaml.example secrets.yaml   # fill in real values
esphome compile esp32-spa.yaml
```

## Home Assistant setup

Home Assistant runs on the Pi, separate from this build step:

1. Run Home Assistant on the Pi — [HA Container](https://www.home-assistant.io/installation/raspberrypi#docker-compose)
   is the lightest fit alongside the existing Mosquitto install; HA OS is
   the alternative if you'd rather give it the whole disk.
2. First flash is over USB (`esphome run esp32-spa.yaml`); after that, HA's
   **ESPHome** integration (Settings → Devices & Services → Add Integration
   → ESPHome) discovers the device on the network via `api:` and handles
   OTA updates from then on — no need to keep running `esphome` by hand.
3. `secrets.yaml`'s `api_key` is the noise-encryption PSK HA and the device
   share; HA will ask for it (or auto-fill it if it discovered the key from
   the device) when you add the integration.

### Frontend

`spa-control-card.js` (vendored from upstream, same commit as everything
else here) is a custom Lovelace card purpose-built for this firmware's
entities:

1. Copy `spa-control-card.js` into HA's `config/www/` folder.
2. In the dashboard you want it on: **⋮ menu → Manage resources → Add
   resource**, URL `/local/spa-control-card.js`, type **JavaScript Module**.
3. **Add Card → Spa Control Card**, or add this YAML directly:

   ```yaml
   type: 'custom:spa-control-card'
   device_name: 'esp32-spa'   # matches esphome: name: in esp32-spa.yaml
   title: 'Hot Tub Control'
   ```

If the card doesn't show up, hard-refresh the dashboard
(Ctrl/Cmd+Shift+R) — HA aggressively caches Lovelace resources.

[kg]: https://github.com/kgstorm/Balboa-GS100-with-VL260-topside
