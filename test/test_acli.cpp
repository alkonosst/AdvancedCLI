/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

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
 * - getCommandCount()
 * - getArgCount()
 * - getAttemptedCommandCount() / getAttemptedArgCount()
 * - Unknown command + onUnknownCommand callback
 * - onInvalid() per-argument callback
 * - Multiple args in one command
 * - "--" positional separator
 * - printHelp(cmd_name)
 * - printHelp(const Command&) and Command::printHelp()
 * - Duplicate arg name detection
 * - Persistent arguments (addPersistentIntArg / addPersistentFlag / getArgByName fallback)
 * - Defensive/edge case tests
 */

#ifdef ARDUINO
#  include <Arduino.h>
#else
#  include <cstring>
#endif

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
      int sl = static_cast<int>(strlen(s));
      if (len + sl < static_cast<int>(sizeof(buf)) - 1) {
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

static void test_getCommandCount() {
  AdvancedCLI cli;
  TEST_ASSERT_EQUAL(0, cli.getCommandCount());
  cli.addCommand("a");
  cli.addCommand("b");
  TEST_ASSERT_EQUAL(2, cli.getCommandCount());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                          getArgCount()                                         */
/* ---------------------------------------------------------------------------------------------- */

static void test_getArgCount_zero_with_no_commands() {
  AdvancedCLI cli;
  TEST_ASSERT_EQUAL(0, cli.getArgCount());
}

static void test_getArgCount_increments_per_command() {
  AdvancedCLI cli;
  // getArgCount() returns the number of actual argument registrations, not reserved slots.
  auto& a = cli.addCommand("a");
  a.addArg("x"); // 1 slot
  auto& b = cli.addCommand("b");
  b.addIntArg("y"); // 1 slot
  b.addIntArg("z"); // 1 slot
  TEST_ASSERT_EQUAL(3, cli.getArgCount());
}

static void test_getArgCount_includes_subcommands() {
  AdvancedCLI cli;
  // getArgCount() reflects actual addArg() calls across parent and child commands.
  auto& parent = cli.addCommand("parent");
  parent.addArg("p1"); // 1 slot
  auto& child = parent.addSubCommand("child");
  child.addArg("c1");    // 1 slot
  child.addIntArg("c2"); // 1 slot
  TEST_ASSERT_EQUAL(3, cli.getArgCount());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                           isValid()                                            */
/* ---------------------------------------------------------------------------------------------- */

static void test_isValid_true_on_empty_cli() {
  AdvancedCLI cli;
  TEST_ASSERT_TRUE(cli.isValid());
}

static void test_isValid_true_after_normal_registration() {
  AdvancedCLI cli;
  cli.addCommand("ping").onExecute([](Command&) {});
  cli.addCommand("pong").onExecute([](Command&) {});
  TEST_ASSERT_TRUE(cli.isValid());
}

static void test_isValid_false_on_command_overflow() {
  // Fill the command table to capacity, then attempt one more registration.
  AdvancedCLI cli;
  char name[4] = "c0";
  for (uint8_t i = 0; i < Config::MAX_COMMANDS; ++i) {
    name[1] = static_cast<char>('0' + (i % 10));
    name[2] = static_cast<char>('0' + (i / 10));
    name[3] = '\0';
    cli.addCommand(name);
  }
  // Table is now full; this extra call must set _overflow.
  cli.addCommand("overflow");
  TEST_ASSERT_FALSE(cli.isValid());
}

static void test_isValid_false_on_args_overflow() {
  // Fill the entire argument pool using a single command, then attempt one more addArg.
  // A single command can hold all MAX_ARGS_TOTAL slots because there is no per-command limit.
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("fill");

  // Names must outlive the loop
  char names[Config::MAX_ARGS_TOTAL][4] = {};

  for (uint8_t i = 0; i < Config::MAX_ARGS_TOTAL; ++i) {
    names[i][0] = 'a';
    names[i][1] = static_cast<char>('0' + (i % 10));
    names[i][2] = static_cast<char>('0' + (i / 10));
    names[i][3] = '\0';
    cmd.addIntArg(names[i]);
  }
  TEST_ASSERT_EQUAL(Config::MAX_ARGS_TOTAL, cli.getArgCount());

  // One more arg must trigger overflow.
  cmd.addIntArg("boom");
  TEST_ASSERT_FALSE(cli.isValid());
}

/* ---------------------------------------------------------------------------------------------- */
/*                          getAttemptedCommandCount() / getAttemptedArgCount()                   */
/* ---------------------------------------------------------------------------------------------- */

static void test_getAttemptedCommandCount_no_overflow() {
  // When no overflow occurs, attempted equals registered.
  AdvancedCLI cli;
  cli.addCommand("a");
  cli.addCommand("b");
  TEST_ASSERT_EQUAL(2, cli.getAttemptedCommandCount());
  TEST_ASSERT_EQUAL(cli.getCommandCount(), cli.getAttemptedCommandCount());
}

static void test_getAttemptedCommandCount_overflow() {
  // When overflow occurs, attempted exceeds registered by the number of dropped calls.
  AdvancedCLI cli;
  char name[4] = "c0";
  for (uint8_t i = 0; i < Config::MAX_COMMANDS; ++i) {
    name[1] = static_cast<char>('0' + (i % 10));
    name[2] = static_cast<char>('0' + (i / 10));
    name[3] = '\0';
    cli.addCommand(name);
  }
  cli.addCommand("overflow"); // one extra
  TEST_ASSERT_EQUAL(Config::MAX_COMMANDS, cli.getCommandCount());
  TEST_ASSERT_EQUAL(Config::MAX_COMMANDS + 1, cli.getAttemptedCommandCount());
}

static void test_getAttemptedArgCount_no_overflow() {
  // When no overflow occurs, attempted equals registered.
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("cmd");
  cmd.addArg("a");
  cmd.addArg("b");
  TEST_ASSERT_EQUAL(2, cli.getAttemptedArgCount());
  TEST_ASSERT_EQUAL(cli.getArgCount(), cli.getAttemptedArgCount());
}

static void test_getAttemptedArgCount_overflow() {
  // When overflow occurs, attempted exceeds registered by the number of dropped calls.
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("fill");

  char names[Config::MAX_ARGS_TOTAL + 1][4] = {};
  for (uint16_t i = 0; i <= Config::MAX_ARGS_TOTAL; ++i) {
    names[i][0] = 'a';
    names[i][1] = static_cast<char>('0' + (i % 10));
    names[i][2] = static_cast<char>('0' + (i / 10));
    names[i][3] = '\0';
    cmd.addIntArg(names[i]);
  }
  TEST_ASSERT_EQUAL(Config::MAX_ARGS_TOTAL, cli.getArgCount());
  TEST_ASSERT_EQUAL(Config::MAX_ARGS_TOTAL + 1, cli.getAttemptedArgCount());
}

static void test_getAttemptedCommandCount_subcommand_of_overflow() {
  // Sub-commands added onto an overflowed (dummy) parent must still be counted.
  AdvancedCLI cli;
  char name[4] = "c0";
  for (uint8_t i = 0; i < Config::MAX_COMMANDS; ++i) {
    name[1] = static_cast<char>('0' + (i % 10));
    name[2] = static_cast<char>('0' + (i / 10));
    name[3] = '\0';
    cli.addCommand(name);
  }
  auto& overflow = cli.addCommand("overflow"); // attempt MAX_COMMANDS + 1
  overflow.addSubCommand("child");             // attempt MAX_COMMANDS + 2
  TEST_ASSERT_EQUAL(Config::MAX_COMMANDS + 2, cli.getAttemptedCommandCount());
}

static void test_getAttemptedArgCount_arg_on_overflow_command() {
  // Args added onto an overflowed (dummy) command must still be counted.
  AdvancedCLI cli;
  char name[4] = "c0";
  for (uint8_t i = 0; i < Config::MAX_COMMANDS; ++i) {
    name[1] = static_cast<char>('0' + (i % 10));
    name[2] = static_cast<char>('0' + (i / 10));
    name[3] = '\0';
    cli.addCommand(name);
  }
  auto& overflow = cli.addCommand("overflow"); // command overflows
  overflow.addArg("a");                        // must be counted despite command overflow
  overflow.addArg("b");                        // same
  TEST_ASSERT_EQUAL(2, cli.getAttemptedArgCount());
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

static void test_named_string_arg_json_with_quotes() {
  AdvancedCLI cli;
  ArgStr h;
  const char* received = nullptr;

  auto& cmd = cli.addCommand("cmd");
  h         = cmd.addArg("val");
  cmd.onExecute([&](Command& c) {
    auto a   = c.getArg(h);
    received = a.getValue();
  });

  // Using quotes around the JSON value allows it to be parsed as a single argument without needing
  // to escape inner spaces.
  TEST_ASSERT_TRUE(cli.inject(R"(cmd --val '{"key": "value","num": 42}')"));
  TEST_ASSERT_EQUAL_STRING(R"({"key": "value","num": 42})", received);

  // Even without spaces, quotes should still work and be stripped from the value.
  TEST_ASSERT_TRUE(cli.inject(R"(cmd --val '{"key":"value","num":42}')"));
  TEST_ASSERT_EQUAL_STRING(R"({"key":"value","num":42})", received);
}

static void test_named_string_arg_json_without_quotes() {
  AdvancedCLI cli;
  ArgStr h;
  const char* received = nullptr;

  auto& cmd = cli.addCommand("cmd");
  h         = cmd.addArg("val");
  cmd.onExecute([&](Command& c) {
    auto a   = c.getArg(h);
    received = a.getValue();
  });

  // If the JSON value has no spaces, it should be parsed correctly even without quotes.
  TEST_ASSERT_TRUE(cli.inject(R"(cmd --val {"key":"value","num":42})"));
  TEST_ASSERT_EQUAL_STRING(R"({"key":"value","num":42})", received);

  // However, if there are spaces, the value will be truncated at the first space since it's not
  // quoted.
  TEST_ASSERT_FALSE(cli.inject(R"(cmd --val {"key": "value","num": 42})"));
  TEST_ASSERT_EQUAL_STRING(R"({"key":)", received);
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
/*                                     printHelp(depth)                                           */
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
/*                                       Persistent arguments                                     */
/* ---------------------------------------------------------------------------------------------- */

static void test_persistent_arg_before_subcommand() {
  // "joy -n 2 cal" - persistent -n on parent, read inside sub-command callback
  AdvancedCLI cli;
  ArgInt h_n;
  int32_t got_n   = -1;
  bool cal_called = false;

  Command& joy = cli.addCommand("joy");
  h_n          = joy.addPersistentIntArg("n", 0);

  joy.addSubCommand("cal").onExecute([&](Command& cmd) {
    cal_called = true;
    got_n      = cmd.getArg(h_n).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("joy -n 2 cal"));
  TEST_ASSERT_TRUE(cal_called);
  TEST_ASSERT_EQUAL(2, got_n);
}

static void test_persistent_arg_default_used_when_absent() {
  // "joy cal" - persistent -n absent, default 0 should be returned
  AdvancedCLI cli;
  ArgInt h_n;
  int32_t got_n = -1;

  Command& joy = cli.addCommand("joy");
  h_n          = joy.addPersistentIntArg("n", 0);

  joy.addSubCommand("cal").onExecute([&](Command& cmd) { got_n = cmd.getArg(h_n).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("joy cal"));
  TEST_ASSERT_EQUAL(0, got_n);
}

static void test_persistent_arg_readable_via_getArgByName_in_sub() {
  // getArgByName("n") inside a sub-command falls back to parent's persistent arg
  AdvancedCLI cli;
  int32_t got_n = -1;

  Command& joy = cli.addCommand("joy");
  joy.addPersistentIntArg("n", 7);

  joy.addSubCommand("cal").onExecute([&](Command& cmd) {
    auto a = cmd.getArgByName("n");
    got_n  = a.isValid() ? static_cast<int32_t>(strtol(a.getValue(), nullptr, 0)) : -99;
  });

  TEST_ASSERT_TRUE(cli.inject("joy -n 3 cal"));
  TEST_ASSERT_EQUAL(3, got_n);
}

static void test_persistent_flag_before_subcommand() {
  // "joy -verbose cal" - persistent flag on parent, read via getArg inside sub
  AdvancedCLI cli;
  ArgFlag h_v;
  bool got_verbose = false;

  Command& joy = cli.addCommand("joy");
  h_v          = joy.addPersistentFlag("verbose");

  joy.addSubCommand("cal").onExecute([&](Command& cmd) { got_verbose = cmd.getArg(h_v).isSet(); });

  TEST_ASSERT_TRUE(cli.inject("joy -verbose cal"));
  TEST_ASSERT_TRUE(got_verbose);
}

static void test_persistent_flag_absent_is_false() {
  AdvancedCLI cli;
  ArgFlag h_v;
  bool got_verbose = true; // start true

  Command& joy = cli.addCommand("joy");
  h_v          = joy.addPersistentFlag("verbose");

  joy.addSubCommand("cal").onExecute([&](Command& cmd) { got_verbose = cmd.getArg(h_v).isSet(); });

  TEST_ASSERT_TRUE(cli.inject("joy cal"));
  TEST_ASSERT_FALSE(got_verbose);
}

static void test_persistent_required_arg_missing_fails() {
  // Required persistent arg not provided -> parse should fail, callback not called
  AdvancedCLI cli;
  bool called = false;

  Command& joy = cli.addCommand("joy");
  joy.addPersistentIntArg("n").setRequired();

  joy.addSubCommand("cal").onExecute([&](Command&) { called = true; });

  TEST_ASSERT_FALSE(cli.inject("joy cal"));
  TEST_ASSERT_FALSE(called);
}

static void test_persistent_required_arg_provided_succeeds() {
  AdvancedCLI cli;
  ArgInt h_n;
  bool called = false;
  int32_t got = -1;

  Command& joy = cli.addCommand("joy");
  h_n          = joy.addPersistentIntArg("n");
  h_n.setRequired();

  joy.addSubCommand("cal").onExecute([&](Command& cmd) {
    called = true;
    got    = cmd.getArg(h_n).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("joy -n 1 cal"));
  TEST_ASSERT_TRUE(called);
  TEST_ASSERT_EQUAL(1, got);
}

static void test_persistent_arg_multiple_subcommands_share_it() {
  // Same persistent handle should be readable in different sub-commands
  AdvancedCLI cli;
  ArgInt h_n;
  int32_t got_cal    = -1;
  int32_t got_filter = -1;

  Command& joy = cli.addCommand("joy");
  h_n          = joy.addPersistentIntArg("n", 0);

  joy.addSubCommand("cal").onExecute([&](Command& cmd) { got_cal = cmd.getArg(h_n).getValue(); });
  joy.addSubCommand("filter").onExecute(
    [&](Command& cmd) { got_filter = cmd.getArg(h_n).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("joy -n 2 cal"));
  TEST_ASSERT_TRUE(cli.inject("joy -n 3 filter"));
  TEST_ASSERT_EQUAL(2, got_cal);
  TEST_ASSERT_EQUAL(3, got_filter);
}

static void test_persistent_arg_parent_standalone_still_works() {
  // Parent command invoked directly (no sub-command) still reads its persistent arg normally
  AdvancedCLI cli;
  ArgInt h_n;
  int32_t got = -1;

  Command& joy = cli.addCommand("joy");
  h_n          = joy.addPersistentIntArg("n", 5);
  joy.addSubCommand("cal").onExecute([](Command&) {});
  joy.onExecute([&](Command& cmd) { got = cmd.getArg(h_n).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("joy -n 9"));
  TEST_ASSERT_EQUAL(9, got);
}

static void test_persistent_arg_argCount_includes_persistent() {
  // addPersistentIntArg must consume a pool slot
  AdvancedCLI cli;
  Command& joy = cli.addCommand("joy");
  joy.addPersistentIntArg("n");
  joy.addSubCommand("cal").addIntArg("x");
  TEST_ASSERT_EQUAL(2, cli.getArgCount()); // 1 persistent + 1 sub-cmd arg
}

static void test_persistent_arg_registered_after_subcommand_fails() {
  // Adding an arg to a parent after its first sub-command is registered must be blocked.
  // Otherwise the parent's arg pool slot and the sub-command's pool start alias each other.
  AdvancedCLI cli;
  Command& joy = cli.addCommand("joy");
  joy.addSubCommand("cal");                  // seals joy
  ArgInt h_n = joy.addPersistentIntArg("n"); // must fail: returns invalid handle

  TEST_ASSERT_FALSE(h_n.isValid());
  TEST_ASSERT_FALSE(cli.isValid()); // overflow flag must be set
}

/* ---------------------------------------------------------------------------------------------- */
/*                       printHelp(const Command&) and Command::printHelp()                       */
/* ---------------------------------------------------------------------------------------------- */

static void test_printHelp_by_ref_prints_specific_command() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& wifi = cli.addCommand("wifi").setDescription("WiFi commands");
  cli.addCommand("led").setDescription("LED control");

  cli.printHelp(wifi);

  // "wifi" appears; "led" does not
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NULL(strstr(cap.buf, "led"));
}

static void test_printHelp_by_ref_disambiguates_same_name_subcommands() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& battery  = cli.addCommand("battery");
  Command& batt_ctl = battery.addSubCommand("control").setDescription("Battery control");

  Command& system  = cli.addCommand("system");
  Command& sys_ctl = system.addSubCommand("control").setDescription("System control");

  // Print only battery's "control" - should show "Battery control", not "System control"
  cli.printHelp(batt_ctl);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "Battery control"));
  TEST_ASSERT_NULL(strstr(cap.buf, "System control"));

  cap.clear();

  // Print only system's "control" - should show "System control", not "Battery control"
  cli.printHelp(sys_ctl);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "System control"));
  TEST_ASSERT_NULL(strstr(cap.buf, "Battery control"));
}

static void test_printHelp_by_ref_excludes_sibling_commands() {
  // Printing a top-level command by ref must not include siblings
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& wifi = cli.addCommand("wifi").setDescription("WiFi commands");
  cli.addCommand("ble").setDescription("BLE commands");

  cli.printHelp(wifi);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NULL(strstr(cap.buf, "ble"));
}

static void test_printHelp_by_ref_depth_control() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& wifi = cli.addCommand("wifi");
  wifi.addIntArg("channel");
  wifi.addSubCommand("scan").setDescription("Scan networks");

  // depth=1: command name only - no sub-commands, no args
  cli.printHelp(wifi, 1);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NULL(strstr(cap.buf, "scan"));
  TEST_ASSERT_NULL(strstr(cap.buf, "channel"));

  cap.clear();

  // depth=2: command + sub-commands, no args
  cli.printHelp(wifi, 2);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "scan"));
  TEST_ASSERT_NULL(strstr(cap.buf, "channel"));

  cap.clear();

  // depth=3 (default): everything
  cli.printHelp(wifi);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "scan"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "channel"));
}

