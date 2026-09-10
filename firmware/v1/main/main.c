#include <string.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "uart_test";

// UART2: UART0 is the console, UART1's default pins are the SPI flash.
// Not GPIO1/3 despite the TX/RX silkscreen -- those reach the CP2102N, so
// the ROM bootloader's banner would land on the spa bus at every reset.
#define RS485_UART        UART_NUM_2
#define RS485_TX_GPIO     17   // -> transceiver TXD (DI)
#define RS485_RX_GPIO     16   // <- transceiver RXD (RO)
#define RS485_DE_GPIO     4    // -> transceiver EN  (DE + RE)

#define RS485_BAUD        115200
#define RS485_RX_BUF      1024  // must exceed the 128-byte hardware FIFO

#define READ_TIMEOUT_MS   100
#define IDLE_GIVEUP_MS    5000
#define IDLE_GIVEUP_READS (IDLE_GIVEUP_MS / READ_TIMEOUT_MS)

#define CAPTURE_BYTES     2048

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

void app_main(void)
{
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
  if (offset > 0)
  {
    ESP_LOG_BUFFER_HEXDUMP(TAG, capture, offset, ESP_LOG_INFO);
  }
}
