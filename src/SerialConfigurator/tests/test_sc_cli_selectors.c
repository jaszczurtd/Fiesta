/*
 * CLI argument parsing: selectors, positional arguments, reboot paths,
 * --id/--value pairs and selector matching against detected modules.
 */

#include "../src/cli/sc_cli_selectors.h"

#include <hal/core/hal_array.h>
#include <stdio.h>
#include <string.h>

static int s_failures;

#define TEST_ASSERT(condition, message)                                        \
  do {                                                                         \
    if (!(condition)) {                                                        \
      (void)fprintf(stderr, "FAIL: %s (line %d)\n", (message), __LINE__);      \
      ++s_failures;                                                            \
    }                                                                          \
  } while (0)

#define ARGC(argv) ((int)COUNTOF(argv))

static bool str_is(const char *actual, const char *expected) {
  return actual != NULL && strcmp(actual, expected) == 0;
}

static void test_selectors(void) {
  char *argv[] = {"cli",   "detect", "--module", "ECU",
                  "--uid", "U1",     "--port",   "/dev/x"};
  CliSelectors sel = {"stale", "stale", "stale"};
  TEST_ASSERT(sc_cli_parse_selectors(ARGC(argv), argv, 2, &sel),
              "all selectors parse");
  TEST_ASSERT(str_is(sel.module, "ECU") && str_is(sel.uid, "U1") &&
                  str_is(sel.port, "/dev/x"),
              "selector values");

  char *none[] = {"cli", "detect"};
  TEST_ASSERT(sc_cli_parse_selectors(ARGC(none), none, 2, &sel),
              "no selectors parse");
  TEST_ASSERT(sel.module == NULL && sel.uid == NULL && sel.port == NULL,
              "selectors reset before parsing");

  char *missing[] = {"cli", "detect", "--port"};
  TEST_ASSERT(!sc_cli_parse_selectors(ARGC(missing), missing, 2, &sel),
              "missing selector value");
  char *unknown[] = {"cli", "detect", "--speed", "1"};
  TEST_ASSERT(!sc_cli_parse_selectors(ARGC(unknown), unknown, 2, &sel),
              "unknown option");
  char *positional[] = {"cli", "detect", "extra"};
  TEST_ASSERT(!sc_cli_parse_selectors(ARGC(positional), positional, 2, &sel),
              "positional argument is not a selector");
  TEST_ASSERT(!sc_cli_parse_selectors(ARGC(argv), argv, 2, NULL),
              "NULL output");
}

static void test_positional(void) {
  CliSelectors sel;
  const char *id = "stale";
  char *argv[] = {"cli",   "get-param", "--uid", "U2",
                  "speed", "--module",  "ECU"};
  TEST_ASSERT(sc_cli_parse_get_param_args(ARGC(argv), argv, &id, &sel),
              "positional with selectors");
  TEST_ASSERT(str_is(id, "speed") && str_is(sel.uid, "U2") &&
                  str_is(sel.module, "ECU") && sel.port == NULL,
              "positional values");

  char *extra[] = {"cli", "get-param", "speed", "rpm"};
  TEST_ASSERT(!sc_cli_parse_get_param_args(ARGC(extra), extra, &id, &sel),
              "second positional argument");
  char *absent[] = {"cli", "get-param", "--module", "ECU"};
  TEST_ASSERT(
      !sc_cli_parse_positional_args(ARGC(absent), absent, "<name>", &id, &sel),
      "missing positional argument");
  TEST_ASSERT(id == NULL, "positional reset before parsing");
  char *empty[] = {"cli", "get-param", ""};
  TEST_ASSERT(!sc_cli_parse_get_param_args(ARGC(empty), empty, &id, &sel),
              "empty positional argument");
  char *missing[] = {"cli", "get-param", "speed", "--module"};
  TEST_ASSERT(!sc_cli_parse_get_param_args(ARGC(missing), missing, &id, &sel),
              "missing selector value after positional");
  TEST_ASSERT(!sc_cli_parse_positional_args(ARGC(argv), argv, NULL, &id, &sel),
              "NULL label");
}

static void test_reboot(void) {
  CliSelectors sel;
  const char *manifest = "stale";
  const char *artifact = "stale";
  char *argv[] = {"cli",    "reboot",     "--artifact", "fw.uf2",   "--port",
                  "/dev/y", "--manifest", "fw.json",    "--module", "Clocks"};
  TEST_ASSERT(
      sc_cli_parse_reboot_args(ARGC(argv), argv, &sel, &manifest, &artifact),
      "reboot arguments");
  TEST_ASSERT(str_is(manifest, "fw.json") && str_is(artifact, "fw.uf2") &&
                  str_is(sel.port, "/dev/y") && str_is(sel.module, "Clocks") &&
                  sel.uid == NULL,
              "reboot values");

  char *bare[] = {"cli", "reboot"};
  TEST_ASSERT(
      sc_cli_parse_reboot_args(ARGC(bare), bare, &sel, &manifest, &artifact),
      "reboot without options");
  TEST_ASSERT(manifest == NULL && artifact == NULL, "reboot paths reset");

  char *missing[] = {"cli", "reboot", "--manifest"};
  TEST_ASSERT(!sc_cli_parse_reboot_args(ARGC(missing), missing, &sel, &manifest,
                                        &artifact),
              "missing manifest path");
  char *positional[] = {"cli", "reboot", "fw.uf2"};
  TEST_ASSERT(!sc_cli_parse_reboot_args(ARGC(positional), positional, &sel,
                                        &manifest, &artifact),
              "reboot rejects positional arguments");
}