static void test_command_printHelp_prints_own_command() {
  // Command::printHelp() called directly (outside a callback) prints only that command
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& wifi = cli.addCommand("wifi").setDescription("WiFi commands");
  cli.addCommand("led").setDescription("LED control");

  wifi.printHelp();

  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NULL(strstr(cap.buf, "led"));
}

static void test_command_printHelp_inside_callback() {
  // Command::printHelp() called from inside a callback prints only that command
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& wifi = cli.addCommand("wifi").setDescription("WiFi commands");
  cli.addCommand("led").setDescription("LED control");
  wifi.onExecute([](Command& cmd) { cmd.printHelp(); });

  cli.inject("wifi");

  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NULL(strstr(cap.buf, "led"));
}

static void test_command_printHelp_disambiguates_from_callback() {
  // The core use-case: two sub-commands share the name "control" under different parents.
  // printHelp("control") always finds the first one registered; cmd.printHelp() inside the
  // callback is unambiguous because it operates on the exact instance being executed.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& battery = cli.addCommand("battery");
  battery.addSubCommand("control").setDescription("Battery control").onExecute([](Command& cmd) {
    cmd.printHelp();
  });

  Command& system = cli.addCommand("system");
  system.addSubCommand("control").setDescription("System control").onExecute([](Command& cmd) {
    cmd.printHelp();
  });

  // Invoke battery's control
  cli.inject("battery control");
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "Battery control"));
  TEST_ASSERT_NULL(strstr(cap.buf, "System control"));

  cap.clear();

  // Invoke system's control
  cli.inject("system control");
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "System control"));
  TEST_ASSERT_NULL(strstr(cap.buf, "Battery control"));
}

