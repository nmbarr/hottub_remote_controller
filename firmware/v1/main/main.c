#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "mqtt.h"

static const char *TAG = "uart_test";

// UART2: UART0 is the console, UART1's default pins are the SPI flash.
// Not GPIO1/3 despite the TX/RX silkscreen -- those reach the CP2102N, so
// the ROM bootloader's banner would land on the spa bus at every reset.
#define RS485_UART UART_NUM_2
#define RS485_TX_GPIO 17  // -> transceiver TXD (DI)
#define RS485_RX_GPIO 16  // <- transceiver RXD (RO)
#define RS485_DE_GPIO 4   // -> transceiver EN  (DE + RE)

#define RS485_BAUD 115200
#define RS485_RX_BUF 1024  // must exceed the 128-byte hardware FIFO

#define LED_GPIO 19  // Green LED

#define READ_TIMEOUT_MS 100
#define IDLE_GIVEUP_MS 5000
#define IDLE_GIVEUP_READS (IDLE_GIVEUP_MS / READ_TIMEOUT_MS)

#define CAPTURE_BYTES 2048

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

static void rs485_init(void)
{
  uart_config_t uart_config = {
      .baud_rate = RS485_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      // Must stay disabled: flow control would claim RTS, which half-duplex
      // mode needs for the direction line.
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(RS485_UART, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(RS485_UART, RS485_TX_GPIO, RS485_RX_GPIO,
                               RS485_DE_GPIO, UART_PIN_NO_CHANGE));

  // tx_buffer_size 0 makes writes block until the bytes reach the FIFO.
  ESP_ERROR_CHECK(uart_driver_install(RS485_UART, RS485_RX_BUF, 0, 0, NULL, 0));

  // Must follow uart_driver_install. Kept on in this receive-only build so
  // the driver holds the direction line low instead of GPIO4 floating.
  ESP_ERROR_CHECK(uart_set_mode(RS485_UART, UART_MODE_RS485_HALF_DUPLEX));
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

  rs485_init();

  // Collect first, dump once. Hexdumping as bytes arrive costs ~5x the
  // bandwidth we receive at, and the console is the same 115200.
  static uint8_t capture[CAPTURE_BYTES];
  int offset = 0;
  int idle = 0;
  const char *why = "buffer full";

  ESP_LOGI(TAG, "listening on UART%d @ %d 8N1, capturing %d bytes",
           RS485_UART, RS485_BAUD, (int)sizeof(capture));

  while (offset < (int)sizeof(capture))
  {
    int n = uart_read_bytes(RS485_UART, capture + offset,
                            sizeof(capture) - offset,
                            pdMS_TO_TICKS(READ_TIMEOUT_MS));
    if (n < 0)
    {
      ESP_LOGE(TAG, "uart_read_bytes failed (%d)", n);
      why = "read error";
      break;
    }
    else if (n == 0)
    {
      if (++idle >= IDLE_GIVEUP_READS)
      {
        why = "idle timeout";
        break;
      }
      else
      {
        continue;
      }
    }
    else
    {
      idle = 0;
      offset += n;
      // Timestamps on these give the arrival cadence the hexdump cannot.
      ESP_LOGI(TAG, "+%d", n);
    }
  }

  ESP_LOGI(TAG, "captured %d bytes (%s)", offset, why);

  // Stand-in for a decoded status blob, to prove the publish path end to end.
  char json[64];
  snprintf(json, sizeof(json), "{\"captured_bytes\":%d}", offset);
  mqtt_publish_state(json);

  if (offset > 0)
  {
    ESP_LOG_BUFFER_HEXDUMP(TAG, capture, offset, ESP_LOG_INFO);
  }
}