static void test_id_value(void) {
  CliSelectors sel;
  const char *id = NULL;
  long value = 7;
  char *argv[] = {"cli", "set",  "--value", "-5",    "--module",
                  "ECU", "--id", "boost",   "--uid", "U3"};
  TEST_ASSERT(sc_cli_parse_id_value_args(ARGC(argv), argv, -10, 10, "small",
                                         &id, &value, &sel),
              "id and value");
  TEST_ASSERT(str_is(id, "boost") && value == -5 && str_is(sel.module, "ECU") &&
                  str_is(sel.uid, "U3") && sel.port == NULL,
              "id and value results");

  char *high[] = {"cli", "set", "--id", "boost", "--value", "11"};
  TEST_ASSERT(!sc_cli_parse_id_value_args(ARGC(high), high, -10, 10, "small",
                                          &id, &value, &sel),
              "value above range");
  char *text[] = {"cli", "set", "--id", "boost", "--value", "5x"};
  TEST_ASSERT(!sc_cli_parse_id_value_args(ARGC(text), text, -10, 10, "small",
                                          &id, &value, &sel),
              "trailing characters");
  char *no_id[] = {"cli", "set", "--value", "5"};
  TEST_ASSERT(!sc_cli_parse_id_value_args(ARGC(no_id), no_id, -10, 10, "small",
                                          &id, &value, &sel),
              "missing --id");
  char *no_value[] = {"cli", "set", "--id", "boost"};
  TEST_ASSERT(!sc_cli_parse_id_value_args(ARGC(no_value), no_value, -10, 10,
                                          "small", &id, &value, &sel),
              "missing --value");
  char *dangling[] = {"cli", "set", "--id", "boost", "--value"};
  TEST_ASSERT(!sc_cli_parse_id_value_args(ARGC(dangling), dangling, -10, 10,
                                          "small", &id, &value, &sel),
              "--value without a number");
  char *other[] = {"cli", "set", "--id", "boost", "--value", "1", "--x"};
  TEST_ASSERT(!sc_cli_parse_id_value_args(ARGC(other), other, -10, 10, "small",
                                          &id, &value, &sel),
              "unknown option");

  int v16 = 0;
  char *top[] = {"cli", "set", "--id", "a", "--value", "32767"};
  TEST_ASSERT(sc_cli_parse_set_param_args(ARGC(top), top, &id, &v16, &sel) &&
                  v16 == 32767,
              "int16 upper bound");
  char *over[] = {"cli", "set", "--id", "a", "--value", "32768"};
  TEST_ASSERT(!sc_cli_parse_set_param_args(ARGC(over), over, &id, &v16, &sel),
              "above int16");
  char *bottom[] = {"cli", "set", "--id", "a", "--value", "-32768"};
  TEST_ASSERT(
      sc_cli_parse_set_param_args(ARGC(bottom), bottom, &id, &v16, &sel) &&
          v16 == -32768,
      "int16 lower bound");
}

static void test_module_matching(void) {
  ScModuleStatus status;
  memset(&status, 0, sizeof(status));
  status.display_name = "RTC_Clock";
  status.detected = true;
  (void)strcpy(status.hello_identity.module_name, "RTC_CLK");
  (void)strcpy(status.hello_identity.uid, "E6614C");
  (void)strcpy(status.port_path, "/dev/ttyACM0");

  CliSelectors sel = {"rtc_clock", NULL, NULL};
  TEST_ASSERT(sc_cli_module_matches_selectors(&status, &sel),
              "display name ignores case");
  sel.module = "rtc_clk";
  TEST_ASSERT(sc_cli_module_matches_selectors(&status, &sel),
              "HELLO module name ignores case");
  sel.module = "rtc";
  TEST_ASSERT(!sc_cli_module_matches_selectors(&status, &sel),
              "prefix is not a match");
  sel.module = NULL;
  sel.uid = "e6614c";
  TEST_ASSERT(sc_cli_module_matches_selectors(&status, &sel),
              "uid ignores case");
  sel.port = "/dev/ttyacm0";
  TEST_ASSERT(!sc_cli_module_matches_selectors(&status, &sel),
              "port is case sensitive");
  sel.port = "/dev/ttyACM0";
  TEST_ASSERT(sc_cli_module_matches_selectors(&status, &sel),
              "uid and port together");
  status.detected = false;
  TEST_ASSERT(!sc_cli_module_matches_selectors(&status, &sel),
              "undetected module never matches");
}

int main(void) {
  test_selectors();
  test_positional();
  test_reboot();
  test_id_value();
  test_module_matching();
  if (s_failures != 0) {
    (void)fprintf(stderr, "sc_cli_selectors tests: %d failure(s)\n",
                  s_failures);
    return 1;
  }
  (void)printf("[OK] sc_cli_selectors tests passed\n");
  return 0;
}