static void test_command_printHelp_depth_control() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& wifi = cli.addCommand("wifi");
  wifi.addIntArg("channel");
  wifi.addSubCommand("scan").setDescription("Scan networks");

  // depth=1
  wifi.printHelp(1);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NULL(strstr(cap.buf, "scan"));
  TEST_ASSERT_NULL(strstr(cap.buf, "channel"));

  cap.clear();

  // depth=2
  wifi.printHelp(2);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "scan"));
  TEST_ASSERT_NULL(strstr(cap.buf, "channel"));

  cap.clear();

  // depth=3 (default)
  wifi.printHelp();
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "scan"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "channel"));
}

/* ---------------------------------------------------------------------------------------------- */
/*                         Builder-handle query methods (ArgBaseImpl)                             */
/* ---------------------------------------------------------------------------------------------- */

static void test_builder_handle_query_methods() {
  // isSet() and operator bool() called on the builder handle itself (not the parsed reader).
  AdvancedCLI cli;
  ArgStr h;
  bool set_via_builder  = false;
  bool bool_via_builder = false;

  auto& cmd = cli.addCommand("q");
  h         = cmd.addArg("v");
  cmd.onExecute([&](Command&) {
    set_via_builder  = h.isSet();
    bool_via_builder = static_cast<bool>(h);
  });

  TEST_ASSERT_TRUE(cli.inject("q --v x"));
  TEST_ASSERT_TRUE(set_via_builder);
  TEST_ASSERT_TRUE(bool_via_builder);
}

/* ---------------------------------------------------------------------------------------------- */
/*                 Builder methods across all argument types (template instances)                 */
/* ---------------------------------------------------------------------------------------------- */

static void test_builder_methods_all_types() {
  // Exercises setAlias/setDescription/setRequired/onInvalid for every handle type, ensuring each
  // template instantiation is covered (gcov tracks instantiations separately).
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("build");

  ArgStr s = cmd.addArg("s");
  s.setAlias("salias")
    .setDescription("string arg")
    .onInvalid([](const char*, const char*, const char*) {});

  ArgFlag f = cmd.addFlag("f");
  f.setAlias("falias")
    .setDescription("flag arg")
    .setRequired()
    .onInvalid([](const char*, const char*, const char*) {});

  ArgInt i = cmd.addIntArg("i");
  i.setAlias("ialias").setDescription("int arg");

  ArgFloat fl = cmd.addFloatArg("fl");
  fl.setAlias("flalias")
    .setDescription("float arg")
    .setRequired()
    .onInvalid([](const char*, const char*, const char*) {});

  TEST_ASSERT_TRUE(s.isValid());
  TEST_ASSERT_TRUE(f.isValid());
  TEST_ASSERT_TRUE(i.isValid());
  TEST_ASSERT_TRUE(fl.isValid());
}

static void test_argbase_direct_instantiation() {
  // The CRTP base's complete-object constructor is emitted by the explicit template instantiations
  // but never invoked through the derived handles. Construct each base directly to cover it.
  detail::ArgBase<ArgStr> a_str(nullptr, -1);
  detail::ArgBase<ArgFlag> a_flag(nullptr, -1);
  detail::ArgBase<ArgInt> a_int(nullptr, -1);
  detail::ArgBase<ArgFloat> a_float(nullptr, -1);

  TEST_ASSERT_FALSE(a_str.isValid());
  TEST_ASSERT_FALSE(a_flag.isValid());
  TEST_ASSERT_FALSE(a_int.isValid());
  TEST_ASSERT_FALSE(a_float.isValid());
}

/* ---------------------------------------------------------------------------------------------- */
/*                        Reader query methods (ArgReaderBase / ParsedAny)                        */
/* ---------------------------------------------------------------------------------------------- */

