#include "console_capture.h"
#include "console_stream.h"
#include "hal/system/hal_system.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(expr)                                                          \
  do {                                                                         \
    if (!(expr)) {                                                             \
      fprintf(stderr, "console: %s at %d\n", #expr, __LINE__);                 \
      return false;                                                            \
    }                                                                          \
  } while (0)

typedef struct {
  char text[128];
  size_t used;
  unsigned lines;
  hal_status_t result;
} Received;

static hal_status_t receive(void *context, const char *data, size_t size) {
  Received *received = context;
  if (size + 1 >= sizeof received->text - received->used)
    return HAL_EOVERFLOW;
  memcpy(received->text + received->used, data, size);
  received->used += size;
  received->text[received->used++] = '|';
  received->text[received->used] = 0;
  ++received->lines;
  return received->result;
}

bool vp37v_test_console_capture(void) {
  static const unsigned char input[] =
      "\033[32mECU: VP37 C adj:1\033[0m\r\n"
      "\033]0;title\007UTF8 \xc5\xbc\tOK\rnext\n"
      "\033]title\033\\last\npartial";
  /* Every split, including inside ANSI, CRLF and UTF-8 sequences. */
  for (size_t split = 0; split < sizeof input; ++split) {
    char storage[64];
    Received received = {0};
    received.result = HAL_OK;
    vp37_console_stream_t stream;
    REQUIRE(vp37_console_stream_init(&stream, storage, sizeof storage, receive,
                                     &received) == HAL_OK);
    REQUIRE(vp37_console_stream_feed(&stream, input, split) == HAL_OK);
    REQUIRE(vp37_console_stream_feed(&stream, input + split,
                                     sizeof input - 1 - split) == HAL_OK);
    REQUIRE(received.lines == 4);
    REQUIRE(strcmp(received.text,
                   "ECU: VP37 C adj:1|UTF8 \xc5\xbc OK|next|last|") == 0);
    REQUIRE(stream.lines.used == 7); /* An unfinished record stays private. */
  }
  char storage[4];
  Received received = {0};
  received.result = HAL_OK;
  vp37_console_stream_t stream;
  REQUIRE(vp37_console_stream_init(NULL, storage, sizeof storage, receive,
                                   &received) == HAL_EINVAL);
  REQUIRE(vp37_console_stream_init(&stream, storage, sizeof storage, receive,
                                   &received) == HAL_OK);
  REQUIRE(vp37_console_stream_feed(&stream, NULL, 1) == HAL_EINVAL);
  REQUIRE(vp37_console_stream_feed(&stream, NULL, 0) == HAL_OK);
  REQUIRE(vp37_console_stream_feed(&stream, (const unsigned char *)"1234\n",
                                   5) == HAL_OK);
  REQUIRE(received.lines == 1);
  REQUIRE(vp37_console_stream_feed(&stream, (const unsigned char *)"12345\n",
                                   6) == HAL_EOVERFLOW);
  REQUIRE(vp37_console_stream_feed(&stream, (const unsigned char *)"\n", 1) ==
          HAL_EOVERFLOW);
  REQUIRE(received.lines == 1);
  REQUIRE(vp37_console_stream_init(&stream, storage, sizeof storage, receive,
                                   &received) == HAL_OK);
  REQUIRE(vp37_console_stream_feed(&stream, (const unsigned char *)"x\0\n",
                                   3) == HAL_EPROTO);
  REQUIRE(vp37_console_stream_feed(&stream, NULL, 0) == HAL_EPROTO);
  REQUIRE(vp37_console_stream_init(&stream, storage, sizeof storage, receive,
                                   &received) == HAL_OK);
  received.result = HAL_EIO;
  REQUIRE(vp37_console_stream_feed(&stream, (const unsigned char *)"x\ny\n",
                                   4) == HAL_EIO);
  REQUIRE(received.lines == 2);
  return true;
}

bool vp37v_test_sample_clock(void) {
  /* A reset in either half of the counter range and a genuine forward wrap. */
  const uint32_t previous[] = {600000000u, 2400000000u, 4294000000u};
  for (size_t i = 0; i < COUNTOF(previous); ++i) {
    vp37_sample_clock_t clock = {0};
    double t = 0;
    REQUIRE(vp37_sample_clock_time(&clock, previous[i], 100, true, &t) ==
            HAL_OK);
    REQUIRE(vp37_sample_clock_time(&clock, 3000000u, 101, true, &t) == HAL_OK);
    /* The third case wraps naturally within the 5 s host tolerance. */
    REQUIRE(fabs(t - (i == 2 ? 103.967296 : 101)) < 0.000001);
    REQUIRE(clock.restarted == (i != 2));
    REQUIRE(vp37_sample_clock_time(&clock, 2999000u, 105, false, &t) == HAL_OK);
    REQUIRE(!clock.restarted);
    REQUIRE(fabs(t - (i == 2 ? 103.966296 : 100.999)) < 0.000001);
  }
  vp37_sample_clock_t clock = {0};
  double t = 0;
  REQUIRE(vp37_sample_clock_time(&clock, 1000000, 100, true, &t) == HAL_OK);
  REQUIRE(vp37_sample_clock_time(&clock, 2000000, 106, true, &t) == HAL_OK);
  REQUIRE(!clock.restarted && t == 101); /* Exactly the tolerance. */
  REQUIRE(vp37_sample_clock_time(&clock, 3000000, 107.01, true, &t) == HAL_OK);
  REQUIRE(clock.restarted && t == 107.01);
  return true;
}

static bool unbuffered(void) {
#ifdef _WIN32
  char value[2];
  size_t size = 0;
  return getenv_s(&size, value, sizeof value, "PYTHONUNBUFFERED") == 0 &&
         size == 2 && value[0] == '1';
#else
  const char *value = getenv("PYTHONUNBUFFERED");
  return value != NULL && strcmp(value, "1") == 0;
#endif
}

static const char separator[] = "----------------------------------------"
                                "----------------------------------------\n";

bool vp37v_test_monitor_help(const char *output_path, const char *log_path,
                             const char *port) {
  const char *paths[] = {output_path, log_path};
  static char contents[2][8192];
  for (size_t i = 0; i < COUNTOF(paths); ++i) {
    FILE *file = fopen(paths[i], "r");
    REQUIRE(file != NULL);
    const size_t size = fread(contents[i], 1, sizeof contents[i] - 1, file);
    const bool complete = feof(file) && !ferror(file);
    const int closed = fclose(file);
    REQUIRE(complete && closed == 0);
    contents[i][size] = 0;
  }
  char url[96];
  const int size = snprintf(url, sizeof url, "http://127.0.0.1:%s/live", port);
  REQUIRE(size > 0 && (size_t)size < sizeof url);
  REQUIRE(strstr(contents[0], url) != NULL);
  REQUIRE(strstr(contents[0], "Chrome:") != NULL);
  REQUIRE(strstr(contents[0], "Ctrl+Shift+P -> Open Integrated Browser") !=
          NULL);
  REQUIRE(strstr(contents[0], "Tasks: Run Task -> VP37: Live viewer") != NULL);
  REQUIRE(strstr(contents[0], "ECU BENCH") != NULL);
  unsigned count = 0;
  const char *cursor = contents[0];
  while ((cursor = strstr(cursor, "VP37 live viewer is running.")) != NULL) {
    REQUIRE(cursor >= contents[0] + sizeof separator);
    REQUIRE(memcmp(cursor - sizeof separator, separator,
                   sizeof separator - 1) == 0);
    REQUIRE(cursor[-1] == '\n');
    ++count;
    ++cursor;
  }
  /* One initial connection and one reconnect, not the unrelated separator. */
  REQUIRE(count == 2);
  REQUIRE(strstr(contents[1], "VP37 live viewer") == NULL);
  REQUIRE(strstr(contents[1], url) == NULL);
  return true;
}

int vp37v_monitor_fixture(int argc, char **argv) {
  if (argc != 6 || strcmp(argv[2], "--project") != 0 ||
      strcmp(argv[4], "--lock-policy") != 0 ||
      strcmp(argv[5], "replace-own") != 0 || !unbuffered())
    return 9;
  if (strstr(argv[3], "fail") != NULL)
    return 7;
  if (strstr(argv[3], "overflow") != NULL) {
    /* Finite even if the line budget is broken. */
    for (unsigned i = 0; i < 66000; ++i)
      if (putchar('x') == EOF)
        return 8;
    return 0;
  }
  if (strstr(argv[3], "quiet") != NULL) {
    hal_delay_ms(30000);
    return 0;
  }
  fputs(separator, stdout); /* An unrelated separator is not a connection. */
  fputs("\033[32mConnected to fixture @ 115200\033[0m\r\n\033[2m", stdout);
  fputs(separator, stdout);
  fputs("\033[0mECU: VP", stdout);
  fflush(stdout);
  hal_delay_ms(30);
  fputs("37 C us:1000 tar:1200 des:1100 adj:1050 pwm:20 rest:0\r", stdout);
  fflush(stdout);
  hal_delay_ms(30);
  fputs("\nVP37 C us:2000 tar:1200 des:1120 adj:1080 pwm:30 rest:0\n"
        "Connected to fixture @ 115200\n",
        stdout);
  fputs(separator, stdout);
  fputs("partial", stdout);
  fflush(stdout);
  hal_delay_ms(30);
  return 0;
}
