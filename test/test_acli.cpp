/**
 * Unit tests for AdvancedCLI.
 *
 * Each test function creates a fresh CLI instance (or reuses a module-level one where
 * state is not shared across tests) and calls Unity assertions.
 *
 * Coverage:
 * - Basic command dispatch
 * - Named args (string, int, float)
 * - Flag args
 * - Positional args
 * - Default values (string / int / float)
 * - Required args - missing vs provided
 * - setValidator (typed validator for ArgStr, ArgInt, ArgFloat)
 * - Aliases
 * - Sub-commands
 * - Case-insensitive matching (default) and case-sensitive mode
 * - parse() return value
 * - inject() return value and output capture
 * - onError() callback
 * - cmd.fail()
 * - getArgByName()
 * - lastParseOk()
 * - commandCount()
 * - Unknown command + onUnknownCommand callback
 * - onInvalid() per-argument callback
 * - Multiple args in one command
 * - "--" positional separator
 * - printHelp(cmd_name)
 * - Duplicate arg name detection
 */

#include <Arduino.h>
#include <unity.h>

#include <AdvancedCLI.h>

using namespace ACLI;

/* ---------------------------------------------------------------------------------------------- */
/*                                             Helpers                                            */
/* ---------------------------------------------------------------------------------------------- */

// Accumulate all output emitted by a CLI instance during one operation.
struct OutputCapture {
  char buf[512] = {};
  int len       = 0;

  void clear() {
    buf[0] = '\0';
    len    = 0;
  }

  OutputFn fn() {
    return [this](const char* s) {
      int sl = (int)strlen(s);
      if (len + sl < (int)sizeof(buf) - 1) {
        memcpy(buf + len, s, sl);
        len += sl;
        buf[len] = '\0';
      }
    };
  }
};

/* ---------------------------------------------------------------------------------------------- */
/*                                         Basic dispatch                                         */
/* ---------------------------------------------------------------------------------------------- */

static void test_basic_dispatch() {
  AdvancedCLI cli;
  bool called = false;

  cli.addCommand("ping").onExecute([&](Command&) { called = true; });

  TEST_ASSERT_TRUE(cli.inject("ping"));
  TEST_ASSERT_TRUE(called);
}

static void test_unknown_command_returns_false() {
  AdvancedCLI cli;
  cli.addCommand("ping").onExecute([](Command&) {});
  TEST_ASSERT_FALSE(cli.inject("pong"));
}