static void test_reader_query_methods() {
  AdvancedCLI cli;
  ArgStr h;
  const char* name   = nullptr;
  const char* desc   = nullptr;
  bool reader_bool   = false;
  const char* anyval = nullptr;

  auto& cmd = cli.addCommand("r");
  h         = cmd.addArg("v").setDescription("the v");
  cmd.onExecute([&](Command& c) {
    auto reader  = c.getArg(h); // ParsedStr
    name         = reader.getName();
    desc         = reader.getDescription();
    reader_bool  = static_cast<bool>(reader);
    ParsedAny pa = reader; // ParsedAny(const ArgReaderBase&) implicit conversion
    anyval       = pa.getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("r --v hello"));
  TEST_ASSERT_EQUAL_STRING("v", name);
  TEST_ASSERT_EQUAL_STRING("the v", desc);
  TEST_ASSERT_TRUE(reader_bool);
  TEST_ASSERT_EQUAL_STRING("hello", anyval);
}

static void test_getArg_foreign_handle_returns_invalid() {
  // getArg() with a handle that belongs to another (non-parent) command must return an invalid
  // reader. Covers every getArg<T> instantiation's "no match" return.
  AdvancedCLI cli;
  ArgStr hs;
  ArgInt hi;
  ArgFloat hf;
  ArgFlag hfl;
  bool all_invalid = false;

  auto& a = cli.addCommand("a");
  hs      = a.addArg("s");
  hi      = a.addIntArg("i");
  hf      = a.addFloatArg("f");
  hfl     = a.addFlag("fl");

  auto& b = cli.addCommand("b");
  b.onExecute([&](Command& c) {
    all_invalid = !c.getArg(hs).isValid() && !c.getArg(hi).isValid() && !c.getArg(hf).isValid() &&
                  !c.getArg(hfl).isValid();
  });

  TEST_ASSERT_TRUE(cli.inject("b"));
  TEST_ASSERT_TRUE(all_invalid);
}

/* ---------------------------------------------------------------------------------------------- */
/*                       Positional typed args / persistent string + float                       */
/* ---------------------------------------------------------------------------------------------- */

static void test_addPosIntArg_and_addPosFloatArg() {
  AdvancedCLI cli;
  ArgInt hi;
  ArgFloat hf;
  int32_t gi = 0;
  float gf   = 0.f;

  auto& cmd = cli.addCommand("pt");
  hi        = cmd.addPosIntArg("n");
  hf        = cmd.addPosFloatArg("f");
  cmd.onExecute([&](Command& c) {
    gi = c.getArg(hi).getValue();
    gf = c.getArg(hf).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("pt 42 3.5"));
  TEST_ASSERT_EQUAL(42, gi);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.5f, gf);
}

static void test_persistent_string_arg_read_from_sub() {
  // addPersistentArg (string) provided, read inside the sub-command (getArg<ArgStr> parent path).
  AdvancedCLI cli;
  ArgStr h;
  const char* got = nullptr;

  auto& joy = cli.addCommand("joy");
  h         = joy.addPersistentArg("tag", "none");
  joy.addSubCommand("cal").onExecute([&](Command& c) { got = c.getArg(h).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("joy -tag hello cal"));
  TEST_ASSERT_EQUAL_STRING("hello", got);
}

static void test_persistent_string_arg_default_in_sub() {
  AdvancedCLI cli;
  ArgStr h;
  const char* got = nullptr;

  auto& joy = cli.addCommand("joy");
  h         = joy.addPersistentArg("tag", "none");
  joy.addSubCommand("cal").onExecute([&](Command& c) { got = c.getArg(h).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("joy cal"));
  TEST_ASSERT_EQUAL_STRING("none", got);
}

static void test_persistent_float_arg_variants_read_from_sub() {
  // addPersistentFloatArg with and without default, read inside the sub-command
  // (getArg<ArgFloat> parent path).
  AdvancedCLI cli;
  ArgFloat hx;
  ArgFloat hy;
  float gx = 0.f;
  float gy = 0.f;

  auto& joy = cli.addCommand("joy");
  hx        = joy.addPersistentFloatArg("x");       // no default
  hy        = joy.addPersistentFloatArg("y", 2.5f); // with default
  joy.addSubCommand("cal").onExecute([&](Command& c) {
    gx = c.getArg(hx).getValue();
    gy = c.getArg(hy).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("joy -x 1.5 cal"));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, gx);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, gy); // default used
}

static void test_command_isValid() {
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("c");
  TEST_ASSERT_TRUE(cmd.isValid());

  Command dummy; // default-constructed, never registered
  TEST_ASSERT_FALSE(dummy.isValid());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                Parse error / default paths                                     */
/* ---------------------------------------------------------------------------------------------- */

static void test_did_you_mean_suggestion() {
  // Unknown command that is a prefix of a registered command, with no onUnknownCommand handler.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  cli.addCommand("ping").onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("pin"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "Did you mean"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "ping"));
}

static void test_unknown_argument_main_loop_fails() {
  AdvancedCLI cli;
  bool called = false;
  auto& cmd   = cli.addCommand("cmd");
  cmd.addArg("known");
  cmd.onExecute([&](Command&) { called = true; });

  // An unknown argument must fail the parse AND prevent the callback from executing.
  TEST_ASSERT_FALSE(cli.inject("cmd --bogus"));
  TEST_ASSERT_FALSE(called);
}

static void test_named_arg_uses_default_when_value_absent() {
  // Named args present without a following value fall back to their defaults.
  AdvancedCLI cli;
  ArgInt hi;
  ArgStr hs;
  int32_t gi     = 0;
  const char* gs = nullptr;

  auto& cmd = cli.addCommand("cmd");
  hi        = cmd.addIntArg("i", 9);  // typed default -> null token branch
  hs        = cmd.addArg("s", "def"); // string default -> str token branch
  cmd.onExecute([&](Command& c) {
    gi = c.getArg(hi).getValue();
    gs = c.getArg(hs).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("cmd --i --s"));
  TEST_ASSERT_EQUAL(9, gi);
  TEST_ASSERT_EQUAL_STRING("def", gs);
}

static void test_named_arg_expects_value_error() {
  AdvancedCLI cli;
  bool called = false;
  auto& cmd   = cli.addCommand("cmd");
  cmd.addArg("x"); // no default
  cmd.onExecute([&](Command&) { called = true; });

  // A named arg with no value and no default must fail the parse AND not execute.
  TEST_ASSERT_FALSE(cli.inject("cmd --x"));
  TEST_ASSERT_FALSE(called);
}

static void test_unexpected_positional_does_not_execute() {
  // A surplus positional must fail the parse AND prevent the command callback from executing.
  AdvancedCLI cli;
  bool called  = false;
  Command& cmd = cli.addCommand("copy");
  cmd.addPosArg("src");
  cmd.addPosArg("dst");
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_FALSE(cli.inject("copy a b c")); // 'c' has no positional slot
  TEST_ASSERT_FALSE(called);
}

static void test_type_check_errors_main_loop() {
  // Int and float args given non-numeric values trigger the type-check error path.
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("cmd");
  cmd.addIntArg("i");
  cmd.addFloatArg("f");
  cmd.onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("cmd --i abc --f xyz"));
}

static void test_persistent_type_check_errors() {
  // Persistent int/float args with invalid values trigger the persistent type-check error path.
  AdvancedCLI cli;
  auto& joy = cli.addCommand("joy");
  joy.addPersistentIntArg("n");
  joy.addPersistentFloatArg("r");
  joy.addSubCommand("cal").onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("joy -n abc -r xyz cal"));
}

static void test_persistent_named_defaults_when_next_is_flag() {
  // A persistent named arg immediately followed by a flag falls back to its default.
  AdvancedCLI cli;
  ArgStr h_a;
  const char* got_a = nullptr;
  bool cal_called   = false;

  Command& joy = cli.addCommand("joy");
  h_a          = joy.addPersistentArg("a", "fallback");
  joy.addPersistentFlag("b");
  joy.addSubCommand("cal").onExecute([&](Command& c) {
    cal_called = true;
    got_a      = c.getArg(h_a).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("joy -a -b cal"));
  TEST_ASSERT_TRUE(cal_called);
  TEST_ASSERT_EQUAL_STRING("fallback", got_a);
}

static void test_persistent_named_missing_value_errors() {
  // A persistent named arg with no value and no default reports an error.
  AdvancedCLI cli;
  bool cal_called = false;
  Command& joy    = cli.addCommand("joy");
  joy.addPersistentArg("a"); // no default
  joy.addPersistentFlag("b");
  joy.addSubCommand("cal").onExecute([&](Command&) { cal_called = true; });

  // The persistent-arg error must fail the parse AND prevent the sub-command from executing.
  TEST_ASSERT_FALSE(cli.inject("joy -a -b cal"));
  TEST_ASSERT_FALSE(cal_called);
}

static void test_persistent_unknown_flag_skipped() {
  // An unknown flag in the persistent-arg region is skipped gracefully.
  AdvancedCLI cli;
  ArgStr h_a;
  const char* got_a = nullptr;
  bool cal_called   = false;

  Command& joy = cli.addCommand("joy");
  h_a          = joy.addPersistentArg("a", "fallback");
  joy.addSubCommand("cal").onExecute([&](Command& c) {
    cal_called = true;
    got_a      = c.getArg(h_a).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("joy -a -z cal"));
  TEST_ASSERT_TRUE(cal_called);
  TEST_ASSERT_EQUAL_STRING("fallback", got_a);
}

static void test_addCommand_null_name() {
  AdvancedCLI cli;
  cli.addCommand(nullptr);
  TEST_ASSERT_FALSE(cli.isValid());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                   Tokenizer escape sequences                                   */
/* ---------------------------------------------------------------------------------------------- */

static void test_tokenizer_escape_sequences() {
  AdvancedCLI cli;
  ArgStr h;
  const char* got = nullptr;

  auto& cmd = cli.addCommand("cmd");
  h         = cmd.addArg("v");
  cmd.onExecute([&](Command& c) { got = c.getArg(h).getValue(); });

  // Escapes inside a quoted token: \" \' \\ \n \t and an unrecognised \z (passes char through).
  TEST_ASSERT_TRUE(cli.inject(R"(cmd --v "x\"\'\\\n\t\zy")"));
  TEST_ASSERT_EQUAL_STRING("x\"'\\\n\tzy", got);

  // A backslash as the very last character (no room for an escape) is taken literally.
  TEST_ASSERT_TRUE(cli.inject("cmd --v \"ab\\"));
  TEST_ASSERT_EQUAL_STRING("ab\\", got);
}

/* ---------------------------------------------------------------------------------------------- */
/*                              inject() buffer overload edge cases                               */
/* ---------------------------------------------------------------------------------------------- */

static void test_inject_null_buffer_delegates() {
  AdvancedCLI cli;
  bool called = false;
  cli.addCommand("cmd").onExecute([&](Command&) { called = true; });

  TEST_ASSERT_TRUE(cli.inject("cmd", nullptr, 10)); // null buffer -> delegates
  TEST_ASSERT_TRUE(called);

  char buf[8];
  TEST_ASSERT_TRUE(cli.inject("cmd", buf, 0)); // zero size -> delegates
}

static void test_inject_small_buffer_truncates() {
  AdvancedCLI cli;
  cli.addCommand("help").onExecute([&](Command&) { cli.printHelp(); });

  char small[6] = {};
  cli.inject("help", small, sizeof(small)); // forces the buffer-full / truncation paths
  TEST_ASSERT_TRUE(strlen(small) <= sizeof(small) - 1);
}

/* ---------------------------------------------------------------------------------------------- */
/*                          printHelp(cmd_name) with sub-commands / args                          */
/* ---------------------------------------------------------------------------------------------- */

static void test_printHelp_by_name_with_subcommands() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& wifi = cli.addCommand("wifi").setDescription("WiFi");
  wifi.addSubCommand("scan").setDescription("Scan nets");

  cli.printHelp("wifi", 3);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "scan"));
}

static void test_printHelp_renders_all_arg_features() {
  // A single help dump that exercises alias rendering, every type tag, descriptions and all
  // default-value kinds in _printCommandEntry.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  auto& cmd = cli.addCommand("config").setDescription("configure");
  cmd.addArg("name", "guest").setAlias("n").setDescription("user name");
  cmd.addFlag("verbose").setAlias("v");
  cmd.addPosArg("target");
  cmd.addIntArg("count", 5);
  cmd.addFloatArg("ratio", 1.5f);
  cmd.addArg("empty", ""); // empty string default -> default not rendered

  cli.printHelp(3);

  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "(-n)"));      // alias rendered
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "[flag ]"));   // flag type tag
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "[pos  ]"));   // positional type tag
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "user name")); // arg description
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "default: guest"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "default: 5"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "default: 1.5"));
}

