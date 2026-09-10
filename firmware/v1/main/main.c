#include <string.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
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
  int uart_rx_buffer_size = 1024;
  int uart_tx_buffer_size = 0;
  int uart_queue_size = 0;
  int uart_intr_alloc_flags = 0;
  ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_rx_buffer_size, uart_tx_buffer_size, uart_queue_size, NULL, uart_intr_alloc_flags));

  // Set the mode to RS485 Half Duplex
  ESP_ERROR_CHECK(uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX));

  // Capture raw bytes off the bus without interpreting them: enough to answer
  // whether the wiring works, whether 115200 8N1 is right, and whether the
  // pack really speaks Balboa. Collect first and dump once at the end --
  // hexdumping as we go would cost ~5x the bandwidth we are receiving at,
  // since the console is also 115200, and the RX ring would overflow.
  static uint8_t capture[2048];
  int offset = 0;
  int idle = 0;

  // Why the loop stopped. The three exits mean very different things.
  const char *why = "buffer full";

  ESP_LOGI(TAG, "listening on UART2 @ %d 8N1, capturing %d bytes",
           uart_config.baud_rate, (int)sizeof(capture));

  while (offset < (int)sizeof(capture))
  {
    int n = uart_read_bytes(uart_num, capture + offset, sizeof(capture) - offset, pdMS_TO_TICKS(100));
    if (n < 0)
    {
      ESP_LOGE(TAG, "uart_read_bytes failed (%d)", n);
      why = "read error";
      break;
    }
    else if (n == 0)
    {
      // Idle. Give up after ~5s of silence rather than blocking forever.
      if (++idle >= 50)
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
      // One short line per read: the timestamps give the arrival cadence,
      // which the deferred hexdump cannot show.
      ESP_LOGI(TAG, "+%d", n);
    }
  }

  ESP_LOGI(TAG, "captured %d bytes (%s)", offset, why);
  if (offset > 0)
  {
    ESP_LOG_BUFFER_HEXDUMP(TAG, capture, offset, ESP_LOG_INFO);
  }
}
