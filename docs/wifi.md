# WiFi sensor access

Goal: view live hot tub status (and, once v1.5's water chemistry sensors are
in, pH/ORP readings) from a phone instead of only via a wired debug
connection.

WiFi: the ESP32 has WiFi built into the SoC, so there's no separate WiFi
module or driver to write — connectivity and the HTTP server both come from
ESP-IDF's own `esp_wifi`/`esp_http_server` components, used directly rather
than vendored into this repo. (This replaces an earlier plan, from when the
board was STM32-based, to drive an external Inventek ISM43362 WiFi module
over SPI via ST's X-CUBE-WIFI1 middleware — moot now that WiFi is native to
the processor.)

## v1 — local, single device

Scope: the board joins a local WiFi network and serves current status to one
phone on the same network. No internet routing involved.

Decisions:
- **Transport**: minimal on-device HTTP server (`esp_http_server`). Phone
  hits a URL in a regular browser (or `curl`), board responds with current
  status as JSON.
  ```
  GET http://192.168.1.42/status

  {"water_temp_F":102.4,"ph":7.4,...}
  ```
- **Push vs. poll**: phone polls. Board has no need to track connected
  clients or push timing; fits naturally with the HTTP request/response
  model above.
- **Discovery**: hardcoded/static IP for v1 — configure a static IP (or a
  DHCP reservation) and read/type it in. No mDNS responder needed yet.
- **Credentials**: SSID/password hardcoded at build time.

Explicit non-goals for v1: authentication, multiple simultaneous clients,
access from outside the local network.

Open implementation questions (need deciding before/while implementing):
- **Credentials in git**: hardcoding SSID/password at build time is fine,
  but not directly in a tracked file. Use a gitignored header (e.g.
  `wifi_credentials.h`, with a checked-in `.example` template) rather than a
  literal `#define` in tracked source.
- **Where does the current reading live for the HTTP handler to read?**
  RS485 status (and later pH/ORP) polling and the HTTP handler will run as
  separate FreeRTOS tasks; need some shared "latest reading" state between
  them (e.g. a small struct behind a mutex, or a queue).
- **Blocking vs. non-blocking connection handling**: `esp_http_server` runs
  request handlers on their own task, so this is largely handled by the
  framework — but still need to decide how the RS485 polling loop and WiFi
  task interact so one doesn't starve the other.

## v2 — remote, anyone from anywhere

Scope: any client, not just one on the same local network, can check status
from anywhere on the internet.

This is a substantially bigger jump than v1, not just "open a port":
- The board can't safely be directly internet-exposed (no port-forwarding a
  bare embedded HTTP server onto the open internet) — likely needs the board
  to phone out to a relay/broker/backend service instead of accepting
  inbound connections directly.
- Needs some form of auth (this is no longer "if you're on my WiFi you're
  trusted").
- Needs a backend/service component this repo doesn't currently have any of
  (hosting, protocol choice — MQTT to a broker is a common fit for this kind
  of telemetry, but undecided).

Not started — v1's on-device data format/API should inform some of these
decisions once it exists.
