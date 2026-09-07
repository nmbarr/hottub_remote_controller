# WiFi access

Goal: view live hot tub status (and, once v1.5's water chemistry sensors are
in, pH/ORP readings) from a phone instead of only via a wired debug
connection, and send commands — starting with the temperature setpoint —
back the other way.

WiFi: the ESP32 has WiFi built into the SoC, so there's no separate WiFi
module or driver to write — connectivity and the MQTT client both come from
ESP-IDF's own `esp_wifi`/`esp-mqtt` components, used directly rather than
vendored into this repo. (This replaces an earlier plan, from when the board
was STM32-based, to drive an external Inventek ISM43362 WiFi module over SPI
via ST's X-CUBE-WIFI1 middleware — moot now that WiFi is native to the
processor.)

## Architecture

Both versions use the same topology; they differ only in *where you can be
standing*. A Raspberry Pi on the LAN acts as the IoT server:

```
  ESP32  <--outbound MQTT-->  Mosquitto  <-->  Node-RED  -->  dashboard
 (RS485                      (broker,          (flows,        (phone)
  to spa pack)                on the Pi)        logic, UI)
```

- **Broker**: Mosquitto on the Pi. The ESP32 makes a single outbound
  connection to it and never accepts inbound connections itself — including
  for commands, which arrive over that same outbound connection as
  subscriptions rather than as inbound requests. This is what makes v2's
  remote access possible without ever exposing the board.
- **Logic/UI**: Node-RED on the same Pi, subscribing and publishing through
  `mqtt in`/`mqtt out` nodes. `node-red-dashboard` provides the phone UI
  (gauges for temp/pH/ORP, a slider for the setpoint), so there's no custom
  frontend in this repo and UI changes never require reflashing the board.
- **Firmware side**: `esp-mqtt` (`mqtt_client`), which ships with ESP-IDF —
  no vendored dependency added.

## Topic map

| Topic | Dir | Retain | QoS | Payload | Version |
| --- | --- | --- | --- | --- | --- |
| `hottub/status` | ESP32 → | yes | 1 | `online` / `offline` (LWT) | v1 |
| `hottub/state` | ESP32 → | yes | 0 | JSON status blob | v1 |
| `hottub/cmd/setpoint` | → ESP32 | **no** | 1 | setpoint in °F, e.g. `102` | v1 |

## v1 — local monitoring and control

Scope: the board joins the local WiFi network, publishes current status to
the broker, and accepts commands from it. Node-RED renders the dashboard and
drives the controls for a phone on the same network. No internet routing.

### Telemetry

- **Transport**: MQTT publish to Mosquitto. The board publishes; it does not
  serve. This is strictly less firmware than an on-device HTTP server — no
  request handlers, no connection handling, no client tracking, since the
  broker does all of that.
- **Push, not poll**: the RS485 task publishes `hottub/state` after each
  poll cycle. Because the topic is retained, a phone or a restarted Node-RED
  gets current values immediately on connect rather than waiting for the
  next update.
- **No shared "latest reading" state needed.** The task that produces the
  reading is the task that publishes it, so there's no struct-behind-a-mutex
  handoff between a polling loop and a server task. (This was an open
  question under the earlier HTTP design; the MQTT shape removes it.)
- **Payload**: one JSON blob on `hottub/state`, not a topic per field. Keeps
  the firmware to a single serializer; Node-RED splits fields out for the
  dashboard with a `json` node.
  ```
  hottub/state  {"water_temp_F":102.4,"ph":7.4,...}
  ```
- **LWT** on `hottub/status` so the dashboard can distinguish live readings
  from stale ones left behind by a board that dropped off.

### Commands

- **Commands are absolute, not relative.** `setpoint = 102`, never
  `temp_up`. QoS 1 is at-least-once, so a redelivered relative command would
  silently apply twice.
- **Never retain command topics.** A retained command is redelivered to the
  ESP32 on every reconnect, replaying the last setpoint change after every
  power blip. Publish commands with `retain=false`. (State stays retained —
  the asymmetry is deliberate.)
- **Command → apply → echo.** The command topic is not state. The ESP32
  validates the command, writes to the spa pack over RS485, and then
  publishes what actually took effect to `hottub/state`. The dashboard binds
  to the state topic, so a rejected or failed command visibly doesn't take
  rather than showing optimistic UI.
