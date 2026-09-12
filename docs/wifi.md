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
 (clock/data +               (broker,          (flows,        (phone)
  buttons, on the             on the Pi)        logic, UI)
  panel harness)
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
- **Firmware side** (stale): this bullet described a from-scratch `esp-mqtt`
  (`mqtt_client`) firmware, declared as a managed component in the
  now-deleted `firmware/v1`. That firmware was replaced by
  `firmware/esphome-spa` — a vendored ESPHome fork that talks to Home
  Assistant's native API instead, not MQTT. See its own README.

## Topside panel interface

The topside is a **Balboa VL240**: a stock Balboa ValueLine keypad, where
"AquaRest"/"Dream Maker" is only the self-adhesive overlay (DreamMaker p/n
407016) stuck on the front. It reaches the pack over an 8-pin RJ45 on a 7ft
cord.

That harness is **not** an RS485 packet bus, and that fact shapes everything
below. On GS-series packs a VL2xx/VL4xx topside is a dumb terminal: the pack
owns all the logic and clocks the panel's 7-segment glass directly, while the
four membrane switches return to the pack as raw contacts. There is no
addressing, no framing, no CRC, and nothing for this board to transmit.

This supersedes an earlier plan built on the reverse-engineered Balboa
RS485 protocol ([ccutrer/balboa_worldwide_app][bwa]) and a MAX3485
transceiver interposed between panel and pack. That protocol is real, but it
belongs to Balboa's BP-series systems; it is not what this pack speaks to
this panel.

[bwa]: https://github.com/ccutrer/balboa_worldwide_app

### Connector

| RJ45 pin | Function | Direction |
| ---: | --- | --- |
| 1 | VIN | power |
| 2 | Warm button | ESP32 → pack |
| 3 | Light button | ESP32 → pack |
| 4 | GND | — |
| 5 | Display data | pack → ESP32 |
| 6 | Clock | pack → ESP32 |
| 7 | Jets/blower button | ESP32 → pack |
| 8 | Cool button | ESP32 → pack |

Six GPIO, not four: four to press buttons and two to read the display.
Dropping pins 5 and 6 would leave control with no monitoring — no water
temp, no setpoint, no heater state, no error codes, and nothing to put on a
dashboard.

Pin 7 is whichever function the overlay prints on the fourth switch; the
AquaRest overlay says blower where Balboa's stock one says jets. Same
switch either way.

### Electrical

RJ45 pin 1 is **+5V** — kgstorm's wiring diagram labels it outright, and he
feeds the DevKit's VIN from it. Measure it before trusting that on this pack.

- **Clock and data** are 5V and need dividing down. His two sources disagree
  slightly and both sit tighter than they need to:

  | Source | Top / bottom | At the GPIO |
  | --- | --- | --- |
  | wiring diagram | 2.2k / 4.7k | 3.41V |
  | PCB BOM (R1–R4) | 4.7k / 10k | 3.40V |
  | **used here** | **6.8k / 10k** | **2.98V** |

  The ESP32's absolute maximum is 3.6V, so 3.4V works — but a rail sitting
  at 5.25V puts it at 3.58V. 6.8k/10k keeps the 10k leg and buys ~0.6V of
  headroom for the price of one resistor value.
- **Use input-only GPIOs for clock and data** (GPIO34–39). They have no
  internal pull-ups, so pull externally (~10k). Keep a small series resistor
  (100–220Ω) on each for edge-ringing and ESD — not current limiting — and
  especially on the clock line, which takes an interrupt on every edge.

### Button injection

Four **PC817B** optocouplers, one per button. The transistor goes across the
same two points the membrane switch bridges: you are adding a second switch
in parallel with the panel's, not driving the line.

| PC817 pin | | Connects to |
| ---: | --- | --- |
| 1 | Anode | ESP32 GPIO, through 220Ω |
| 2 | Cathode | ESP32 GND |
| 3 | Emitter | RJ45 button line |
| 4 | Collector | RJ45 pin 1 (+5V) |

Pin 1 is marked with a dot or chamfer.

| Opto | GPIO | Emitter → RJ45 | Button |
| --- | --- | --- | --- |
| U1 | 25 | pin 2 | Warm |
| U2 | 26 | pin 8 | Cool |
| U3 | 27 | pin 3 | Light |
| U4 | 32 | pin 7 | Jets/blower |