/* ---------------------------------------------------------------------------------------------- */
/*                              Defensive / edge-case branch coverage                             */
/* ---------------------------------------------------------------------------------------------- */

static void test_parse_input_guards() {
  AdvancedCLI cli;
  cli.addCommand("cmd").onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.parse(nullptr));          // null input
  TEST_ASSERT_FALSE(cli.parse(nullptr, 5));       // null input with length
  TEST_ASSERT_FALSE(cli.parse("cmd", (size_t)0)); // zero length
  TEST_ASSERT_TRUE(cli.parse("   "));             // whitespace only -> no tokens, not an error

  // Input longer than MAX_INPUT_LEN is capped (not a crash); unknown command -> false
  char big[Config::MAX_INPUT_LEN + 64];
  memset(big, 'a', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  TEST_ASSERT_FALSE(cli.parse(big));
}

static void test_invalid_handle_builder_methods_safe() {
  // Builder methods on default (invalid) handles must be safe no-ops.
  ArgStr s;
  ArgFlag f;
  ArgInt i;
  ArgFloat fl;
  auto noop = [](const char*, const char*, const char*) {};

  s.setAlias("x").setDescription("d").setRequired().onInvalid(noop);
  f.setAlias("x").setDescription("d").setRequired().onInvalid(noop);
  i.setAlias("x").setDescription("d").setRequired().onInvalid(noop);
  fl.setAlias("x").setDescription("d").setRequired().onInvalid(noop);

  TEST_ASSERT_FALSE(s.isValid());
  TEST_ASSERT_FALSE(s.isSet()); // ArgBaseImpl::isSet with null _parsed
  TEST_ASSERT_FALSE(static_cast<bool>(f));
}

static void test_setAlias_edge_cases() {
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("c");
  ArgStr h  = cmd.addArg("a");

  h.setAlias(nullptr); // null alias name branch

  // Exceed MAX_ALIASES; extra aliases are ignored.
  char names[Config::MAX_ALIASES + 1][4] = {};
  for (uint8_t k = 0; k <= Config::MAX_ALIASES; ++k) {
    names[k][0] = 'a';
    names[k][1] = static_cast<char>('0' + k);
    names[k][2] = '\0';
    h.setAlias(names[k]);
  }
  TEST_ASSERT_TRUE(h.isValid());
}

static void test_setValidator_null_and_invalid_handle() {
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("c");
  cmd.addArg("s").setValidator(nullptr);      // ArgStr: null fn
  cmd.addIntArg("i").setValidator(nullptr);   // ArgInt: null fn
  cmd.addFloatArg("f").setValidator(nullptr); // ArgFloat: null fn

  ArgStr().setValidator([](const char*) { return true; }); // invalid handle
  ArgInt().setValidator([](int32_t) { return true; });
  ArgFloat().setValidator([](float) { return true; });

  TEST_ASSERT_TRUE(cmd.isValid());
}

static void test_reader_invalid_handle() {
  ParsedStr ps;
  ParsedInt pi;
  ParsedFloat pf;

  TEST_ASSERT_FALSE(ps.isValid());
  TEST_ASSERT_FALSE(ps.isSet());
  TEST_ASSERT_NULL(ps.getName());        // _def null -> nullptr
  TEST_ASSERT_NULL(ps.getDescription()); // _def null -> nullptr
  TEST_ASSERT_FALSE(static_cast<bool>(ps));
  TEST_ASSERT_EQUAL(7, pi.getValue(7));                      // empty value -> default
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, pf.getValue(2.5f)); // empty value -> default
}

static void test_type_check_trailing_chars() {
  // Values that start numeric but contain trailing junk fail the type check.
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("cmd");
  cmd.addIntArg("i");
  cmd.addFloatArg("f");
  cmd.onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("cmd --i 12abc"));
  TEST_ASSERT_FALSE(cli.inject("cmd --f 1.2.3"));
}