- **The ESP32 is the authority.** Every inbound command is range- and
  sanity-checked in firmware before it reaches the bus — the broker will
  happily deliver a typo'd `mosquitto_pub` of `999`. Note also that the
  Mach-7 pack keeps its own thermostat and high-limit logic, and the tub's
  own panel keeps working regardless, so neither the Pi nor this board sits
  in a safety-critical path. That's not a reason to skip the checks.
- **Resubscribe on every `MQTT_EVENT_CONNECTED`**, unless clean-session is
  disabled — subscriptions don't survive a clean-session reconnect.

### Addressing, credentials and auth

- **Addressing**: the ESP32 needs no fixed address of its own, since nothing
  connects to it — only the *broker* needs a stable address (static IP or
  DHCP reservation on the Pi), hardcoded at build time.
- **Credentials**: WiFi SSID/password and broker host/user/password
  hardcoded at build time.
- **Auth, even on the LAN**: because v1 can *change the tub's setpoint*,
  "everyone on my WiFi is trusted" is a weaker assumption than it was for a
  read-only version — a guest on the WiFi, or anything compromised on it,
  can drive the tub. Set a per-device username/password on Mosquitto and
  turn on Node-RED's dashboard auth (`httpNodeAuth`) and editor auth
  (`adminAuth`) from the start. They're config, not code, and v2 needs them
  anyway.

Explicit non-goals for v1: access from outside the local network, TLS,
history/logging.

### Open implementation questions

- **Credentials in git**: hardcoding at build time is fine, but not directly
  in a tracked file. Use a gitignored header (e.g. `wifi_credentials.h`,
  with a checked-in `.example` template) rather than literal `#define`s in
  tracked source.
- **Publish cadence**: every RS485 poll, on change, or rate-limited? Water
  temp barely moves; publishing a full blob at poll rate is mostly redundant
  traffic, but on-change needs a deadband so noise on the pH ADC doesn't
  publish continuously.
- **Broker unreachable**: `esp-mqtt` reconnects on its own, but decide what
  the RS485 task does meanwhile — almost certainly keep polling and drop
  updates rather than buffering, since stale readings have no value.
- **Which commands beyond setpoint** (pump/jets/lights/filter cycle) depends
  on what the Mach-7 RS485 protocol actually exposes — to be filled in once
  the bus is decoded.

### Implementation order within v1

Telemetry first, commands second — not because they're separate versions,
but because decoding the bus has to happen before writing to it is even
possible, and a read-only firmware is the tool that does the decoding. Get
`hottub/state` publishing real values, confirm the frame format is
understood, then add the command path.

## v2 — remote access

Scope: reach the same dashboard and the same controls from outside the LAN.

Because v1 already put the broker in the middle and the command path
through it, **v2 requires no firmware change at all** — the ESP32 is
feature-complete at v1. This is a Pi deployment step, not a board revision.

Decisions:
- **Remote access**: reach the Pi over a VPN (Tailscale/WireGuard) rather
  than forwarding any port. Nothing — not 1883, not the Node-RED dashboard —
  is published to the open internet. The ESP32's exposure does not change:
  it still only makes an outbound connection to a LAN broker.
- **TLS**: optional on-LAN given the VPN boundary; if added, the ESP32
  bundles the CA cert. This is the one item that would touch firmware.
- **Auth**: already in place from v1, which is the point of doing it there.

Open question:
- **History/logging**: not scoped. Node-RED can fan `hottub/state` out to
  InfluxDB/SQLite for graphs and alerting ("pH drifted", "heater on 6h")
  with no firmware change, whenever it's wanted.

## Rejected: on-device HTTP server

An earlier version of this plan had v1 serving JSON from an on-device
`esp_http_server` endpoint (`GET /status`), polled by the phone, with MQTT
arriving only in v2. Dropped, because it made the v1→v2 step a transport
rewrite rather than a scope increase, and because it was the more complex of
the two for v1's own goal: an HTTP server needs request handlers and shared
state between the server and RS485 tasks, where publishing needs neither.

It also could not have carried v1's command path: taking commands over HTTP
means accepting inbound connections to the board, which is exactly what the
broker-in-the-middle design exists to avoid.

A small `/status` endpoint could still be added later as a Pi-independent
debug view. It isn't needed for bring-up — UART logging covers that — so
it's not scoped here.

## Consequences

- **A second box is in the chain.** Both monitoring *and* control now depend
  on the Pi staying up — boot it from an SSD rather than an SD card. The
  tub's own panel keeps working regardless, by design, so the failure mode
  is "no remote control", not "no control".