**Orientation is easy to get backwards.** Collector to +5V, emitter to the
button line, never the reverse. That makes it an emitter follower, which
will not hard-saturate — but the button line is a high-impedance divider
node (it idles at 2.5V off a 5V rail), and a PC817B at ~10mA of LED current
sources far more than that node needs. It pulls to within a couple hundred
mV of 5V.

**220Ω on the LED, not 100Ω.** His two sources disagree here too, and the
breadboard value is the right one: 220Ω gives (3.3 − 1.15)/220 ≈ 9.8mA,
where the PCB's 100Ω gives 21mA — past the ESP32's 20mA recommended per-pin
source current, for no benefit.

**Specify the B rank.** PC817B is CTR 130–260%, so 10mA in yields 13–26mA
out. Ungraded PC817 is 50–600%; at the bottom of that spread the part that
gets soldered behaves differently from the one that was prototyped with.

#### No resistor between +5V and the collector

Deliberately. His PCB has none — all ten resistors are accounted for as two
dividers, two signal series and four LED series.

The membrane switch being emulated is a bare short, so whatever current
flows on a real press is what the pack was designed to sink; putting a
resistor in the path makes the injected press electrically *different* from
a real one. Current is set by the load anyway — roughly (5V − Vce) over the
pack's pull-down, on the order of 0.5mA for a 10k leg, against the PC817's
50mA maximum.

Headroom is the scarce resource. There is only 2.5V between idle and
pressed, the emitter follower already eats some of it, and the pack's logic
threshold is unknown. A series resistor stacks another drop onto that budget
and buys the worst failure mode available: presses that work on the bench
and go intermittent cold, wet, or once contact resistance has crept.

**Measure before committing this to a schematic.** With the panel plugged
in, put a milliammeter between a button line and +5V — that is a real press,
current-metered. Sub-milliamp is expected and confirms the above. Tens of mA
means the topology is not what is documented here; stop and re-probe. Check
the idle level on all four button pins while there rather than assuming 2.5V
across the board.

#### Isolation, or the lack of it

This circuit is **not galvanically isolated**. The ESP32 takes VIN from RJ45
pin 1 and ground from pin 4, so ESP32 ground *is* spa ground. The optos buy
a floating contact, not isolation: a switch that works regardless of the
levels on either side, cannot backfeed 5V into a 3.3V pin, and cannot assert
a button from a floating or bootstrapping GPIO. Those are real and are most
of the value — but they are not isolation, and the distinction matters for a
board in a wet bay sharing a ground with a heater.

Real isolation means powering the ESP32 from its own supply and leaving RJ45
pins 1 and 4 off the board entirely. The catch is that the clock/data
dividers then lose their ground reference, so those two signals need optos
or a digital isolator as well and the divider approach stops working. That
is a fork worth deciding before layout, not after.

A stuck-on opto is not the reason to add parts. It is electrically
indistinguishable from a stuck membrane switch, which is a failure the pack
already has to tolerate. The realistic fault is a firmware bug parking a
press, and the fix for that is in firmware: bound every press with a hard
maximum duration enforced by a timer that is not the code path that started
the press. GPIO25/26/27/32 are boot-safe, which is why they were chosen —
but confirm none of them float high before `app_main` runs, because an opto
does not care whether the level was intentional.

### Frame format

Synchronous and clocked, so there is no baud rate to get wrong:

| | |
| --- | --- |
| Clock | ~16µs high, ~21µs low (~27kHz) |
| Frame | 24 bits, MSB first |
| Frame gap | ~19ms — i.e. ~50Hz, because this is a display refresh, not a message |

The 24 bits split into four fields:

| Field | Bits | Meaning |
| --- | ---: | --- |
| `p1` | 7 | bits 5 and 4 both set = hundreds digit `1` (temp ≥ 100°F); **bit 2 = heater on**; `(p1 & 0x4B) == 0` is the validity check |
| `p2` | 7 | tens digit, raw 7-segment bitmap |
| `p3` | 7 | ones digit, raw 7-segment bitmap |
| `p4` | 3 | bit 2 = pump/jets, bit 1 = light, bit 0 always 0 |