static void test_persistent_int_defaults_when_next_is_flag() {
  // Persistent typed (int) arg using its default -> the null-token default branch.
  AdvancedCLI cli;
  ArgInt h_n;
  int32_t got_n   = -1;
  bool cal_called = false;

  Command& joy = cli.addCommand("joy");
  h_n          = joy.addPersistentIntArg("n", 5);
  joy.addPersistentFlag("b");
  joy.addSubCommand("cal").onExecute([&](Command& c) {
    cal_called = true;
    got_n      = c.getArg(h_n).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("joy -n -b cal"));
  TEST_ASSERT_TRUE(cal_called);
  TEST_ASSERT_EQUAL(5, got_n); // default used
}

static void test_persistent_arg_as_last_token() {
  // Persistent named arg as the final token (no following value token in the scan).
  AdvancedCLI cli;
  ArgInt h_n;
  int32_t got_n = -1;

  Command& joy = cli.addCommand("joy");
  h_n          = joy.addPersistentIntArg("n", 8);
  joy.addSubCommand("cal").onExecute([](Command&) {});
  joy.onExecute([&](Command& c) { got_n = c.getArg(h_n).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("joy -n"));
  TEST_ASSERT_EQUAL(8, got_n); // default used, parent executed standalone
}

static void test_parent_nonpersistent_arg_skipped_in_validation() {
  // A parent's non-persistent arg is skipped during the sub-command persistent-arg validation.
  AdvancedCLI cli;
  ArgInt h_n;
  bool cal_called = false;

  Command& joy = cli.addCommand("joy");
  joy.addArg("plain"); // non-persistent parent arg
  h_n = joy.addPersistentIntArg("n");
  joy.addSubCommand("cal").onExecute([&](Command&) { cal_called = true; });

  TEST_ASSERT_TRUE(cli.inject("joy -n 2 cal"));
  TEST_ASSERT_TRUE(cal_called);
}

static void test_subcommand_usage_includes_parent_persistent() {
  // A failing sub-command builds a usage string that interleaves the parent's persistent args.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& joy = cli.addCommand("joy");
  joy.addPersistentIntArg("n").setRequired(); // required persistent named
  joy.addPersistentFlag("v");                 // persistent flag
  Command& cal = joy.addSubCommand("cal");
  cal.addArg("x").setRequired(); // required sub-command arg (forces the error + usage)
  cal.onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("joy -n 1 cal")); // -x missing -> error with usage
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "Usage"));
}

static void test_did_you_mean_mixed_case_skips_subcommands() {
  // Uppercase unknown token, candidate registered with uppercase, and a sub-command present that
  // the suggestion loop must skip.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& menu = cli.addCommand("menu");
  menu.addSubCommand("sub");
  cli.addCommand("Ping").onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("PI"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "Did you mean"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "Ping"));
}

static void test_printHelp_by_name_skips_nonmatching() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  cli.addCommand("other").setDescription("Other");
  Command& wifi = cli.addCommand("wifi").setDescription("WiFi");
  wifi.addSubCommand("scan").setDescription("Scan");

  cli.printHelp("wifi");
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NULL(strstr(cap.buf, "Other"));
}

static void test_tokenizer_edge_cases() {
  AdvancedCLI cli;
  ArgStr h;
  const char* got = nullptr;
  auto& cmd       = cli.addCommand("cmd");
  h               = cmd.addPosArg("v");
  cmd.onExecute([&](Command& c) { got = c.getArg(h).getValue(); });

  // Trailing whitespace after the last token.
  TEST_ASSERT_TRUE(cli.inject("cmd hello   "));
  TEST_ASSERT_EQUAL_STRING("hello", got);

  // An empty quoted token is still produced as a (empty) positional value.
  TEST_ASSERT_TRUE(cli.inject("cmd \"\""));
  TEST_ASSERT_EQUAL_STRING("", got);

  // A token longer than MAX_TOKEN_LEN is truncated, not overflowed.
  char longtok[Config::MAX_TOKEN_LEN + 16];
  const char prefix[] = "cmd ";
  size_t plen         = strlen(prefix);
  memcpy(longtok, prefix, plen);
  memset(longtok + plen, 'z', sizeof(longtok) - plen - 1);
  longtok[sizeof(longtok) - 1] = '\0';
  TEST_ASSERT_TRUE(cli.inject(longtok));
  TEST_ASSERT_TRUE(strlen(got) <= Config::MAX_TOKEN_LEN - 1);
}

static void test_help_long_description_and_multi_alias() {
  // Multiple aliases on a printed arg and an over-long line exercise the alias loop and the
  // write-position clamp.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  static const char long_desc[] =
    "this is a very long argument description that exceeds the line buffer used by the help "
    "renderer so that the write position clamp is exercised at least once here";

  auto& cmd = cli.addCommand("c");
  cmd.addArg("name").setAlias("n").setAlias("nm").setDescription(long_desc);

  cli.printHelp(3);
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "-n"));
}

static void test_fail_empty_message_no_output() {
  // cmd.fail("") with no onError handler and a sink: the empty message is not printed.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  cli.addCommand("boom").onExecute([](Command& c) { c.fail(""); });

  TEST_ASSERT_FALSE(cli.inject("boom"));
}

/* ---------------------------------------------------------------------------------------------- */
/*                       Command-level defensive / overflow branch coverage                      */
/* ---------------------------------------------------------------------------------------------- */

static void test_add_arg_methods_on_full_pool_return_invalid() {
  // With the argument pool exhausted, every add*Arg variant must return an invalid handle.
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("fill");

  static char names[Config::MAX_ARGS_TOTAL][4] = {};
  for (uint16_t k = 0; k < Config::MAX_ARGS_TOTAL; ++k) {
    names[k][0] = 'a';
    names[k][1] = static_cast<char>('0' + (k % 10));
    names[k][2] = static_cast<char>('0' + (k / 10));
    names[k][3] = '\0';
    cmd.addIntArg(names[k]);
  }

  TEST_ASSERT_FALSE(cmd.addArg("x").isValid());
  TEST_ASSERT_FALSE(cmd.addFlag("x").isValid());
  TEST_ASSERT_FALSE(cmd.addIntArg("x").isValid());
  TEST_ASSERT_FALSE(cmd.addIntArg("x", 1).isValid());
  TEST_ASSERT_FALSE(cmd.addFloatArg("x").isValid());
  TEST_ASSERT_FALSE(cmd.addFloatArg("x", 1.f).isValid());
  TEST_ASSERT_FALSE(cmd.addPosArg("x").isValid());
  TEST_ASSERT_FALSE(cmd.addPosIntArg("x").isValid());
  TEST_ASSERT_FALSE(cmd.addPosFloatArg("x").isValid());
  TEST_ASSERT_FALSE(cmd.addPersistentArg("x").isValid());
  TEST_ASSERT_FALSE(cmd.addPersistentFlag("x").isValid());
  TEST_ASSERT_FALSE(cmd.addPersistentIntArg("x").isValid());
  TEST_ASSERT_FALSE(cmd.addPersistentIntArg("x", 1).isValid());
  TEST_ASSERT_FALSE(cmd.addPersistentFloatArg("x").isValid());
  TEST_ASSERT_FALSE(cmd.addPersistentFloatArg("x", 1.f).isValid());
}

static void test_dummy_command_methods_safe() {
  // Methods on a default-constructed (never registered) Command must be safe no-ops.
  Command dummy;
  TEST_ASSERT_FALSE(dummy.getArgByName("x").isValid());
  TEST_ASSERT_FALSE(dummy.addArg("x").isValid()); // _addArgInternal: !_owner
  TEST_ASSERT_FALSE(dummy.addSubCommand("s").isValid());
  TEST_ASSERT_EQUAL_STRING("", dummy.getName());
  TEST_ASSERT_EQUAL_STRING("", dummy.getDescription());
  TEST_ASSERT_EQUAL(0, dummy.getParsedArgCount());
  dummy.fail("boom"); // _owner null -> no-op
  dummy.printHelp();  // _owner null -> no-op
}

static void test_getArgByName_edge_cases() {
  AdvancedCLI cli;
  bool null_invalid = false;
  bool miss_invalid = false;

  auto& cmd = cli.addCommand("c");
  cmd.addArg("a");
  cmd.onExecute([&](Command& c) {
    null_invalid = !c.getArgByName(nullptr).isValid();
    miss_invalid = !c.getArgByName("zzz").isValid();
  });

  TEST_ASSERT_TRUE(cli.inject("c"));
  TEST_ASSERT_TRUE(null_invalid);
  TEST_ASSERT_TRUE(miss_invalid);
}

static void test_getArgByName_parent_fallback_skips_nonpersistent() {
  AdvancedCLI cli;
  bool miss = false;

  Command& joy = cli.addCommand("joy");
  joy.addArg("plain");          // non-persistent parent arg -> skipped during fallback
  joy.addPersistentIntArg("n"); // persistent
  joy.addSubCommand("cal").onExecute([&](Command& c) { miss = !c.getArgByName("nope").isValid(); });

  TEST_ASSERT_TRUE(cli.inject("joy -n 1 cal"));
  TEST_ASSERT_TRUE(miss);
}

static void test_command_no_callback_executes_safely() {
  AdvancedCLI cli;
  cli.addCommand("c"); // no onExecute
  TEST_ASSERT_TRUE(cli.inject("c"));
}

static void test_addArg_null_name() {
  AdvancedCLI cli;
  auto& cmd = cli.addCommand("c");
  TEST_ASSERT_FALSE(cmd.addArg(nullptr).isValid());
}

