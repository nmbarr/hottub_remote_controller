# ESPHome fork (experimental)

An alternative to `firmware/v1`'s custom `esp-mqtt` firmware: instead of
writing the Balboa display decode from scratch, this vendors
[kgstorm/Balboa-GS100-with-VL260-topside][kg] — the same reference
implementation cited in [`docs/wifi.md`](../../docs/wifi.md#reference-implementation) —
and adapts it to this project's MQTT/Node-RED stack.

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

- **`api:` → `mqtt:`** (`esp32-spa.yaml`). This stack has no Home Assistant;
  entities now publish under `hottub/panel/...` to the Pi's Mosquitto broker
  instead of over the HA-native API. Command topics (e.g.
  `hottub/panel/number/spa_high_temperature/command`) replace HA service
  calls for anything Node-RED needs to write.
- **Removed the `update:`/second `ota:` platform** that polled kgstorm's
  hosted `manifest.json` for firmware updates — meaningless once this fork
  diverges from upstream, and undesirable to leave pointed at someone else's
  release feed.
- **Added water chemistry sensors** (`i2c:` + `ads1115` platform entries in
  `sensor:`) for this project's planned pH/ORP probes. These are new
  additions alongside kgstorm's decode, not modifications to it — the
  custom `esp32_spa` component (`esp32-spa/inputs/`) is untouched.
- **GPIO pins left as upstream's** (CLK=35, DATA=34, WARM=25, COOL=26,
  LIGHT=27, PUMP=32) — this board's v1 schematic hasn't finalized its own
  pin assignments yet, so these are placeholders, not confirmed against
  `hardware/v1`. Cross-check against the schematic's `DISPLAY_CLK`/
  `DISPLAY_DATA`/`WARM_BUTTON`/`COOL_BUTTON`/`LIGHT_BUTTON`/`JET_BUTTON`
  net labels before wiring.

## What still needs real values, not placeholders

- **pH/ORP calibration** (`calibrate_linear` in `esp32-spa.yaml`) — the
  current `0.0 -> 0.0, 3.0 -> 14.0` mapping is a stand-in shape, not a
  calibration. Needs real two-point buffer-solution readings once a probe
  is on hand.
- **I2C pins** (`sda: GPIO21` / `scl: GPIO22`) — arbitrary defaults, not
  checked against `hardware/v1`'s actual wiring.

## Building

```
pip install esphome   # or the Docker image, see project notes
cp secrets.yaml.example secrets.yaml   # fill in real values
esphome compile esp32-spa.yaml
```

[kg]: https://github.com/kgstorm/Balboa-GS100-with-VL260-topside
