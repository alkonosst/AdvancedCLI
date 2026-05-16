/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * FullConfig - Demonstrates: setCaseSensitive, onUnknownCommand, parse(buf, len), getCommandCount
 *
 * With case sensitivity enabled, "Version" will NOT match "version".
 * onUnknownCommand intercepts unrecognized commands before the default error output.
 * parse(buf, len) is used instead of parse(buf) - no null terminator required.
 *
 * Available commands:
 *   version  - Prints the firmware version
 *   reset    - Resets all settings
 *   diag     - Runs a diagnostic check
 *   help     - Prints the full help listing
 *   help <command> - Prints help for a single command (positional)
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgStr help_target;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("--------------------------------");
  Serial.println("AdvancedCLI - FullConfig Example");
  Serial.println("--------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Case-sensitive: "Version" will NOT match "version"
  cli.setCaseSensitive(true);

  // Custom handler for unrecognized commands; replaces the default "[CLI] Unknown command" output
  cli.onUnknownCommand([](const char* name) {
    Serial.print("[Shell] Unknown: \"");
    Serial.print(name);
    Serial.print("\"  (");
    Serial.print(cli.getCommandCount());
    Serial.println(" commands available - type \"diag\" for info)");
  });

  cli.addCommand("version").setDescription("Prints firmware version.").onExecute([](Command&) {
    Serial.println("v1.0.0");
  });

  cli.addCommand("reset").setDescription("Resets all settings.").onExecute([](Command&) {
    Serial.println("Settings reset.");
  });

  cli.addCommand("diag").setDescription("Runs a diagnostic check.").onExecute([](Command&) {
    Serial.print("Commands registered: ");
    Serial.println(cli.getCommandCount());
    Serial.println("All systems nominal.");
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

  Serial.print("CLI ready. ");
  Serial.print(cli.getCommandCount());
  Serial.println(" commands registered (case-sensitive).");
  Serial.println("> Example ready! Use the provided commands to see them in action.");
}

void loop() {
  if (!Serial.available()) return;

  // Static buffer for incoming command lines; adjust size as needed
  // (see ACLI::Config::MAX_INPUT_LEN)
  static char buf[Config::MAX_INPUT_LEN];

  // parse(buf, len) does not require a null terminator - useful when reading from binary streams
  // or when readBytesUntil already knows the length.
  size_t len = Serial.readBytesUntil('\r', buf, sizeof(buf) - 1);
  buf[len]   = '\0';

  // Flush any remaining newline character from the input buffer
  while (Serial.available()) {
    Serial.read();
  }

  // Print the received command line for clarity
  Serial.print("> ");
  Serial.println(buf);

  // Parse using the explicit length overload (no null terminator required).
  cli.parse(buf, len);
}