static void test_find_named_skips_positional() {
  AdvancedCLI cli;
  ArgStr hp;
  ArgInt hn;
  const char* gp = nullptr;
  int32_t gn     = 0;

  auto& cmd = cli.addCommand("c");
  hp        = cmd.addPosArg("pos");
  hn        = cmd.addIntArg("n");
  cmd.onExecute([&](Command& c) {
    gp = c.getArg(hp).getValue();
    gn = c.getArg(hn).getValue();
  });

  TEST_ASSERT_TRUE(cli.inject("c here -n 5"));
  TEST_ASSERT_EQUAL_STRING("here", gp);
  TEST_ASSERT_EQUAL(5, gn);
}

static void test_arg_registration_contiguity_guard() {
  // Registering an arg on an earlier command after a later command exists is rejected.
  AdvancedCLI cli;
  auto& a = cli.addCommand("a");
  a.addArg("x");
  auto& b = cli.addCommand("b");
  b.addArg("y");
  TEST_ASSERT_FALSE(a.addArg("z").isValid());
}

static void test_fail_null_message() {
  AdvancedCLI cli;
  cli.addCommand("boom").onExecute([](Command& c) { c.fail(nullptr); });
  TEST_ASSERT_FALSE(cli.inject("boom"));
}

static void test_setAlias_overflow_all_types() {
  // Alias overflow on flag/int/float handles (per-type template instantiation).
  AdvancedCLI cli;
  auto& cmd   = cli.addCommand("c");
  ArgFlag f   = cmd.addFlag("f");
  ArgInt i    = cmd.addIntArg("i");
  ArgFloat fl = cmd.addFloatArg("fl");

  static char nm[Config::MAX_ALIASES + 1][6] = {};
  for (uint8_t k = 0; k <= Config::MAX_ALIASES; ++k) {
    nm[k][0] = 'z';
    nm[k][1] = static_cast<char>('0' + k);
    nm[k][2] = '\0';
    f.setAlias(nm[k]);
    i.setAlias(nm[k]);
    fl.setAlias(nm[k]);
  }
  TEST_ASSERT_TRUE(f.isValid());
  TEST_ASSERT_TRUE(i.isValid());
  TEST_ASSERT_TRUE(fl.isValid());
}

static void test_printHelp_by_name_edge_cases() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& wifi = cli.addCommand("wifi");
  wifi.addSubCommand("scan");

  cli.printHelp(nullptr);       // null name -> no-op
  cli.printHelp("nonexistent"); // not found -> no-op
  TEST_ASSERT_EQUAL(0, cap.len);

  cli.printHelp("wifi", 1); // depth 1 -> sub-commands hidden
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "wifi"));
  TEST_ASSERT_NULL(strstr(cap.buf, "scan"));
}

static void test_builder_handle_isSet_false_when_absent() {
  AdvancedCLI cli;
  ArgFlag h;
  bool was_set = true;

  auto& cmd = cli.addCommand("c");
  h         = cmd.addFlag("f");
  cmd.onExecute([&](Command&) { was_set = h.isSet(); });

  TEST_ASSERT_TRUE(cli.inject("c")); // flag absent
  TEST_ASSERT_FALSE(was_set);
}

static void test_usage_string_all_arg_types() {
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  auto& cmd = cli.addCommand("c");
  cmd.addFlag("ff");
  cmd.addArg("nn");
  cmd.addPosArg("pp").setRequired();
  cmd.onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("c")); // required positional missing -> usage with all arg types
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "Usage"));
}

static void test_dash_only_token_not_an_arg() {
  // A bare "-" token strips to an empty arg name, so it matches no argument and is reported as
  // unknown; the command callback must not run.
  AdvancedCLI cli;
  bool called = false;

  auto& cmd = cli.addCommand("dev");
  cmd.addFlag("v"); // a named arg so the lookup actually compares "-" against a definition
  cmd.onExecute([&](Command&) { called = true; });

  TEST_ASSERT_FALSE(cli.inject("dev -"));
  TEST_ASSERT_FALSE(called);
}

static void test_negative_number_positional() {
  // A negative number given as a positional value (not after a named arg) is a value, not a flag.
  // Exercises the is-negative-number arc of the flag detection in both the sub-command scan and
  // the token parser.
  AdvancedCLI cli;
  ArgInt h;
  int32_t got = 0;

  auto& cmd = cli.addCommand("temp");
  h         = cmd.addPosIntArg("delta");
  cmd.onExecute([&](Command& c) { got = c.getArg(h).getValue(); });

  TEST_ASSERT_TRUE(cli.inject("temp -7"));
  TEST_ASSERT_EQUAL(-7, got);
}

static void test_tokenizer_token_limit() {
  // Feeding more than MAX_TOKENS whitespace-separated tokens must not overflow the token table:
  // the tokenizer stops at the limit and the CLI stays usable afterwards.
  AdvancedCLI cli;
  cli.addCommand("noop").onExecute([](Command&) {});

  char line[Config::MAX_INPUT_LEN] = "noop";
  size_t pos                       = strlen(line);
  for (uint8_t k = 0; k < Config::MAX_TOKENS + 5; ++k) {
    line[pos++] = ' ';
    line[pos++] = 'a';
  }
  line[pos] = '\0';

  // noop takes no positionals, so the extra tokens are unexpected -> parse fails gracefully.
  TEST_ASSERT_FALSE(cli.inject(line));
  TEST_ASSERT_TRUE(cli.inject("noop")); // still works -> no state corruption
}

static void test_usage_required_flags() {
  // Required flags render unbracketed ("-f") instead of optional ("[-f]") in usage strings, for
  // both a command's own flag and a parent's persistent flag.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  Command& dev = cli.addCommand("dev");
  dev.addPersistentFlag("g").setRequired(); // required persistent flag -> parent branch
  Command& run = dev.addSubCommand("run");
  run.addFlag("f").setRequired(); // required flag -> main branch
  run.addArg("x").setRequired();  // missing -> forces an error so usage is emitted
  run.onExecute([](Command&) {});

  // -g and -f are provided; -x is missing -> error with usage. Both flags appear unbracketed.
  TEST_ASSERT_FALSE(cli.inject("dev -g run -f"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, " -g"));
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, " -f"));
  TEST_ASSERT_NULL(strstr(cap.buf, "[-g]"));
  TEST_ASSERT_NULL(strstr(cap.buf, "[-f]"));
}

static void test_usage_string_buffer_truncates() {
  // A command whose usage string exceeds MAX_INPUT_LEN must truncate safely: the build loop stops
  // at the buffer bound instead of overflowing.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  // 23-char names (~53 chars of usage each) -> six of them overflow the 256-byte usage buffer.
  auto& cmd = cli.addCommand("x");
  cmd.addArg("aaaaaaaaaaaaaaaaaaaaaaa").setRequired(); // required -> forces an error + usage output
  cmd.addArg("bbbbbbbbbbbbbbbbbbbbbbb");
  cmd.addArg("ccccccccccccccccccccccc");
  cmd.addArg("ddddddddddddddddddddddd");
  cmd.addArg("eeeeeeeeeeeeeeeeeeeeeee");
  cmd.addArg("fffffffffffffffffffffff");
  cmd.onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("x")); // required arg missing -> error with (truncated) usage
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "Usage"));
}

static void test_alias_display_buffer_truncates() {
  // An argument with several long aliases overflows the 64-byte alias display buffer; the builder
  // must stop at the bound instead of overflowing.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  auto& cmd = cli.addCommand("c");
  cmd.addArg("n")
    .setAlias("aaaaaaaaaaaaaaaaaaaaaaa") // 23 chars each; MAX_ALIASES (4) of these overflow
    .setAlias("bbbbbbbbbbbbbbbbbbbbbbb")
    .setAlias("ccccccccccccccccccccccc")
    .setAlias("ddddddddddddddddddddddd");
  cmd.onExecute([](Command&) {});

  cli.printHelp(3); // renders arg lines incl. the alias display -> exercises the truncation
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "-n"));
}

