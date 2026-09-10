#include <string.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "freertos/projdefs.h"
#include "hal/uart_types.h"

static const char *TAG = "uart_test";

void app_main(void)
{

  const uart_port_t uart_num = UART_NUM_2;

  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  // Configure UART Parameters
  ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));

  //Set UART pins (RX: GPIO16, TX: GPIO17, RTS: GPIO4)
  ESP_ERROR_CHECK(uart_set_pin(uart_num, 17, 16, 4, UART_PIN_NO_CHANGE));

  // Install the driver
  int uart_rx_buffer_size = 256;
  int uart_tx_buffer_size = 0;
  int uart_queue_size = 0;
  int uart_intr_alloc_flags = 0;
  ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_rx_buffer_size, uart_tx_buffer_size, uart_queue_size, NULL, uart_intr_alloc_flags));

  // Set the RS485 Half Duplex
  uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX);

  // Discard anything sitting in the buffer prior to reading
  uart_flush_input(uart_num);

  // Send a known pattern out TX and read it back on RX through the jumper.
  // 0x7e and 0x00 are in there deliberately: the Balboa frame delimiter and a
  // NUL, the two bytes most likely to be mangled somewhere in the path.
  static const uint8_t pattern[] = {0x7e, 0x00, 0xab};
  uint8_t rx[64];

  int written = uart_write_bytes(uart_num, pattern, sizeof(pattern));
  int received = uart_read_bytes(uart_num, rx, sizeof(rx), pdMS_TO_TICKS(50));

  // Diagnostics unconditionally, so a passing run still shows what happened.
  ESP_LOGI(TAG, "written=%d received=%d", written, received);
  ESP_LOG_BUFFER_HEXDUMP(TAG, pattern, sizeof(pattern), ESP_LOG_INFO);
  if (received > 0)
  {
    ESP_LOG_BUFFER_HEXDUMP(TAG, rx, received, ESP_LOG_INFO);
  }

  // Check different failure modes and log
  if (written != (int)sizeof(pattern))
  {
    ESP_LOGE(TAG, "FAIL: short write, %d of %d bytes", written, (int)sizeof(pattern));
  }
  else if (received < 0)
  {
    ESP_LOGE(TAG, "FAIL: read error");
  }
  else if (received != written)
  {
    ESP_LOGE(TAG, "FAIL: sent %d bytes, got %d back", written, received);
  }
  else if (memcmp(pattern, rx, sizeof(pattern)) != 0)
  {
    ESP_LOGE(TAG, "FAIL: data mismatch");
  }
  else
  {
    ESP_LOGI(TAG, "PASS: %d bytes looped back intact", received);
  }
}