Decoding means reverse-mapping segment patterns to characters. This is
reading the display, not parsing a protocol, and the consequences run
through the whole design: error codes arrive as the two-character string the
panel shows (`HH`, `FL`, …), and heat mode has to be caught as `St`/`Ec`/`SL`
flash past during mode selection. Anything the panel cannot display does not
exist to this firmware.

Because it is a display, frames also repeat at 50Hz and flicker through
transient states. Decoded values need a stability filter — N identical
consecutive frames before publishing — rather than being trusted per frame.

### Reference implementation

[kgstorm/Balboa-GS100-with-VL260-topside][kg] (ESPHome, ESP32 DevKit V1) is
the closest prior art: same interface, tested against VL200- and VL400-series
panels on a GS100, with a PCB and an enclosure in-tree. [kgstorm/Balboa-GS5xx][kg5]
covers panels with a single Temp button instead of Warm/Cool.
[MagnusPer/Balboa-GS510SZ][mp] is an independent implementation for the
GS510SZ and useful as a second opinion on the decode.

[kg]: https://github.com/kgstorm/Balboa-GS100-with-VL260-topside
[kg5]: https://github.com/kgstorm/Balboa-GS5xx
[mp]: https://github.com/MagnusPer/Balboa-GS510SZ

### Still unconfirmed

kgstorm's pack is a GS100; this one is a Mach 7 (RS-81), sold as a "Mach 7
Balboa 56795 Pack". The VL240 is listed as compatible with GS100, GS500Z,
GS501Z and GS511Z, which is good evidence they are the same family — but it
is still evidence from parts listings, which is exactly the kind of
inference that produced the RS485 plan this section replaces.

Confirm on the hardware before building anything:

- Scope or logic-analyze RJ45 pins 5 and 6 and look for 24-bit bursts
  separated by ~19ms gaps. Read-only and non-destructive.
- Measure pin 1 before wiring it to the DevKitC's VIN.
- Confirm the button idle/pressed levels on pins 2, 3, 7 and 8 rather than
  assuming 2.5V/5V.

### Consequence: the setpoint is a closed loop

The pack exposes no way to *write* a setpoint. Warm and Cool step it, and
while stepping, the display shows the set temperature instead of the
measured one. So setting an absolute temperature is a loop in firmware:
press Cool to make the pack reveal the current setpoint, read it back off
the display feed, step toward the target, confirm. kgstorm re-reads the
setpoint every 6 hours with a Cool→Light sequence for the same reason — left
alone, the display only ever shows measured temp.

This does not change the MQTT contract below; `hottub/cmd/setpoint` stays
absolute. It moves the work into the firmware, where it belongs.

## Topic map

| Topic | Dir | Retain | QoS | Payload | Version |
| --- | --- | --- | --- | --- | --- |
| `hottub/status` | ESP32 → | yes | 1 | `online` / `offline` (LWT) | v1 |
| `hottub/state` | ESP32 → | yes | 0 | JSON status blob | v1 |
| `hottub/cmd/setpoint` | → ESP32 | **no** | 1 | setpoint in °F, e.g. `102` | v1 |
| `hottub/cmd/light` | → ESP32 | **no** | 1 | `on` / `off` / `toggle` | v1 |
| `hottub/cmd/jets` | → ESP32 | **no** | 1 | `on` / `off` / `toggle` | v1 |

`light` and `jets` map to single button presses, so they are cheap. The
panel reports both back in `p4`, which is what makes `on`/`off` (rather than
only `toggle`) implementable: read the current state, press only if it
differs.

## v1 — local monitoring and control

Scope: the board joins the local WiFi network, publishes current status to
the broker, and accepts commands from it. Node-RED renders the dashboard and
drives the controls for a phone on the same network. No internet routing.

### Telemetry

- **Transport**: MQTT publish to Mosquitto. The board publishes; it does not
  serve. This is strictly less firmware than an on-device HTTP server — no
  request handlers, no connection handling, no client tracking, since the
  broker does all of that.
- **Push, not poll**: the panel task publishes `hottub/state` when a decoded
  value changes and clears its stability filter. Because the topic is
  retained, a phone or a restarted Node-RED gets current values immediately
  on connect rather than waiting for the next update. Note that the display
  feed itself arrives at ~50Hz; that cadence is a refresh rate, not a
  publish rate.