static void test_subcommand_usage_buffer_truncates() {
  // A parent with many long-named persistent args builds a sub-command usage string that exceeds
  // MAX_INPUT_LEN; write_pos is clamped so the final append cannot index/size out of bounds.
  AdvancedCLI cli;
  OutputCapture cap;
  cli.setOutput(cap.fn());

  // 23-char persistent names (~53 chars of usage each) -> overflow the 256-byte usage buffer.
  Command& p = cli.addCommand("p");
  p.addPersistentArg("aaaaaaaaaaaaaaaaaaaaaaa");
  p.addPersistentArg("bbbbbbbbbbbbbbbbbbbbbbb");
  p.addPersistentArg("ccccccccccccccccccccccc");
  p.addPersistentArg("ddddddddddddddddddddddd");
  p.addPersistentArg("eeeeeeeeeeeeeeeeeeeeeee");
  p.addPersistentArg("fffffffffffffffffffffff");
  Command& s = p.addSubCommand("s");
  s.addArg("z").setRequired(); // missing -> error forces the (truncated) usage to be emitted
  s.onExecute([](Command&) {});

  TEST_ASSERT_FALSE(cli.inject("p s")); // -z missing -> error + truncated usage, no overflow
  TEST_ASSERT_NOT_NULL(strstr(cap.buf, "Usage"));
}

/* ---------------------------------------------------------------------------------------------- */
/*                                             Runners                                            */
/* ---------------------------------------------------------------------------------------------- */

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

int runUnityTests(void) {
  UNITY_BEGIN();

  // Basic dispatch
  RUN_TEST(test_basic_dispatch);
  RUN_TEST(test_unknown_command_returns_false);
  RUN_TEST(test_getCommandCount);

  // getArgCount()
  RUN_TEST(test_getArgCount_zero_with_no_commands);
  RUN_TEST(test_getArgCount_increments_per_command);
  RUN_TEST(test_getArgCount_includes_subcommands);

  // isValid()
  RUN_TEST(test_isValid_true_on_empty_cli);
  RUN_TEST(test_isValid_true_after_normal_registration);
  RUN_TEST(test_isValid_false_on_command_overflow);
  RUN_TEST(test_isValid_false_on_args_overflow);

  // getAttemptedCommandCount() / getAttemptedArgCount()
  RUN_TEST(test_getAttemptedCommandCount_no_overflow);
  RUN_TEST(test_getAttemptedCommandCount_overflow);
  RUN_TEST(test_getAttemptedCommandCount_subcommand_of_overflow);
  RUN_TEST(test_getAttemptedArgCount_no_overflow);
  RUN_TEST(test_getAttemptedArgCount_overflow);
  RUN_TEST(test_getAttemptedArgCount_arg_on_overflow_command);

  // Named string arg
  RUN_TEST(test_named_string_arg_provided);
  RUN_TEST(test_named_string_arg_default);
  RUN_TEST(test_named_string_arg_no_default_returns_empty);
  RUN_TEST(test_named_string_arg_json_with_quotes);
  RUN_TEST(test_named_string_arg_json_without_quotes);

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

  // Persistent arguments
  RUN_TEST(test_persistent_arg_before_subcommand);
  RUN_TEST(test_persistent_arg_default_used_when_absent);
  RUN_TEST(test_persistent_arg_readable_via_getArgByName_in_sub);
  RUN_TEST(test_persistent_flag_before_subcommand);
  RUN_TEST(test_persistent_flag_absent_is_false);
  RUN_TEST(test_persistent_required_arg_missing_fails);
  RUN_TEST(test_persistent_required_arg_provided_succeeds);
  RUN_TEST(test_persistent_arg_multiple_subcommands_share_it);
  RUN_TEST(test_persistent_arg_parent_standalone_still_works);
  RUN_TEST(test_persistent_arg_argCount_includes_persistent);
  RUN_TEST(test_persistent_arg_registered_after_subcommand_fails);

  // printHelp(const Command&) and Command::printHelp()
  RUN_TEST(test_printHelp_by_ref_prints_specific_command);
  RUN_TEST(test_printHelp_by_ref_disambiguates_same_name_subcommands);
  RUN_TEST(test_printHelp_by_ref_excludes_sibling_commands);
  RUN_TEST(test_printHelp_by_ref_depth_control);
  RUN_TEST(test_command_printHelp_prints_own_command);
  RUN_TEST(test_command_printHelp_inside_callback);
  RUN_TEST(test_command_printHelp_disambiguates_from_callback);
  RUN_TEST(test_command_printHelp_depth_control);

  // Builder-handle query methods + all-type builder coverage
  RUN_TEST(test_builder_handle_query_methods);
  RUN_TEST(test_builder_methods_all_types);
  RUN_TEST(test_argbase_direct_instantiation);

  // Reader query methods + getArg edge cases
  RUN_TEST(test_reader_query_methods);
  RUN_TEST(test_getArg_foreign_handle_returns_invalid);

  // Positional typed args / persistent string + float
  RUN_TEST(test_addPosIntArg_and_addPosFloatArg);
  RUN_TEST(test_persistent_string_arg_read_from_sub);
  RUN_TEST(test_persistent_string_arg_default_in_sub);
  RUN_TEST(test_persistent_float_arg_variants_read_from_sub);
  RUN_TEST(test_command_isValid);

  // Parse error / default paths
  RUN_TEST(test_did_you_mean_suggestion);
  RUN_TEST(test_unknown_argument_main_loop_fails);
  RUN_TEST(test_named_arg_uses_default_when_value_absent);
  RUN_TEST(test_named_arg_expects_value_error);
  RUN_TEST(test_unexpected_positional_does_not_execute);
  RUN_TEST(test_type_check_errors_main_loop);
  RUN_TEST(test_persistent_type_check_errors);
  RUN_TEST(test_persistent_named_defaults_when_next_is_flag);
  RUN_TEST(test_persistent_named_missing_value_errors);
  RUN_TEST(test_persistent_unknown_flag_skipped);
  RUN_TEST(test_addCommand_null_name);

  // Tokenizer escapes + inject buffer edge cases
  RUN_TEST(test_tokenizer_escape_sequences);
  RUN_TEST(test_inject_null_buffer_delegates);
  RUN_TEST(test_inject_small_buffer_truncates);

  // printHelp rendering details
  RUN_TEST(test_printHelp_by_name_with_subcommands);
  RUN_TEST(test_printHelp_renders_all_arg_features);

  // Defensive / edge-case branch coverage
  RUN_TEST(test_parse_input_guards);
  RUN_TEST(test_invalid_handle_builder_methods_safe);
  RUN_TEST(test_setAlias_edge_cases);
  RUN_TEST(test_setValidator_null_and_invalid_handle);
  RUN_TEST(test_reader_invalid_handle);
  RUN_TEST(test_type_check_trailing_chars);
  RUN_TEST(test_persistent_int_defaults_when_next_is_flag);
  RUN_TEST(test_persistent_arg_as_last_token);
  RUN_TEST(test_parent_nonpersistent_arg_skipped_in_validation);
  RUN_TEST(test_subcommand_usage_includes_parent_persistent);
  RUN_TEST(test_did_you_mean_mixed_case_skips_subcommands);
  RUN_TEST(test_printHelp_by_name_skips_nonmatching);
  RUN_TEST(test_tokenizer_edge_cases);
  RUN_TEST(test_help_long_description_and_multi_alias);
  RUN_TEST(test_fail_empty_message_no_output);

  // Command-level defensive / overflow branch coverage
  RUN_TEST(test_add_arg_methods_on_full_pool_return_invalid);
  RUN_TEST(test_dummy_command_methods_safe);
  RUN_TEST(test_getArgByName_edge_cases);
  RUN_TEST(test_getArgByName_parent_fallback_skips_nonpersistent);
  RUN_TEST(test_command_no_callback_executes_safely);
  RUN_TEST(test_addArg_null_name);
  RUN_TEST(test_find_named_skips_positional);
  RUN_TEST(test_arg_registration_contiguity_guard);
  RUN_TEST(test_fail_null_message);
  RUN_TEST(test_setAlias_overflow_all_types);
  RUN_TEST(test_printHelp_by_name_edge_cases);
  RUN_TEST(test_builder_handle_isSet_false_when_absent);
  RUN_TEST(test_usage_string_all_arg_types);
  RUN_TEST(test_dash_only_token_not_an_arg);
  RUN_TEST(test_negative_number_positional);
  RUN_TEST(test_tokenizer_token_limit);
  RUN_TEST(test_usage_required_flags);
  RUN_TEST(test_usage_string_buffer_truncates);
  RUN_TEST(test_alias_display_buffer_truncates);
  RUN_TEST(test_subcommand_usage_buffer_truncates);

  return UNITY_END();
}

// For native
int main(void) { return runUnityTests(); }

// For Arduino framework
#ifdef ARDUINO
void setup() {
  delay(2000);
  runUnityTests();
}
void loop() {}
#endif
