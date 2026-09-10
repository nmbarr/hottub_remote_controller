#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
#error "Copy main/wifi_credentials.h.example to main/wifi_credentials.h and fill it in"
#endif

#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt.h"

#define TOPIC_STATUS "hottub/status"
#define TOPIC_STATE  "hottub/state"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
  esp_mqtt_event_handle_t event = event_data;

  switch ((esp_mqtt_event_id_t)event_id)
  {
  case MQTT_EVENT_CONNECTED:
    // The LWT only covers vanishing; coming back is ours to announce.
    // Retained so a dashboard connecting later sees the current state.
    esp_mqtt_client_publish(s_client, TOPIC_STATUS, "online", 0, 1, 1);
    ESP_LOGI(TAG, "connected to " MQTT_BROKER_URI);
    break;

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

void mqtt_publish_state(const char *json)
{
  if (s_client == NULL)
  {
    return;
  }

  // enqueue, not publish: a QoS 0 publish writes the socket in the calling
  // task, and the RS485 task cannot afford to block -- it would miss the
  // Ready window that is its only chance to transmit.
  int id = esp_mqtt_client_enqueue(s_client, TOPIC_STATE, json, 0, 0, 1, true);
  if (id < 0)
  {
    ESP_LOGW(TAG, "state publish dropped (%d)", id);
  }
}
