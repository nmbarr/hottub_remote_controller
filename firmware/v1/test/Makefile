# Host-compiles the RS485 framer against stub ESP-IDF headers so the framing
# and CRC arithmetic can be exercised without a board or a spa.
#
# Run outside the ESP-IDF environment: exporting IDF puts the xtensa
# cross-assembler ahead of the host one on PATH, and the build fails with
# "as: unrecognized option '--64'". -B/usr/bin keeps the host tools in reach
# if you have exported anyway.

CC ?= cc
CFLAGS := -I stubs -I ../main -B/usr/bin -Wall -Wextra -std=c11 -g

run: test_rs485_framing
	./test_rs485_framing

test_rs485_framing: test_rs485_framing.c ../main/rs485.c ../main/rs485.h
	$(CC) $(CFLAGS) -o $@ test_rs485_framing.c

clean:
	rm -f test_rs485_framing

.PHONY: run clean
