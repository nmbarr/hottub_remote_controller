// Host-side tests for the RS485 framer. Included as source rather than linked
// so the tests can reach the framing internals and the CRC directly.
//
// These cover the framing arithmetic -- where the length byte points, what the
// CRC spans, how a bad frame resyncs -- which is exactly the sort of thing
// that reads correctly and is still off by one. They say nothing about
// whether the spa actually speaks this protocol.

#include <stdio.h>
#include <string.h>

#include "rs485.c"

static int g_fails;
static int g_calls;
static uint8_t g_last[FRAME_LEN_MAX];
static size_t g_last_len;

#define CHECK(cond)                                                  \
  do                                                                 \
  {                                                                  \
    if (!(cond))                                                     \
    {                                                                \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
      g_fails++;                                                     \
    }                                                                \
  } while (0)

static void capture(const uint8_t *body, size_t len)
{
  g_calls++;
  memcpy(g_last, body, len);
  g_last_len = len;
}

// 7e LL src type[2] payload CRC 7e, with the CRC over LL through the last
// payload byte. Deliberately built from the spec rather than from the
// framer's own constants.
static int build(uint8_t *out, uint8_t src, uint8_t t1, uint8_t t2,
                 const uint8_t *payload, int payload_len)
{
  uint8_t ll = (uint8_t)(payload_len + 5);
  int i = 0;

  out[i++] = 0x7e;
  out[i++] = ll;
  out[i++] = src;
  out[i++] = t1;
  out[i++] = t2;
  if (payload_len > 0)
  {
    memcpy(out + i, payload, payload_len);
    i += payload_len;
  }
  out[i++] = balboa_crc8(out + 1, ll - 1);
  out[i++] = 0x7e;

  return i;
}

static void feed(const uint8_t *bytes, int n)
{
  for (int i = 0; i < n; i++)
  {
    frame_feed(bytes[i]);
  }
}

static void reset_all(void)
{
  s_ok = s_crc_bad = s_resync = 0;
  g_calls = 0;
  g_last_len = 0;
  frame_restart(FRAME_WAIT_START);
}

int main(void)
{
  uint8_t buf[256];
  uint8_t payload[24];
  for (int i = 0; i < 24; i++)
  {
    payload[i] = (uint8_t)i;
  }

  rs485_set_message_handler(capture);

  // A Ready poll carries no payload, so it exercises the minimum length.
  reset_all();
  int n = build(buf, 0x10, 0xbf, 0x06, NULL, 0);
  CHECK(n == 7);
  CHECK(buf[1] == FRAME_LEN_MIN);
  feed(buf, n);
  CHECK(s_ok == 1);
  CHECK(g_calls == 1);
  CHECK(g_last_len == 3);
  CHECK(g_last[0] == 0x10 && g_last[1] == 0xbf && g_last[2] == 0x06);

  // A status broadcast: 24 payload bytes means LL 0x1d and 31 bytes on the
  // wire. Getting this wrong by one is the whole risk in the framer.
  reset_all();
  n = build(buf, 0xff, 0xaf, 0x13, payload, 24);
  CHECK(n == 31);
  CHECK(buf[1] == 0x1d);
  feed(buf, n);
  CHECK(s_ok == 1);
  CHECK(g_last_len == 27);

  // Back-to-back frames may share a single delimiter.
  reset_all();
  int first = build(buf, 0x10, 0xbf, 0x06, NULL, 0);
  int second = build(buf + first - 1, 0xff, 0xaf, 0x13, payload, 24);
  feed(buf, first - 1 + second);
  CHECK(s_ok == 2);

  // A flipped CRC must be counted and withheld from the handler.
  reset_all();
  n = build(buf, 0x10, 0xbf, 0x06, NULL, 0);
  buf[n - 2] ^= 0xff;
  feed(buf, n);
  CHECK(s_ok == 0);
  CHECK(s_crc_bad == 1);
  CHECK(g_calls == 0);

  // Payload bytes are not escaped, so a 0x7e inside one is legal and must not
  // be mistaken for a delimiter.
  reset_all();
  uint8_t tricky[6] = {0x01, 0x7e, 0x7e, 0x04, 0x05, 0x06};
  n = build(buf, 0xff, 0xaf, 0x13, tricky, 6);
  feed(buf, n);
  CHECK(s_ok == 1);
  CHECK(g_last_len == 9);

  // Joining a bus mid-message, including a length byte that cannot be real.
  reset_all();
  uint8_t junk[5] = {0x00, 0xff, 0x12, 0x7e, 0x02};
  feed(junk, 5);
  n = build(buf, 0x10, 0xbf, 0x06, NULL, 0);
  feed(buf, n);
  CHECK(s_ok == 1);

  // A truncated frame swallows what follows until the length runs out. It has
  // to recover on its own, and quickly enough not to matter at one status
  // broadcast a second.
  reset_all();
  n = build(buf, 0xff, 0xaf, 0x13, payload, 24);
  feed(buf, 12);
  int recovered_after = -1;
  for (int k = 1; k <= 5; k++)
  {
    int m = build(buf, 0xff, 0xaf, 0x13, payload, 24);
    feed(buf, m);
    if (s_ok > 0)
    {
      recovered_after = k;
      break;
    }
  }
  CHECK(recovered_after > 0 && recovered_after <= 2);

  // An oversized length byte is rejected outright rather than overrunning
  // s_frame.
  reset_all();
  uint8_t huge[3] = {0x7e, FRAME_LEN_MAX + 1, 0x10};
  feed(huge, 3);
  CHECK(s_resync == 1);
  CHECK(s_have == 0);

  printf(g_fails ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", g_fails);
  return g_fails != 0;
}