- **No shared "latest reading" state needed** *in this direction*. The task
  that produces the reading is the task that publishes it, so there's no
  struct-behind-a-mutex handoff between a decoder loop and a server task.
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
  silently apply twice. This matters more here than it would on a packet
  bus: the *mechanism* underneath is relative — stepping Warm and Cool — so
  the firmware is the only thing standing between an at-least-once delivery
  and a tub that drifts a degree every redelivery. Absolute intent in,
  closed-loop stepping inside, never relative commands over the wire.
- **Never retain command topics.** A retained command is redelivered to the
  ESP32 on every reconnect, replaying the last setpoint change after every
  power blip. Publish commands with `retain=false`. (State stays retained —
  the asymmetry is deliberate.)
- **The MQTT callback does not press buttons.** Applying a setpoint is a
  multi-second sequence — press, hold ~200ms, release, wait for the display
  to settle, read, decide, repeat — so it cannot run inside an `esp-mqtt`
  event handler. The handler validates the command, hands the target to the
  panel task, and returns; that task owns the button GPIOs and runs the
  loop. This is the one place the two tasks share state — a pending-command
  slot in the command direction, guarded by a mutex or written as a queue —
  which is why the telemetry-direction claim above is scoped the way it is.
- **One writer for the buttons.** The panel task owning all four GPIOs is
  not just tidiness: a setpoint walk in progress and a stray `cmd/light`
  both pressing buttons would interleave presses into the pack and desync
  the walk from the display it is reading. Serialise commands in that task's
  queue.
- **Command → apply → echo.** The command topic is not state. Once the panel
  task has pressed the buttons, what feeds `hottub/state` is the pack's own
  display output, decoded back off pins 5 and 6. The dashboard binds to the
  state topic, so a rejected or failed command visibly doesn't take rather
  than showing optimistic UI. The echo is unusually trustworthy here: it is
  literally what the tub is showing on its own panel, not a status field the
  firmware asked for.
- **The ESP32 is the authority.** Every inbound command is range- and
  sanity-checked in firmware before it reaches the bus — the broker will
  happily deliver a typo'd `mosquitto_pub` of `999`; the protocol's own
  bounds are 80–104°F in high range. Note also that the
  Mach-7 pack keeps its own thermostat and high-limit logic, and the tub's
  own panel keeps working regardless, so neither the Pi nor this board sits
  in a safety-critical path. That's not a reason to skip the checks — and
  a bounds check is also what stops a bad target turning into an unbounded
  press loop against a setpoint that has already hit the pack's own limit.
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
- **Publish cadence**: on change, or rate-limited? Publishing at the display's
  ~50Hz refresh is obviously wrong, and water temp barely moves — but
  on-change needs a deadband so noise on the pH ADC doesn't publish
  continuously, and the display's own flicker is already handled by the
  stability filter rather than by the publish rule.
- **Broker unreachable**: `esp-mqtt` reconnects on its own, but decide what
  the panel task does meanwhile — almost certainly keep decoding and drop
  updates rather than buffering, since stale readings have no value.
- **Stability thresholds**: how many identical consecutive frames before a
  value is trusted, and how long a transient `0x00` counts as "in set mode".
  kgstorm uses 3 frames and a 2s set-mode timeout; both are tuned against a
  GS100 and are worth re-measuring here rather than copying.
- **Which commands beyond setpoint**: the panel has four buttons, so the
  reachable command set is exactly what those four buttons can drive,
  including their press-and-hold and multi-press sequences. Filter cycle and
  heat mode are reachable but only through timed sequences (kgstorm reads
  mode with Cool→Light) — decide whether the added state machine is worth it
  for v1.

### Implementation order within v1

Telemetry first, commands second — and here that ordering is structural
rather than just prudent. The setpoint command *depends* on the decoder: the
closed loop cannot step toward a target without reading the setpoint back
off the display. A read-only firmware is not merely the safe first step, it
is the prerequisite.

It is also the cheap first step. Decoding requires only pins 5 and 6 —
two inputs and a divider, no optocouplers, no connection to anything the
pack can act on — so the entire protocol assumption can be proved or
disproved before a single button line is wired. Get `hottub/state`
publishing real water temp, then wire the buttons.

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
state between the server and panel tasks, where publishing needs neither.

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
