#pragma once

#include <stddef.h>
#include <stdint.h>

// Called for each frame that arrives intact and passes CRC. body[0] is the
// source, body[1..2] the message type, the rest payload -- delimiters, length
// and CRC are stripped. Valid only for the duration of the call. Runs on the
// RS485 task: must not block, and in particular must not wait on the network.
typedef void (*rs485_msg_handler_t)(const uint8_t *body, size_t len);

// Registers the frame callback. Call before rs485_start().
void rs485_set_message_handler(rs485_msg_handler_t cb);

// Configures UART2 for half-duplex RS485 and starts the receive task.
void rs485_start(void);
