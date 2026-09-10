#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
#error "Copy main/wifi_credentials.h.example to main/wifi_credentials.h and fill it in"
#endif

#include "freertos/idf_additions.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "wifi.h"

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// Attempts before the first-connect wait gives up. Retrying never stops --
// the AP may simply be rebooting -- but the backoff caps so a down AP is not
// hammered.
#define FIRST_CONNECT_ATTEMPTS 5
#define RETRY_BACKOFF_MIN_MS   1000
#define RETRY_BACKOFF_MAX_MS   30000

static const char *TAG = "wifi station";

static int s_retry_num = 0;
static esp_timer_handle_t s_retry_timer;

static void retry_connect(void *arg)
{
  esp_wifi_connect();
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)event_data;
    // reason distinguishes a wrong password from a missing AP from weak signal.
    ESP_LOGW(TAG, "disconnected: reason=%d rssi=%d", e->reason, e->rssi);

    if (s_retry_num == FIRST_CONNECT_ATTEMPTS)
    {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    s_retry_num++;

    int backoff = RETRY_BACKOFF_MIN_MS << (s_retry_num < 5 ? s_retry_num : 5);
    if (backoff > RETRY_BACKOFF_MAX_MS)
    {
      backoff = RETRY_BACKOFF_MAX_MS;
    }
    // Scheduled, not slept: this runs in the event loop task, and blocking
    // here would stall every other event including IP and MQTT.
    esp_timer_start_once(s_retry_timer, (uint64_t)backoff * 1000);
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    s_retry_num = 0;

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
      ESP_LOGI(TAG, "got ip:" IPSTR " ssid:%s rssi:%d",
               IP2STR(&event->ip_info.ip), (char *)ap.ssid, ap.rssi);
    }
    else
    {
      ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    }
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init_sta(void)
{
  s_wifi_event_group = xEventGroupCreate();

  const esp_timer_create_args_t retry_args = {
      .callback = retry_connect,
      .name = "wifi_retry",
  };
  ESP_ERROR_CHECK(esp_timer_create(&retry_args, &s_retry_timer));

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &event_handler,
                                                      NULL,
                                                      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &event_handler,
                                                      NULL,
                                                      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta = {
          .ssid = WIFI_SSID,
          .password = WIFI_PASSWORD,
          .threshold.authmode = WIFI_AUTH_WPA2_PSK,
      },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT)
  {
    ESP_LOGI(TAG, "connected to %s", WIFI_SSID);
  }
  else
  {
    ESP_LOGE(TAG, "failed to connect to %s; still retrying in background",
             WIFI_SSID);
  }
}
