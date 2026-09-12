#pragma once

// Starts the client and returns; esp-mqtt connects and reconnects on its own.
void mqtt_start(void);

typedef void (*mqtt_cmd_handler_t)(const char *topic, const char *payload);
typedef void (*mqtt_connected_handler_t)(void);

// Registers the callback for inbound commands. Call before mqtt_start().
// topic and payload are NUL-terminated and only valid for the duration of
// the call. Runs on the esp-mqtt task: must not block.
void mqtt_set_command_handler(mqtt_cmd_handler_t cb);

// Registers a callback fired after each successful connect. Call before
// mqtt_start(). Runs on the esp-mqtt task: must not block. Use it to republish
// retained state -- a reconnect is the board's chance to correct whatever the
// broker is still handing out from before it vanished.
void mqtt_set_connected_handler(mqtt_connected_handler_t cb);

// Publishes one status blob. Non-blocking and best-effort: a stale reading is
// worth nothing, so an unreachable broker drops the update rather than
// stalling the caller.
void mqtt_publish_state(const char *json);

// Publishes one item's state, retained, under hottub/state/<item>. Retained
// because a dashboard that connects later has no other way to learn what the
// board is doing. Same best-effort contract as mqtt_publish_state.
void mqtt_publish_item_state(const char *item, const char *value);
