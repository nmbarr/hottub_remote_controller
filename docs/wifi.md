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
- **Firmware side**: `esp-mqtt` (`mqtt_client`). It shipped inside ESP-IDF
  through 5.x; in 6.x it moved out of core and is now the managed component
  `espressif/mqtt`, declared in `firmware/v1/main/idf_component.yml` and
  fetched at build time. Still nothing vendored into this repo — but it is a
  declared dependency now rather than something that is simply present, so
  `dependencies.lock` is committed to pin the resolved version.

## RS485 protocol

The spa side is a Balboa protocol bus. The Mach 7 (RS-81) pack is a Balboa
OEM design badged for DreamMaker/AquaRest — parts listings sell it as a
"Mach 7 Balboa 56795 Pack" — so the reverse-engineered Balboa protocol is
expected to apply. That's an inference from parts listings until it's
confirmed against the actual bus.

Reference: [ccutrer/balboa_worldwide_app][bwa] (Ruby, MIT per its gemspec —
there's no LICENSE file at the root, so credit it in any ported source).
`doc/protocol.md` there is the spec; `lib/bwa/crc.rb` and `lib/bwa/messages/`
are the reference implementation.

[bwa]: https://github.com/ccutrer/balboa_worldwide_app

What matters for this firmware:

| | |
| --- | --- |
| Framing | `0x7e [len] [type] [payload] [crc8] 0x7e` |
| CRC | CRC-8, **init `0x02`, final XOR `0x02`** — not a stock CRC-8 |
| Status | `ff af 13`, broadcast every second |
| Set temperature | `0a bf 20`, one byte, doubled if Celsius |
| Setpoint range | 80–104°F high range, 50–80°F low |
| Bus access | transmit **only** immediately after a Ready message, `10 bf 06` |

Serial settings: `doc/protocol.md` says 115200 8N1. The BWA README's ESPHome
snippet says 4800 / parity ODD, but that snippet is labelled `name: SDN` and
appears copy-pasted from a Somfy SDN config (4800/odd are Somfy's settings).
Go with 115200 8N1 and confirm on a scope before trusting either.

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
- **No shared "latest reading" state needed** *in this direction*. The task
  that produces the reading is the task that publishes it, so there's no
  struct-behind-a-mutex handoff between a polling loop and a server task.
  (This was an open question under the earlier HTTP design; the MQTT shape
  removes it for telemetry. Commands do need a handoff — see below.)
- **Payload**: one JSON blob on `hottub/state`, not a topic per field. Keeps
  the firmware to a single serializer; Node-RED splits fields out for the
  dashboard with a `json` node.
  ```
  hottub/state  {"water_temp_F":102.4,"ph":7.4,...}
  ```
- **LWT** on `hottub/status` so the dashboard can distinguish live readings
  from stale ones left behind by a board that dropped off. Keepalive is set
  to 30s rather than the 120s default: a broker waits 1.5x keepalive before
  declaring a client dead, so the default leaves the dashboard showing a
  confident `online` for three minutes after the board stops existing. Low
  enough to be useful, high enough that a brief WiFi glitch does not flap it.
  Note the LWT only covers vanishing — the board publishes `online` itself on
  every connect, retained, or the status stays `offline` after any reconnect.

### Commands

- **Commands are absolute, not relative.** `setpoint = 102`, never
  `temp_up`. QoS 1 is at-least-once, so a redelivered relative command would
  silently apply twice.
- **Never retain command topics.** A retained command is redelivered to the
  ESP32 on every reconnect, replaying the last setpoint change after every
  power blip. Publish commands with `retain=false`. (State stays retained —
  the asymmetry is deliberate.)
- **The MQTT callback does not touch the bus.** Balboa's bus is poll-driven:
  a device may transmit only in the window immediately after a Ready message
  (`10 bf 06`). So the `esp-mqtt` event handler validates the command, hands
  the setpoint to the RS485 task, and returns; that task transmits in its
  next Ready window. This is the one place the two tasks share state — a
  pending-command slot in the command direction, guarded by a mutex or
  written as a queue — which is why the telemetry-direction claim above is
  scoped the way it is.
- **Command → apply → echo.** The command topic is not state. Once the RS485
  task has actually sent the frame, the resulting `ff af 13` status broadcast
  is what feeds `hottub/state`. The dashboard binds to the state topic, so a
  rejected or failed command visibly doesn't take rather than showing
  optimistic UI — and the echo reflects the pack's own report, not what the
  firmware believes it sent.
- **The ESP32 is the authority.** Every inbound command is range- and
  sanity-checked in firmware before it reaches the bus — the broker will
  happily deliver a typo'd `mosquitto_pub` of `999`; the protocol's own
  bounds are 80–104°F in high range. Note also that the
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
- **Which commands beyond setpoint** (pump/jets/lights/filter cycle): start
  from `lib/bwa/messages/` in [the BWA repo][bwa], which has message classes
  for the ones that have been deciphered. Not all of them have been — confirm
  against the actual bus rather than assuming coverage.

### Implementation order within v1

Telemetry first, commands second — not because they're separate versions,
but because reading the bus correctly has to come before writing to it, and
a read-only firmware is the tool that proves the framing and CRC are right.
With the BWA protocol doc in hand this is mostly porting rather than
reverse-engineering, but the pack being Balboa-compatible is still an
assumption until frames actually decode. Get `hottub/state` publishing real
values off `ff af 13`, then add the command path.

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
