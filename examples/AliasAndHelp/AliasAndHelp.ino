/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * AliasAndHelp - Demonstrates: setAlias, setDescription, printHelp
 *
 * Available commands:
 *   status -verbose  - Reports system status; -verbose (or -v) enables detailed output
 *   help             - Prints the full help listing
 *   help <command>   - Prints help for a single command (positional)
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgFlag status_verbose;
static ArgStr help_target;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("----------------------------------");
  Serial.println("AdvancedCLI - AliasAndHelp Example");
  Serial.println("----------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Configure the "status" command with a verbose flag that has an alias "v"
  Command& status_cmd = cli.addCommand("status");
  status_cmd.setDescription("Reports system status.");
  status_verbose =
    status_cmd.addFlag("verbose").setAlias("v").setDescription("Print detailed output.");

  status_cmd.onExecute([](Command& cmd) {
    Serial.println("Status: OK");

    ParsedFlag verbose = cmd.getArg(status_verbose);
    if (verbose.isSet()) {
      Serial.println("  Uptime:   running");
      Serial.println("  Commands: registered");
      Serial.println("  Errors:   none");
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
  cli.parse(buf);
}
