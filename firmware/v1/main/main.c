#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "mqtt.h"
#include "rs485.h"

static const char *TAG = "main";

#define LED_GPIO 19  // Green LED

// The pin is an output, so gpio_get_level cannot read back what we drove.
// This is the only record of what the LED is doing.
static bool s_led_state;

static void led_set(bool on)
{
  s_led_state = on;
  gpio_set_level(LED_GPIO, on);

  // Report what the pin is actually doing rather than letting the dashboard
  // assume its own click worked. No-ops before the client exists, which is
  // why on_mqtt_connected republishes.
  mqtt_publish_item_state("led", on ? "on" : "off");
}

static void configure_led(void)
{
  gpio_reset_pin(LED_GPIO);
  // Set the GPIO as a push/pull output
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  // Drive a known level: gpio_reset_pin leaves the pin as an input, so until
  // something writes it the LED is whatever the pull-up left it at.
  led_set(false);
  ESP_LOGI(TAG, "GPIO%d configured as output for the green LED", LED_GPIO);
}

// Runs on the esp-mqtt task, so it dispatches rather than does: anything
// slower than a register write belongs on a queue to the task that owns the
// hardware. The LED is the exception that is genuinely free to do inline.
static void on_command(const char *topic, const char *payload)
{
  // Subscribed with a wildcard, so the last topic segment names the target.
  const char *target = strrchr(topic, '/');
  target = (target != NULL) ? target + 1 : topic;

  if (strcmp(target, "led") != 0)
  {
    ESP_LOGW(TAG, "no such command target: %s", target);
    return;
  }

  // Explicit on/off rather than a toggle: a redelivered or duplicated command
  // then lands on the same state instead of inverting it.
  if (strcmp(payload, "on") == 0)
  {
    led_set(true);
  }
  else if (strcmp(payload, "off") == 0)
  {
    led_set(false);
  }
  else
  {
    ESP_LOGW(TAG, "bad payload for %s: '%s'", target, payload);
  }
}

// The retained state topic is the board's claim about itself, so every
// reconnect is a chance to correct a stale one the broker is still serving.
static void on_mqtt_connected(void)
{
  mqtt_publish_item_state("led", s_led_state ? "on" : "off");
}

// Protocol discovery aid: the status broadcast repeats every second, so a
// frame is only worth dumping the first time its type shows up.
#define SEEN_TYPES_MAX 16

static uint16_t s_seen_types[SEEN_TYPES_MAX];
static int s_seen_count;

// Runs on the RS485 task. Logging only for now -- decoding the status
// broadcast into hottub/state is the next piece.
static void on_rs485_message(const uint8_t *body, size_t len)
{
  if (len < 3)
  {
    return;
  }

  uint16_t type = (uint16_t)((body[1] << 8) | body[2]);
  for (int i = 0; i < s_seen_count; i++)
  {
    if (s_seen_types[i] == type)
    {
      return;
    }
  }

  if (s_seen_count < SEEN_TYPES_MAX)
  {
    s_seen_types[s_seen_count++] = type;
  }

  ESP_LOGI(TAG, "new message type %02x %02x from %02x, %d bytes", body[1],
           body[2], body[0], (int)len);
  ESP_LOG_BUFFER_HEXDUMP(TAG, body, len, ESP_LOG_INFO);
}

static void nvs_init(void)
{
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

void app_main(void)
{
  // WiFi keeps its calibration data in NVS, so this has to come first.
  nvs_init();

  // Both before mqtt_start: the pin has to be an output and the handler has
  // to be registered before the first command can possibly arrive.
  configure_led();
  mqtt_set_command_handler(on_command);
  mqtt_set_connected_handler(on_mqtt_connected);

  wifi_init_sta();
  mqtt_start();

  rs485_set_message_handler(on_rs485_message);
  rs485_start();

  // Returning is fine: the WiFi, MQTT and RS485 tasks carry on without us.
}
