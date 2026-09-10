#include <string.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"

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
  ESP_ERROR_CHECK(uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX));

  static uint8_t burst[512];
  memset(burst, 0x55, sizeof(burst));

  int bytes_per_second = uart_config.baud_rate / 10;
  int bursts_per_second = bytes_per_second / sizeof(burst);

  while (1)
  {
    // Roughly a second of continuous transmission, so EN stays asserted long
    // enough to see on an LED. uart_write_bytes blocks until the bytes reach
    // the FIFO, so the loop paces itself at line rate.
    int total = 0;
    for (int i = 0; i < bursts_per_second; i++)
    {
      int written = uart_write_bytes(uart_num, burst, sizeof(burst));
      if (written < 0)
      {
        ESP_LOGE(TAG, "write failed on burst %d", i);
        break;
      }
      total += written;
    }

    int expected = bursts_per_second * (int)sizeof(burst);
    if (total != expected)
    {
      ESP_LOGW(TAG, "burst: %d of %d bytes (short)", total, expected);
    }
    else
    {
      ESP_LOGI(TAG, "burst: %d bytes, EN should have been high ~1s", total);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