static void test_commandCount() {
  AdvancedCLI cli;
  TEST_ASSERT_EQUAL(0, cli.commandCount());
  cli.addCommand("a");
  cli.addCommand("b");
  TEST_ASSERT_EQUAL(2, cli.commandCount());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                      Named string argument                                     */
/* ---------------------------------------------------------------------------------------------- */

static void test_named_string_arg_provided() {
  AdvancedCLI cli;
  ArgStr h_name;
  const char* received = nullptr;

  auto& cmd = cli.addCommand("greet");
  h_name    = cmd.addArg("name", "World");
  cmd.onExecute([&](Command& c) {
    auto a   = c.getArg(h_name);
    received = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("greet --name Alice"));
  TEST_ASSERT_EQUAL_STRING("Alice", received);
}

static void test_named_string_arg_default() {
  AdvancedCLI cli;
  ArgStr h_name;
  const char* received = nullptr;

  auto& cmd = cli.addCommand("greet");
  h_name    = cmd.addArg("name", "World");
  cmd.onExecute([&](Command& c) {
    auto a   = c.getArg(h_name);
    received = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("greet"));
  TEST_ASSERT_EQUAL_STRING("World", received);
}

static void test_named_string_arg_no_default_returns_empty() {
  AdvancedCLI cli;
  ArgStr h;
  const char* received = nullptr;

  auto& cmd = cli.addCommand("cmd");
  h         = cmd.addArg("val");
  cmd.onExecute([&](Command& c) {
    auto a   = c.getArg(h);
    received = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("cmd"));
  TEST_ASSERT_EQUAL_STRING("", received);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                          Flag argument                                         */
/* ---------------------------------------------------------------------------------------------- */

static void test_flag_present() {
  AdvancedCLI cli;
  ArgFlag h_verbose;
  bool got = false;

  auto& cmd = cli.addCommand("run");
  h_verbose = cmd.addFlag("verbose");
  cmd.onExecute([&](Command& c) {
    auto f = c.getArg(h_verbose);
    got    = f.isSet();
  });

  TEST_ASSERT_TRUE(cli.inject("run --verbose"));
  TEST_ASSERT_TRUE(got);
}

static void test_flag_absent() {
  AdvancedCLI cli;
  ArgFlag h_verbose;
  bool got = true; // start true to prove it gets set false

  auto& cmd = cli.addCommand("run");
  h_verbose = cmd.addFlag("verbose");
  cmd.onExecute([&](Command& c) {
    auto f = c.getArg(h_verbose);
    got    = f.isSet();
  });

  TEST_ASSERT_TRUE(cli.inject("run"));
  TEST_ASSERT_FALSE(got);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                        Integer argument                                        */
/* ---------------------------------------------------------------------------------------------- */

static void test_int_arg_provided() {
  AdvancedCLI cli;
  ArgInt h;
  int32_t got = 0;

  auto& cmd = cli.addCommand("set");
  h         = cmd.addIntArg("count");
  cmd.onExecute([&](Command& c) {
    auto a = c.getArg(h);
    got    = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("set --count 42"));
  TEST_ASSERT_EQUAL(42, got);
}

static void test_int_arg_default() {
  AdvancedCLI cli;
  ArgInt h;
  int32_t got = 0;

  auto& cmd = cli.addCommand("set");
  h         = cmd.addIntArg("count", 7);
  cmd.onExecute([&](Command& c) {
    auto a = c.getArg(h);
    got    = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("set"));
  TEST_ASSERT_EQUAL(7, got);
}

static void test_int_arg_negative() {
  AdvancedCLI cli;
  ArgInt h;
  int32_t got = 0;

  auto& cmd = cli.addCommand("offset");
  h         = cmd.addIntArg("val");
  cmd.onExecute([&](Command& c) {
    auto a = c.getArg(h);
    got    = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("offset --val -5"));
  TEST_ASSERT_EQUAL(-5, got);
}

static void test_float_arg_negative() {
  AdvancedCLI cli;
  ArgFloat h;
  float got = 0.f;

  auto& cmd = cli.addCommand("temp");
  h         = cmd.addFloatArg("val");
  cmd.onExecute([&](Command& c) { got = c.getArg(h).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("temp --val -3.14"));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -3.14f, got);
}

static void test_float_arg_negative_leading_dot() {
  AdvancedCLI cli;
  ArgFloat h;
  float got = 0.f;

  auto& cmd = cli.addCommand("gain");
  h         = cmd.addFloatArg("val");
  cmd.onExecute([&](Command& c) { got = c.getArg(h).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("gain --val -.5"));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.5f, got);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                         Float argument                                         */
/* ---------------------------------------------------------------------------------------------- */

static void test_float_arg_provided() {
  AdvancedCLI cli;
  ArgFloat h;
  float got = 0.f;

  auto& cmd = cli.addCommand("temp");
  h         = cmd.addFloatArg("val");
  cmd.onExecute([&](Command& c) {
    auto a = c.getArg(h);
    got    = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("temp --val 3.14"));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, got);
}

static void test_float_arg_default() {
  AdvancedCLI cli;
  ArgFloat h;
  float got = 0.f;

  auto& cmd = cli.addCommand("temp");
  h         = cmd.addFloatArg("val", 1.5f);
  cmd.onExecute([&](Command& c) {
    auto a = c.getArg(h);
    got    = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("temp"));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, got);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                        Required argument                                       */
/* ---------------------------------------------------------------------------------------------- */

static void test_required_arg_missing_fails() {
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("req");
  cmd.addArg("must").setRequired();
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_FALSE(cli.inject("req"));
  TEST_ASSERT_FALSE(called);
}

static void test_required_arg_provided_succeeds() {
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("req");
  cmd.addArg("must").setRequired();
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_TRUE(cli.inject("req --must hello"));
  TEST_ASSERT_TRUE(called);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                       Positional argument                                      */
/* ---------------------------------------------------------------------------------------------- */

static void test_positional_arg() {
  AdvancedCLI cli;
  ArgStr h;
  const char* got = nullptr;

  auto& cmd = cli.addCommand("echo");
  h         = cmd.addPosArg("text");
  cmd.onExecute([&](Command& c) {
    auto a = c.getArg(h);
    got    = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("echo hello"));
  TEST_ASSERT_EQUAL_STRING("hello", got);
}

static void test_positional_arg_default() {
  AdvancedCLI cli;
  ArgStr h;
  const char* got = nullptr;

  auto& cmd = cli.addCommand("echo");
  h         = cmd.addPosArg("text", "default");
  cmd.onExecute([&](Command& c) {
    auto a = c.getArg(h);
    got    = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("echo"));
  TEST_ASSERT_EQUAL_STRING("default", got);
}

static void test_multiple_positional_args() {
  AdvancedCLI cli;
  ArgStr h_a, h_b;
  const char* got_a = nullptr;
  const char* got_b = nullptr;

  auto& cmd = cli.addCommand("copy");
  h_a       = cmd.addPosArg("src");
  h_b       = cmd.addPosArg("dst");
  cmd.onExecute([&](Command& c) {
    got_a = c.getArg(h_a).getValue();
    got_b = c.getArg(h_b).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("copy /src /dst"));
  TEST_ASSERT_EQUAL_STRING("/src", got_a);
  TEST_ASSERT_EQUAL_STRING("/dst", got_b);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                 setValidator - typed validator                                 */
/* ---------------------------------------------------------------------------------------------- */

static void test_int_validation_fn_valid() {
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("pin");
  cmd.addIntArg("num").setValidator([](int32_t v) { return v >= 0 && v <= 39; });
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_TRUE(cli.inject("pin --num 20"));
  TEST_ASSERT_TRUE(called);
}

static void test_int_validation_fn_below_min_fails() {
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("pin");
  cmd.addIntArg("num").setValidator([](int32_t v) { return v >= 0 && v <= 39; });
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_FALSE(cli.inject("pin --num -1"));
  TEST_ASSERT_FALSE(called);
}

static void test_int_validation_fn_above_max_fails() {
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("pin");
  cmd.addIntArg("num").setValidator([](int32_t v) { return v >= 0 && v <= 39; });
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_FALSE(cli.inject("pin --num 40"));
  TEST_ASSERT_FALSE(called);
}

static void test_float_validation_fn_valid() {
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("duty");
  cmd.addFloatArg("pct").setValidator([](float v) { return v >= 0.f && v <= 1.f; });
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_TRUE(cli.inject("duty --pct 0.5"));
  TEST_ASSERT_TRUE(called);
}

static void test_float_validation_fn_exceeds_fails() {
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("duty");
  cmd.addFloatArg("pct").setValidator([](float v) { return v >= 0.f && v <= 1.f; });
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_FALSE(cli.inject("duty --pct 1.1"));
  TEST_ASSERT_FALSE(called);
}

static const char* const LOG_LEVELS[] = {"debug", "info", "warn", "error", nullptr};

static void test_str_validation_fn_valid() {
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("log");
  cmd.addArg("level").setValidator([](const char* v) {
    for (uint8_t i = 0; LOG_LEVELS[i]; ++i)
      if (strcmp(LOG_LEVELS[i], v) == 0) return true;
    return false;
  });
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_TRUE(cli.inject("log --level info"));
  TEST_ASSERT_TRUE(called);
}

static void test_str_validation_fn_invalid_fails() {
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("log");
  cmd.addArg("level").setValidator([](const char* v) {
    for (uint8_t i = 0; LOG_LEVELS[i]; ++i)
      if (strcmp(LOG_LEVELS[i], v) == 0) return true;
    return false;
  });
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_FALSE(cli.inject("log --level verbose"));
  TEST_ASSERT_FALSE(called);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                             Aliases                                            */
/* ---------------------------------------------------------------------------------------------- */

static void test_alias_matches_arg() {
  AdvancedCLI cli;
  ArgStr h;
  const char* got = nullptr;

  auto& cmd = cli.addCommand("net");
  h         = cmd.addArg("address");
  h.setAlias("a");
  cmd.onExecute([&](Command& c) { got = c.getArg(h).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("net --a 192.168.1.1"));
  TEST_ASSERT_EQUAL_STRING("192.168.1.1", got);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                          Sub-commands                                          */
/* ---------------------------------------------------------------------------------------------- */

static void test_subcommand_dispatch() {
  AdvancedCLI cli;
  bool scan_called = false;

  auto& wifi = cli.addCommand("wifi");
  wifi.addSubCommand("scan").onExecute([&](Command&) { scan_called = true; });

  TEST_ASSERT_TRUE(cli.inject("wifi scan"));
  TEST_ASSERT_TRUE(scan_called);
}

static void test_subcommand_with_args() {
  AdvancedCLI cli;
  ArgStr h_ssid;
  const char* got_ssid = nullptr;

  auto& wifi    = cli.addCommand("wifi");
  auto& connect = wifi.addSubCommand("connect");
  h_ssid        = connect.addArg("ssid");
  connect.onExecute([&](Command& c) { got_ssid = c.getArg(h_ssid).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("wifi connect --ssid MyNet"));
  TEST_ASSERT_EQUAL_STRING("MyNet", got_ssid);
}

static void test_parent_command_without_subcommand() {
  AdvancedCLI cli;
  bool parent_called = false;

  auto& wifi = cli.addCommand("wifi");
  wifi.addSubCommand("scan").onExecute([](Command&) {});
  wifi.onExecute([&](Command&) { parent_called = true; });

  TEST_ASSERT_TRUE(cli.inject("wifi"));
  TEST_ASSERT_TRUE(parent_called);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                        Case sensitivity                                        */
/* ---------------------------------------------------------------------------------------------- */

static void test_case_insensitive_command_default() {
  AdvancedCLI cli; // default: case-insensitive
  bool called = false;

  cli.addCommand("ping").onExecute([&](Command&) { called = true; });

  TEST_ASSERT_TRUE(cli.inject("PING"));
  TEST_ASSERT_TRUE(called);
}

static void test_case_sensitive_command_mismatch_fails() {
  AdvancedCLI cli;
  cli.setCaseSensitive(true);
  bool called = false;

  cli.addCommand("ping").onExecute([&](Command&) { called = true; });

  TEST_ASSERT_FALSE(cli.inject("PING"));
  TEST_ASSERT_FALSE(called);
}

static void test_case_sensitive_command_exact_match() {
  AdvancedCLI cli;
  cli.setCaseSensitive(true);
  bool called = false;

  cli.addCommand("ping").onExecute([&](Command&) { called = true; });

  TEST_ASSERT_TRUE(cli.inject("ping"));
  TEST_ASSERT_TRUE(called);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                       onError() callback                                       */
/* ---------------------------------------------------------------------------------------------- */

static void test_onError_called_on_required_arg_missing() {
  AdvancedCLI cli;
  bool error_called = false;
  bool cb_called    = false;

  auto& cmd = cli.addCommand("req");
  cmd.addArg("must").setRequired();
  cmd.onExecute([&](Command&) { cb_called = true; });
  cmd.onError([&](Command&, const char*) { error_called = true; });

  TEST_ASSERT_FALSE(cli.inject("req"));
  TEST_ASSERT_TRUE(error_called);
  TEST_ASSERT_FALSE(cb_called);
}

static void test_onError_called_on_range_fail() {
  AdvancedCLI cli;
  bool error_called = false;

  auto& cmd = cli.addCommand("pin");
  cmd.addIntArg("num").setValidator([](int32_t v) { return v >= 0 && v <= 10; });
  cmd.onExecute([](Command&) {});
  cmd.onError([&](Command&, const char*) { error_called = true; });

  TEST_ASSERT_FALSE(cli.inject("pin --num 99"));
  TEST_ASSERT_TRUE(error_called);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                           cmd.fail()                                           */
/* ---------------------------------------------------------------------------------------------- */

static void test_fail_marks_parse_failed() {
  AdvancedCLI cli;

  cli.addCommand("boom").onExecute([](Command& c) { c.fail("something went wrong"); });

  TEST_ASSERT_FALSE(cli.inject("boom"));
  TEST_ASSERT_FALSE(cli.lastParseOk());
}

static void test_fail_routes_through_onError() {
  AdvancedCLI cli;
  const char* got_msg = nullptr;
  static char msg_buf[64];

  auto& cmd = cli.addCommand("boom");
  cmd.onExecute([](Command& c) { c.fail("kaboom"); });
  cmd.onError([&](Command&, const char* msg) {
    strncpy(msg_buf, msg, sizeof(msg_buf) - 1);
    msg_buf[sizeof(msg_buf) - 1] = '\0';
    got_msg                      = msg_buf;
  });

  TEST_ASSERT_FALSE(cli.inject("boom"));
  TEST_ASSERT_NOT_NULL(got_msg);
  TEST_ASSERT_NOT_EQUAL(0, strlen(got_msg)); // message forwarded
}

/* ---------------------------------------------------------------------------------------------- */
/*                                         getArgByName()                                         */
/* ---------------------------------------------------------------------------------------------- */

static void test_getArgByName_by_primary_name() {
  AdvancedCLI cli;
  const char* got = nullptr;

  cli.addCommand("cmd").addArg("key");
  cli.addCommand("cmd").onExecute([&](Command& c) {
    auto a = c.getArgByName("key");
    got    = a.getValue();
  });

  // Re-register properly
  AdvancedCLI cli2;
  cli2.addCommand("cmd").addArg("key").setAlias("k");
  cli2.addCommand("cmd").onExecute(nullptr); // overwrite not possible; build fresh:

  AdvancedCLI cli3;
  ArgStr h;
  auto& c3 = cli3.addCommand("cmd");
  h        = c3.addArg("key");
  h.setAlias("k");
  c3.onExecute([&](Command& c) {
    auto a = c.getArgByName("key");
    got    = a.getValue();
  });

  TEST_ASSERT_TRUE(cli3.inject("cmd --key hello"));
  TEST_ASSERT_EQUAL_STRING("hello", got);
}

static void test_getArgByName_by_alias() {
  AdvancedCLI cli;
  ArgStr h;
  const char* got = nullptr;

  auto& c = cli.addCommand("cmd");
  h       = c.addArg("key");
  h.setAlias("k");
  c.onExecute([&](Command& cmd) {
    auto a = cmd.getArgByName("k");
    got    = a.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("cmd --key world"));
  TEST_ASSERT_EQUAL_STRING("world", got);
}

static void test_getArgByName_unknown_returns_invalid() {
  AdvancedCLI cli;
  bool got_invalid = false;

  cli.addCommand("cmd").onExecute([&](Command& c) {
    auto a      = c.getArgByName("nonexistent");
    got_invalid = !a.isValid();
  });

  TEST_ASSERT_TRUE(cli.inject("cmd"));
  TEST_ASSERT_TRUE(got_invalid);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                          lastParseOk()                                         */
/* ---------------------------------------------------------------------------------------------- */

static void test_lastParseOk_true_after_success() {
  AdvancedCLI cli;
  cli.addCommand("ok").onExecute([](Command&) {});
  cli.inject("ok");
  TEST_ASSERT_TRUE(cli.lastParseOk());
}

static void test_lastParseOk_false_after_error() {
  AdvancedCLI cli;
  cli.inject("nonexistent");
  TEST_ASSERT_FALSE(cli.lastParseOk());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                onInvalid() per-argument callback                               */
/* ---------------------------------------------------------------------------------------------- */

static void test_onInvalid_called_on_validation_fail() {
  AdvancedCLI cli;
  bool invalid_called = false;

  auto& cmd  = cli.addCommand("pin");
  auto arg_h = cmd.addIntArg("num");
  arg_h.setValidator([](int32_t v) { return v >= 0 && v <= 10; });
  arg_h.onInvalid([&](const char*, const char*, const char*) { invalid_called = true; });
  cmd.onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("pin --num 99"));
  TEST_ASSERT_TRUE(invalid_called);
}

static void test_onInvalid_wins_over_onError() {
  AdvancedCLI cli;
  bool invalid_called = false;
  bool error_called   = false;

  auto& cmd  = cli.addCommand("pin");
  auto arg_h = cmd.addIntArg("num");
  arg_h.setValidator([](int32_t v) { return v >= 0 && v <= 10; });
  arg_h.onInvalid([&](const char*, const char*, const char*) { invalid_called = true; });
  cmd.onExecute([](Command&) {});
  cmd.onError([&](Command&, const char*) { error_called = true; });

  TEST_ASSERT_FALSE(cli.inject("pin --num 99"));
  TEST_ASSERT_TRUE(invalid_called);
  TEST_ASSERT_FALSE(error_called); // per-arg handler wins
}

/* ---------------------------------------------------------------------------------------------- */
/*                                     inject() output capture                                    */
/* ---------------------------------------------------------------------------------------------- */

static void test_inject_captures_output() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  cli.addCommand("help").onExecute([&](Command&) { cli.printHelp(); });

  char out[512] = {};
  bool ok       = cli.inject("help", out, sizeof(out));
  TEST_ASSERT_TRUE(ok);
  // printHelp emits at least the command name
  TEST_ASSERT_NOT_EQUAL(0, strlen(out));
}

/* ---------------------------------------------------------------------------------------------- */
/*                                    "--" positional separator                                   */
/* ---------------------------------------------------------------------------------------------- */

static void test_double_dash_forces_positional() {
  AdvancedCLI cli;
  ArgStr h_pos;
  const char* got = nullptr;

  auto& cmd = cli.addCommand("file");
  h_pos     = cmd.addPosArg("path");
  cmd.onExecute([&](Command& c) { got = c.getArg(h_pos).getValue(); });

  // "--path" would normally look like a named arg but after "--" it's positional
  TEST_ASSERT_TRUE(cli.inject("file -- --path"));
  TEST_ASSERT_EQUAL_STRING("--path", got);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                 Mixed named + flag + positional                                */
/* ---------------------------------------------------------------------------------------------- */

static void test_mixed_arg_types() {
  AdvancedCLI cli;
  ArgFlag h_del;
  ArgStr h_path;
  ArgInt h_mode;
  bool got_del         = false;
  const char* got_path = nullptr;
  int32_t got_mode     = 0;

  auto& cmd = cli.addCommand("file");
  h_del     = cmd.addFlag("delete");
  h_path    = cmd.addArg("path", "/tmp/x");
  h_mode    = cmd.addIntArg("mode", 0);
  cmd.onExecute([&](Command& c) {
    got_del  = c.getArg(h_del).isSet();
    got_path = c.getArg(h_path).getValue();
    got_mode = c.getArg(h_mode).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("file --delete --path /home/user --mode 7"));
  TEST_ASSERT_TRUE(got_del);
  TEST_ASSERT_EQUAL_STRING("/home/user", got_path);
  TEST_ASSERT_EQUAL(7, got_mode);
}

/* ---------------------------------------------------------------------------------------------- */
/*                     isSet() distinguishes default from explicitly provided                     */
/* ---------------------------------------------------------------------------------------------- */

static void test_isSet_false_when_only_default() {
  AdvancedCLI cli;
  ArgStr h;
  bool set = true;

  auto& cmd = cli.addCommand("cmd");
  h         = cmd.addArg("val", "default");
  cmd.onExecute([&](Command& c) { set = c.getArg(h).isSet(); });

  TEST_ASSERT_TRUE(cli.inject("cmd"));
  TEST_ASSERT_FALSE(set);
}

static void test_isSet_true_when_explicitly_provided() {
  AdvancedCLI cli;
  ArgStr h;
  bool set = false;

  auto& cmd = cli.addCommand("cmd");
  h         = cmd.addArg("val", "default");
  cmd.onExecute([&](Command& c) { set = c.getArg(h).isSet(); });

  TEST_ASSERT_TRUE(cli.inject("cmd --val explicit"));
  TEST_ASSERT_TRUE(set);
}

/* ---------------------------------------------------------------------------------------------- */
/*                           Unknown command - onUnknownCommand callback                          */
/* ---------------------------------------------------------------------------------------------- */

static void test_onUnknownCommand_called() {
  AdvancedCLI cli;
  const char* got_name = nullptr;
  static char name_buf[32];

  cli.onUnknownCommand([&](const char* name) {
    strncpy(name_buf, name, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    got_name                       = name_buf;
  });
  cli.addCommand("ping").onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("pong"));
  TEST_ASSERT_NOT_NULL(got_name);
  TEST_ASSERT_EQUAL_STRING("pong", got_name);
}

static void test_onUnknownCommand_not_called_for_known() {
  AdvancedCLI cli;
  bool called = false;

  cli.onUnknownCommand([&](const char*) { called = true; });
  cli.addCommand("ping").onExecute([](Command&) {});

  TEST_ASSERT_TRUE(cli.inject("ping"));
  TEST_ASSERT_FALSE(called);
}

/* ---------------------------------------------------------------------------------------------- */
/*                              printHelp(cmd_name) - single command                              */
/* ---------------------------------------------------------------------------------------------- */

static void test_printHelp_single_command_outputs_name() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  cli.addCommand("wifi").setDescription("WiFi commands");
  cli.addCommand("led").setDescription("LED control");

  cli.printHelp("wifi");
  TEST_ASSERT_NOT_EQUAL(0, strlen(cap.buf));
  // "wifi" should appear in output; "led" should not
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NULL(strstr(cap.buf, "led"));
}

/* ---------------------------------------------------------------------------------------------- */
/*                                       getParsedArgCount()                                      */
/* ---------------------------------------------------------------------------------------------- */

static void test_getParsedArgCount_zero_when_no_args_provided() {
  AdvancedCLI cli;
  ArgStr h_a;
  ArgStr h_b;
  uint8_t count = 99;

  auto& cmd = cli.addCommand("cmd");
  h_a       = cmd.addArg("a", "default_a");
  h_b       = cmd.addArg("b", "default_b");
  cmd.onExecute([&](Command& c) { count = c.getParsedArgCount(); });

  // Neither arg is explicitly provided - only defaults exist, so getParsedArgCount() == 0
  TEST_ASSERT_TRUE(cli.inject("cmd"));
  TEST_ASSERT_EQUAL(0, count);
}

static void test_getParsedArgCount_counts_provided_args() {
  AdvancedCLI cli;
  ArgStr h_a;
  ArgStr h_b;
  ArgFlag h_f;
  uint8_t count = 0;

  auto& cmd = cli.addCommand("cmd");
  h_a       = cmd.addArg("a", "default_a");
  h_b       = cmd.addArg("b", "default_b");
  h_f       = cmd.addFlag("flag");
  cmd.onExecute([&](Command& c) { count = c.getParsedArgCount(); });

  TEST_ASSERT_TRUE(cli.inject("cmd --a hello --flag"));
  TEST_ASSERT_EQUAL(2, count);
}

static void test_getParsedArgCount_all_when_all_provided() {
  AdvancedCLI cli;
  ArgStr h_a;
  ArgInt h_b;
  uint8_t count = 0;

  auto& cmd = cli.addCommand("cmd");
  h_a       = cmd.addArg("a");
  h_b       = cmd.addIntArg("b");
  cmd.onExecute([&](Command& c) { count = c.getParsedArgCount(); });

  TEST_ASSERT_TRUE(cli.inject("cmd --a hello --b 42"));
  TEST_ASSERT_EQUAL(2, count);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                     printHelp(depth)                                          */
/* ---------------------------------------------------------------------------------------------- */

static void test_printHelp_depth1_hides_subcommands_and_args() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  ArgInt h_angle;
  Command& servo = cli.addCommand("servo").setDescription("Servo control.");
  h_angle        = servo.addIntArg("angle").setRequired();
  servo.onExecute([](Command&) {});

  Command& wifi = cli.addCommand("wifi").setDescription("Wi-Fi management.");
  wifi.addSubCommand("scan").setDescription("Scan networks.").onExecute([](Command&) {});

  cli.printHelp(1);

  // Command names appear
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "servo"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  // Sub-commands and argument lines must NOT appear
  TEST_ASSERT_NULL(strstr(cap.buf, "scan"));
  TEST_ASSERT_NULL(strstr(cap.buf, "angle"));
}

static void test_printHelp_depth2_shows_subcommands_hides_args() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  ArgInt h_angle;
  Command& servo = cli.addCommand("servo").setDescription("Servo control.");
  h_angle        = servo.addIntArg("angle").setRequired();
  servo.onExecute([](Command&) {});

  Command& wifi = cli.addCommand("wifi").setDescription("Wi-Fi management.");
  wifi.addSubCommand("scan").setDescription("Scan networks.").onExecute([](Command&) {});

  cli.printHelp(2);

  // Sub-commands appear
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "scan"));
  // Argument lines must NOT appear
  TEST_ASSERT_NULL(strstr(cap.buf, "angle"));
}

static void test_printHelp_depth3_shows_everything() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  ArgInt h_angle;
  Command& servo = cli.addCommand("servo").setDescription("Servo control.");
  h_angle        = servo.addIntArg("angle").setRequired();
  servo.onExecute([](Command&) {});

  Command& wifi = cli.addCommand("wifi").setDescription("Wi-Fi management.");
  wifi.addSubCommand("scan").setDescription("Scan networks.").onExecute([](Command&) {});

  cli.printHelp(3);

  // All three levels present
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "servo"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "scan"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "angle"));
}

static void test_printHelp_default_depth_equals_3() {
  AdvancedCLI cli;
  OutputCapture cap_default;
  OutputCapture cap_3;
  cli.setOutput(cap_default.fn());

  ArgInt h_angle;
  Command& servo = cli.addCommand("servo").setDescription("Servo control.");
  h_angle        = servo.addIntArg("angle").setRequired();
  servo.onExecute([](Command&) {});

  cli.printHelp();
  cli.setOutput(cap_3.fn());
  cli.printHelp(3);

  TEST_ASSERT_EQUAL_STRING(cap_default.buf, cap_3.buf);
}

/* ---------------------------------------------------------------------------------------------- */
/*                                Duplicate argument name detection                               */
/* ---------------------------------------------------------------------------------------------- */

static void test_duplicate_arg_name_returns_invalid() {
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("cmd");
  ArgStr h1 = cmd.addArg("key");
  ArgStr h2 = cmd.addArg("key"); // duplicate

  TEST_ASSERT_TRUE(h1.isValid());
  TEST_ASSERT_FALSE(h2.isValid()); // second registration should fail
}

/* ---------------------------------------------------------------------------------------------- */
/*                                          setup / loop                                          */
/* ---------------------------------------------------------------------------------------------- */

void setup() {
  Serial.begin(115200);
  delay(2000);

  UNITY_BEGIN();

  // Basic dispatch
  RUN_TEST(test_basic_dispatch);
  RUN_TEST(test_unknown_command_returns_false);
  RUN_TEST(test_commandCount);

  // Named string arg
  RUN_TEST(test_named_string_arg_provided);
  RUN_TEST(test_named_string_arg_default);
  RUN_TEST(test_named_string_arg_no_default_returns_empty);

  // Flag arg
  RUN_TEST(test_flag_present);
  RUN_TEST(test_flag_absent);

  // Integer arg
  RUN_TEST(test_int_arg_provided);
  RUN_TEST(test_int_arg_default);
  RUN_TEST(test_int_arg_negative);

  // Float arg
  RUN_TEST(test_float_arg_provided);
  RUN_TEST(test_float_arg_default);
  RUN_TEST(test_float_arg_negative);
  RUN_TEST(test_float_arg_negative_leading_dot);

  // Required arg
  RUN_TEST(test_required_arg_missing_fails);
  RUN_TEST(test_required_arg_provided_succeeds);

  // Positional arg
  RUN_TEST(test_positional_arg);
  RUN_TEST(test_positional_arg_default);
  RUN_TEST(test_multiple_positional_args);

  // setValidator
  RUN_TEST(test_int_validation_fn_valid);
  RUN_TEST(test_int_validation_fn_below_min_fails);
  RUN_TEST(test_int_validation_fn_above_max_fails);
  RUN_TEST(test_float_validation_fn_valid);
  RUN_TEST(test_float_validation_fn_exceeds_fails);
  RUN_TEST(test_str_validation_fn_valid);
  RUN_TEST(test_str_validation_fn_invalid_fails);

  // Aliases
  RUN_TEST(test_alias_matches_arg);

  // Sub-commands
  RUN_TEST(test_subcommand_dispatch);
  RUN_TEST(test_subcommand_with_args);
  RUN_TEST(test_parent_command_without_subcommand);

  // Case sensitivity
  RUN_TEST(test_case_insensitive_command_default);
  RUN_TEST(test_case_sensitive_command_mismatch_fails);
  RUN_TEST(test_case_sensitive_command_exact_match);

  // onError()
  RUN_TEST(test_onError_called_on_required_arg_missing);
  RUN_TEST(test_onError_called_on_range_fail);

  // cmd.fail()
  RUN_TEST(test_fail_marks_parse_failed);
  RUN_TEST(test_fail_routes_through_onError);

  // getArgByName()
  RUN_TEST(test_getArgByName_by_primary_name);
  RUN_TEST(test_getArgByName_by_alias);
  RUN_TEST(test_getArgByName_unknown_returns_invalid);

  // lastParseOk()
  RUN_TEST(test_lastParseOk_true_after_success);
  RUN_TEST(test_lastParseOk_false_after_error);

  // onInvalid()
  RUN_TEST(test_onInvalid_called_on_validation_fail);
  RUN_TEST(test_onInvalid_wins_over_onError);

  // inject() output capture
  RUN_TEST(test_inject_captures_output);

  // "--" separator
  RUN_TEST(test_double_dash_forces_positional);

  // Mixed arg types
  RUN_TEST(test_mixed_arg_types);

  // isSet()
  RUN_TEST(test_isSet_false_when_only_default);
  RUN_TEST(test_isSet_true_when_explicitly_provided);

  // onUnknownCommand
  RUN_TEST(test_onUnknownCommand_called);
  RUN_TEST(test_onUnknownCommand_not_called_for_known);

  // printHelp(cmd_name)
  RUN_TEST(test_printHelp_single_command_outputs_name);

  // Duplicate arg detection
  RUN_TEST(test_duplicate_arg_name_returns_invalid);

  // getParsedArgCount()
  RUN_TEST(test_getParsedArgCount_zero_when_no_args_provided);
  RUN_TEST(test_getParsedArgCount_counts_provided_args);
  RUN_TEST(test_getParsedArgCount_all_when_all_provided);

  // printHelp(depth)
  RUN_TEST(test_printHelp_depth1_hides_subcommands_and_args);
  RUN_TEST(test_printHelp_depth2_shows_subcommands_hides_args);
  RUN_TEST(test_printHelp_depth3_shows_everything);
  RUN_TEST(test_printHelp_default_depth_equals_3);

  UNITY_END();
}

void loop() {}
