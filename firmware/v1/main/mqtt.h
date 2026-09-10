#pragma once

// Starts the client and returns; esp-mqtt connects and reconnects on its own.
void mqtt_start(void);

// Publishes one status blob. Non-blocking and best-effort: a stale reading is
// worth nothing, so an unreachable broker drops the update rather than
// stalling the caller.
void mqtt_publish_state(const char *json);
