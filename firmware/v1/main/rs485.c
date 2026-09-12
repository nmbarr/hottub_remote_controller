#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rs485.h"

static const char *TAG = "rs485";

// UART2: UART0 is the console, UART1's default pins are the SPI flash.
// Not GPIO1/3 despite the TX/RX silkscreen -- those reach the CP2102N, so
// the ROM bootloader's banner would land on the spa bus at every reset.
#define RS485_UART UART_NUM_2
#define RS485_TX_GPIO 17  // -> transceiver TXD (DI)
#define RS485_RX_GPIO 16  // <- transceiver RXD (RO)
#define RS485_DE_GPIO 4   // -> transceiver EN  (DE + RE)

#define RS485_BAUD 115200
#define RS485_RX_BUF 1024  // must exceed the 128-byte hardware FIFO

// 7e LL <src> <type hi> <type lo> <payload...> CC 7e, where LL counts itself,
// the source, both type bytes, the payload and the CRC -- everything but the
// two delimiters. So the CRC sits at index LL from the start byte.
#define FRAME_DELIM 0x7e
#define FRAME_LEN_MIN 5  // a frame carrying no payload at all
#define FRAME_LEN_MAX 64

#define READ_CHUNK 128
#define READ_TIMEOUT_MS 100
#define STATS_INTERVAL_MS 30000

#define TASK_STACK 4096
// Above the default so the read loop keeps ahead of a 115200 bus, but below
// the WiFi and MQTT tasks, which this must not starve.
#define TASK_PRIO 10

typedef enum
{
  FRAME_WAIT_START,
  FRAME_WAIT_LEN,
  FRAME_WAIT_BODY,
  FRAME_WAIT_END,
} frame_state_t;

static rs485_msg_handler_t s_msg_cb;

static frame_state_t s_state;
static uint8_t s_frame[FRAME_LEN_MAX];  // s_frame[0] is the length byte
static int s_have;
static uint8_t s_len;

static uint32_t s_ok;
static uint32_t s_crc_bad;
static uint32_t s_resync;

// CRC-8 with the usual 0x07 polynomial, MSB first, but Balboa seeds it with
// 0x02 and XORs the result with 0x02 instead of the customary zeroes.
static uint8_t balboa_crc8(const uint8_t *data, int len)
{
  uint8_t crc = 0x02;

  for (int i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++)
    {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
  }

  return crc ^ 0x02;
}

static void frame_restart(frame_state_t state)
{
  s_state = state;
  s_have = 0;
}

static void frame_feed(uint8_t b)
{
  switch (s_state)
  {
  case FRAME_WAIT_START:
    if (b == FRAME_DELIM)
    {
      s_state = FRAME_WAIT_LEN;
    }
    break;

  case FRAME_WAIT_LEN:
    // Back-to-back frames may share a delimiter, and an idle bus repeats it,
    // so another 0x7e here is a start byte rather than a length.
    if (b == FRAME_DELIM)
    {
      break;
    }
    if (b < FRAME_LEN_MIN || b > FRAME_LEN_MAX)
    {
      s_resync++;
      frame_restart(FRAME_WAIT_START);
      break;
    }
    s_len = b;
    s_frame[0] = b;
    s_have = 1;
    s_state = FRAME_WAIT_BODY;
    break;

  case FRAME_WAIT_BODY:
    s_frame[s_have++] = b;
    // The length counts itself through the CRC, so the frame is complete once
    // we are holding that many bytes.
    if (s_have == s_len)
    {
      s_state = FRAME_WAIT_END;
    }
    break;

  case FRAME_WAIT_END:
    // Payload bytes are not escaped, so a 0x7e inside one can start a false
    // frame. Landing here on a non-delimiter is how that gets caught.
    if (b != FRAME_DELIM)
    {
      s_resync++;
      frame_restart(FRAME_WAIT_START);
      break;
    }

    if (balboa_crc8(s_frame, s_len - 1) != s_frame[s_len - 1])
    {
      s_crc_bad++;
      frame_restart(FRAME_WAIT_LEN);
      break;
    }

    s_ok++;
    if (s_msg_cb != NULL)
    {
      // Hand over src through payload: the length and CRC are framing, and
      // nothing above this cares about them.
      s_msg_cb(&s_frame[1], (size_t)(s_len - 2));
    }
    // Not WAIT_START: the delimiter just consumed may also open the next one.
    frame_restart(FRAME_WAIT_LEN);
    break;
  }
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

  // Must follow uart_driver_install. Keeps the driver in charge of the
  // direction line instead of leaving GPIO4 floating.
  ESP_ERROR_CHECK(uart_set_mode(RS485_UART, UART_MODE_RS485_HALF_DUPLEX));
}

static void rs485_task(void *arg)
{
  uint8_t chunk[READ_CHUNK];
  TickType_t last_stats = xTaskGetTickCount();

  ESP_LOGI(TAG, "listening on UART%d @ %d 8N1", RS485_UART, RS485_BAUD);

  for (;;)
  {
    int n = uart_read_bytes(RS485_UART, chunk, sizeof(chunk),
                            pdMS_TO_TICKS(READ_TIMEOUT_MS));
    if (n < 0)
    {
      ESP_LOGE(TAG, "uart_read_bytes failed (%d)", n);
      continue;
    }

    for (int i = 0; i < n; i++)
    {
      frame_feed(chunk[i]);
    }

    // Periodic, not per frame: the status broadcast alone is one a second,
    // and the console runs at the same 115200 as the bus.
    if (xTaskGetTickCount() - last_stats >= pdMS_TO_TICKS(STATS_INTERVAL_MS))
    {
      last_stats = xTaskGetTickCount();
      ESP_LOGI(TAG, "frames ok=%lu crc_bad=%lu resync=%lu", s_ok, s_crc_bad,
               s_resync);
    }
  }
}

void rs485_set_message_handler(rs485_msg_handler_t cb)
{
  s_msg_cb = cb;
}

void rs485_start(void)
{
  rs485_init();
  frame_restart(FRAME_WAIT_START);
  xTaskCreate(rs485_task, "rs485", TASK_STACK, NULL, TASK_PRIO, NULL);
}
