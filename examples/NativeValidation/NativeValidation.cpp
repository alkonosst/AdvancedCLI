/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * NativeValidation - Validation and error handling.
 *
 * Demonstrates: setRequired, setValidator (int / float / string), onInvalid (per-argument
 * override), onError (command-level handler), Command::fail(), the built-in type check, and
 * onUnknownCommand. The script runs both valid and invalid inputs so every error path is visible.
 *
 * Error routing priority, highest first:
 * 1. Per-argument onInvalid() (when a specific argument fails validation/type check)
 * 2. Command-level onError() (everything else: required-arg, type errors, fail())
 * 3. Default output to the sink (when neither is registered)
 *
 * Run-to-completion: main() feeds a fixed script through inject(); no interactive input.
 *
 * Build & run:
 * - PowerShell: $env:EXAMPLE="examples/NativeValidation"; pio run -e native-example -t exec
 * - bash/WSL  : export EXAMPLE="examples/NativeValidation"; pio run -e native-example -t exec
 */

#include <AdvancedCLI.h>

#include <cstdio>

using namespace ACLI;

static AdvancedCLI cli;

static ArgInt set_port;
static ArgFloat set_ratio;
static ArgStr set_name;

static void setupCli() {
  cli.setOutput([](const char* msg) { std::printf("    %s\n", msg); });

  cli.onUnknownCommand(
    [](const char* name) { std::printf("    unknown command: \"%s\"\n", name); });

  // set -port <1..65535> -ratio <0..1> -name <non-empty>
  Command& set = cli.addCommand("set");
  set.setDescription("Set configuration values (with validation).");

  // Required integer with a range validator. It has no onInvalid(), so a rejected value (or a
  // type error, or it being missing) is routed to the command-level onError() below.
  set_port = set.addIntArg("port")
               .setRequired()
               .setDescription("TCP port, 1-65535 (required).")
               .setValidator([](int32_t value) { return value >= 1 && value <= 65535; });

  // Float with a range validator AND a per-argument onInvalid(): its failures are handled here
  // instead of by onError().
  set_ratio = set.addFloatArg("ratio", 1.0f)
                .setDescription("Load ratio, 0.0-1.0 (default 1.0).")
                .setValidator([](float value) { return value >= 0.0f && value <= 1.0f; })
                .onInvalid([](const char* name, const char* value, const char* reason) {
                  std::printf("    onInvalid: -%s rejected \"%s\" (%s)\n",
                    name,
                    value,
                    reason[0] ? reason : "failed validator");
                });

  // String validator: reject an empty name.
  set_name =
    set.addArg("name", "default")
      .setDescription("Non-empty name (default \"default\").")
      .setValidator([](const char* value) { return value != nullptr && value[0] != '\0'; });

  // Command-level handler for any error without a per-argument onInvalid().
  set.onError([](Command&, const char* message) { std::printf("    [set error] %s\n", message); });

  set.onExecute([](Command& cmd) {
    std::printf("    OK: port=%d ratio=%g name=\"%s\"\n",
      cmd.getArg(set_port).getValue(),
      cmd.getArg(set_ratio).getValue(),
      cmd.getArg(set_name).getValue());
  });

  // commit - shows Command::fail(), used to signal a runtime failure from inside the callback.
  Command& commit = cli.addCommand("commit");
  commit.setDescription("Commit staged changes.");
  commit.onError(
    [](Command&, const char* message) { std::printf("    [commit error] %s\n", message); });
  commit.onExecute([](Command& cmd) { cmd.fail("nothing staged to commit"); });
}

// Echo the line, dispatch it, and report success/failure.
static void run(const char* line) {
  std::printf("$ %s\n", line);
  cli.inject(line);
  std::printf("    -> %s\n\n", cli.lastParseOk() ? "success" : "failure");
}

int main() {
  std::puts("---------------------------------------");
  std::puts("AdvancedCLI - Native Validation Example");
  std::puts("---------------------------------------");

  setupCli();

  std::puts("=== Valid input ===");
  run("set -port 8080 -ratio 0.5 -name web");

  std::puts("=== Required argument missing -> onError ===");
  run("set -ratio 0.5");

  std::puts("=== Integer validator rejects out-of-range port -> onError ===");
  run("set -port 70000 -ratio 0.5");

  std::puts("=== Built-in type check rejects non-numeric port -> onError ===");
  run("set -port xyz -ratio 0.5");

  std::puts("=== Float validator rejects ratio -> per-argument onInvalid ===");
  run("set -port 8080 -ratio 2.0");

  std::puts("=== String validator rejects empty name -> onError ===");
  run("set -port 8080 -name \"\"");

  std::puts("=== Command::fail() from inside a callback ===");
  run("commit");

  std::puts("=== Unknown command -> onUnknownCommand ===");
  run("frobnicate --now");

  return 0;
}
