// Enough of driver/uart.h to host-compile rs485.c. The UART calls are only
// reached by rs485_start, which the framing tests never call.
#pragma once

#include <stdint.h>

#define UART_NUM_2 2
#define UART_DATA_8_BITS 0
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 0
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE (-1)
#define UART_MODE_RS485_HALF_DUPLEX 0

typedef struct
{
  int baud_rate;
  int data_bits;
  int parity;
  int stop_bits;
  int flow_ctrl;
  int source_clk;
} uart_config_t;

static inline int uart_param_config(int p, const uart_config_t *c)
{
  (void)p; (void)c; return 0;
}
static inline int uart_set_pin(int p, int tx, int rx, int rts, int cts)
{
  (void)p; (void)tx; (void)rx; (void)rts; (void)cts; return 0;
}
static inline int uart_driver_install(int p, int rx, int tx, int q, void *h, int f)
{
  (void)p; (void)rx; (void)tx; (void)q; (void)h; (void)f; return 0;
}
static inline int uart_set_mode(int p, int m) { (void)p; (void)m; return 0; }
static inline int uart_read_bytes(int p, uint8_t *b, uint32_t n, uint32_t t)
{
  (void)p; (void)b; (void)n; (void)t; return 0;
}
