/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * ErrorHandling - Demonstrates: onError, fail, getArgByName, lastParseOk
 *
 * Available commands:
 *   reboot -delay 0     - Schedules a reboot; fail() is called if delay > 10000
 *                         onError intercepts both parse errors and fail() calls
 *   info -field <name>  - Reads a field by name using getArgByName (no stored handle needed)
 *   help                - Prints the full help listing
 *   help <command>      - Prints help for a single command (positional)
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgInt reboot_delay;
static ArgStr help_target;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("-----------------------------------");
  Serial.println("AdvancedCLI - ErrorHandling Example");
  Serial.println("-----------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Configure the "reboot" command - onError intercepts both parse errors and fail() calls
  Command& reboot_cmd = cli.addCommand("reboot");
  reboot_cmd.setDescription("Schedules a device reboot.");
  reboot_delay =
    reboot_cmd.addIntArg("delay", 0).setDescription("Delay in ms before rebooting (max 10000).");

  reboot_cmd.onError([](Command&, const char* err) {
    Serial.print("[Reboot error] ");
    Serial.println(err);
  });

  reboot_cmd.onExecute([](Command& cmd) {
    ParsedInt delay_arg = cmd.getArg(reboot_delay);
    int32_t delay_ms    = delay_arg.getValue();
    if (delay_ms > 10000) {
      // fail() marks the parse as failed and routes through onError()
      cmd.fail("Delay must be <= 10000 ms.");
      return;
    }
    // Simulate reboot
    Serial.print("Rebooting in ");
    Serial.print(delay_ms);
    Serial.println(" ms...");
  });

  // Configure the "info" command - getArgByName retrieves an argument without a stored handle
  Command& info_cmd = cli.addCommand("info");
  info_cmd.setDescription("Prints device information.");
  info_cmd.addArg("field", "all").setDescription("Field to show: uptime | commands | all.");

  info_cmd.onExecute([](Command& cmd) {
    // getArgByName retrieves any argument by name without a stored handle.
    ParsedAny field_arg = cmd.getArgByName("field");
    const char* field   = field_arg.getValue();

    bool show_all = (field[0] == 'a'); // "all"

    if (show_all || field[0] == 'u') {
      Serial.println("Uptime:   running");
    }

    if (show_all || field[0] == 'c') {
      Serial.print("Commands: ");
      Serial.println(cli.commandCount());
    }
  });

  // Configure the "help" command with an optional positional argument for a specific command
  Command& help_cmd = cli.addCommand("help");
  help_cmd.setDescription("Prints available commands.");
  help_target = help_cmd.addPosArg("command").setDescription("Command name (optional).");

  help_cmd.onExecute([](Command& cmd) {
    ParsedStr target = cmd.getArg(help_target);

    if (target.isSet()) {
      cli.printHelp(target.getValue());
    } else {
      cli.printHelp();
    }
  });

  if (!cli.isValid()) {
    Serial.println("[WARN] CLI registration overflowed: check MAX_COMMANDS and MAX_ARGS_TOTAL.");
  }

  Serial.println("> Example ready! Use the provided commands to see them in action.");
}

void loop() {
  if (!Serial.available()) return;

  // Static buffer for incoming command lines; adjust size as needed
  // (see ACLI::Config::MAX_INPUT_LEN)
  static char buf[Config::MAX_INPUT_LEN];
  size_t len = Serial.readBytesUntil('\r', buf, sizeof(buf) - 1);
  buf[len]   = '\0';

  // Flush any remaining newline character from the input buffer
  while (Serial.available()) {
    Serial.read();
  }

  // Print the received command line for clarity
  Serial.print("> ");
  Serial.println(buf);

  // Parse the input line; this will dispatch to the appropriate command callbacks.
  bool ok = cli.parse(buf);
  if (!ok) Serial.println("[loop] Last parse failed.");
}
