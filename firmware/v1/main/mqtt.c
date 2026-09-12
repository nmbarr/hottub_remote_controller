#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
#error "Copy main/wifi_credentials.h.example to main/wifi_credentials.h and fill it in"
#endif

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt.h"

#define TOPIC_STATUS "hottub/status"
#define TOPIC_STATE "hottub/state"
#define TOPIC_CMD_SUB "hottub/cmd/+"
#define TOPIC_STATE_PREFIX "hottub/state/"

#define TOPIC_BUFFER_SIZE 64
#define PAYLOAD_BUFFER_SIZE 128

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;

static mqtt_cmd_handler_t s_cmd_cb;
static mqtt_connected_handler_t s_connected_cb;

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
  esp_mqtt_event_handle_t event = event_data;

  switch ((esp_mqtt_event_id_t)event_id)
  {
  case MQTT_EVENT_CONNECTED:
  {
    // The LWT only covers vanishing; coming back is ours to announce.
    // Retained so a dashboard connecting later sees the current state.
    esp_mqtt_client_publish(s_client, TOPIC_STATUS, "online", 0, 1, 1);
    ESP_LOGI(TAG, "connected to " MQTT_BROKER_URI);

    int msg_id = esp_mqtt_client_subscribe(s_client, TOPIC_CMD_SUB, 1);
    if (msg_id < 0)
    {
      ESP_LOGE(TAG, "subscribe to %s failed (%d)", TOPIC_CMD_SUB, msg_id);
    }

    // Last, so anything it republishes lands after we are subscribed and have
    // said we are online. Fires on every reconnect, not just the first.
    if (s_connected_cb != NULL)
    {
      s_connected_cb();
    }
    break;
  }

  case MQTT_EVENT_DATA:
  {
    // A payload too big for one event arrives in pieces, and only the first
    // piece carries the topic. Nothing here to dispatch on.
    if (event->topic_len == 0)
    {
      ESP_LOGW(TAG, "ignoring %d-byte payload fragment", event->data_len);
      break;
    }

    // Commands are a few bytes. Anything arriving split is a mistake, and
    // reassembling it would mean holding state between events for no gain.
    if (event->data_len != event->total_data_len)
    {
      ESP_LOGW(TAG, "ignoring split payload (%d of %d bytes)",
               event->data_len, event->total_data_len);
      break;
    }

    // Strictly smaller, not just smaller-or-equal: the copies below need one
    // byte beyond the data for the terminator.
    if (event->topic_len >= TOPIC_BUFFER_SIZE ||
        event->data_len >= PAYLOAD_BUFFER_SIZE)
    {
      ESP_LOGW(TAG, "ignoring oversized message (topic %d, payload %d)",
               event->topic_len, event->data_len);
      break;
    }

    // esp-mqtt hands out pointer+length into its own receive buffer, not C
    // strings. Copying once here spares every caller the length-aware
    // comparisons, at the cost of two short-lived buffers on this task.
    char topic_buf[TOPIC_BUFFER_SIZE];
    char payload_buf[PAYLOAD_BUFFER_SIZE];

    memcpy(topic_buf, event->topic, event->topic_len);
    topic_buf[event->topic_len] = '\0';

    memcpy(payload_buf, event->data, event->data_len);
    payload_buf[event->data_len] = '\0';

    // Before the dispatch, so a handler that hangs still leaves a trace of
    // what it was handed.
    ESP_LOGI(TAG, "cmd %s = %s", topic_buf, payload_buf);

    if (s_cmd_cb == NULL)
    {
      ESP_LOGW(TAG, "no command handler registered, dropping");
      break;
    }

    s_cmd_cb(topic_buf, payload_buf);
    break;
  }

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "disconnected");
    break;

  case MQTT_EVENT_ERROR:
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
    {
      ESP_LOGE(TAG, "transport error: sock_errno=%d",
               event->error_handle->esp_transport_sock_errno);
    }
    else
    {
      ESP_LOGE(TAG, "error: type=%d connect_return_code=%d",
               event->error_handle->error_type,
               event->error_handle->connect_return_code);
    }
    break;

  default:
    break;
  }
}

void mqtt_start(void)
{
  esp_mqtt_client_config_t cfg = {
      .broker.address.uri = MQTT_BROKER_URI,
      .credentials.username = MQTT_USERNAME,
      .credentials.authentication.password = MQTT_PASSWORD,
      .session.last_will.topic = TOPIC_STATUS,
      .session.last_will.msg = "offline",
      .session.last_will.qos = 1,
      .session.last_will.retain = 1,
      // Default is 120s, so the broker takes ~180s to notice a dead board and
      // the dashboard shows a confident "online" that whole time. Not so low
      // that a brief WiFi glitch flaps it.
      .session.keepalive = 30,
  };

  s_client = esp_mqtt_client_init(&cfg);
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                 mqtt_event_handler, NULL));
  ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
}

void mqtt_set_command_handler(mqtt_cmd_handler_t cb)
{
  s_cmd_cb = cb;
}

void mqtt_set_connected_handler(mqtt_connected_handler_t cb)
{
  s_connected_cb = cb;
}

static void publish_retained(const char *topic, const char *payload)
{
  if (s_client == NULL)
  {
    return;
  }

  // enqueue, not publish: a QoS 0 publish writes the socket in the calling
  // task, and the RS485 task cannot afford to block -- it would miss the
  // Ready window that is its only chance to transmit.
  int id = esp_mqtt_client_enqueue(s_client, topic, payload, 0, 0, 1, true);
  if (id < 0)
  {
    ESP_LOGW(TAG, "publish to %s dropped (%d)", topic, id);
  }
}

void mqtt_publish_state(const char *json)
{
  publish_retained(TOPIC_STATE, json);
}

void mqtt_publish_item_state(const char *item, const char *value)
{
  char topic[TOPIC_BUFFER_SIZE];

  // snprintf always terminates, but silently truncates. A truncated topic
  // would publish to the wrong place, which is worse than not publishing.
  int len = snprintf(topic, sizeof(topic), TOPIC_STATE_PREFIX "%s", item);
  if (len < 0 || len >= (int)sizeof(topic))
  {
    ESP_LOGE(TAG, "state topic for '%s' does not fit", item);
    return;
  }

  publish_retained(topic, value);
}
