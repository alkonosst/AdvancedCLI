/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * BasicCommand - Demonstrates: addCommand, setDescription, onExecute, setOutput, parse
 *
 * Available commands:
 *   ping             - Replies with "pong"
 *   hello -name Max  - Prints "Hello, <name>!" (-name defaults to "World")
 *   help             - Prints the full help listing
 *   help <command>   - Prints help for a single command (positional)
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgStr name_arg;
static ArgStr help_target;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("----------------------------------");
  Serial.println("AdvancedCLI - BasicCommand Example");
  Serial.println("----------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Configure the "ping" command - no arguments, just replies with "pong"
  cli.addCommand("ping").setDescription("Replies with pong.").onExecute([](Command&) {
    Serial.println("pong");
  });

  // Configure the "hello" command with an optional named string argument
  Command& hello_cmd = cli.addCommand("hello");
  hello_cmd.setDescription("Greets the provided name.");

  name_arg = hello_cmd.addArg("name", "World").setDescription("Name to greet.");
  hello_cmd.onExecute([](Command& cmd) {
    ParsedStr parsed = cmd.getArg(name_arg);
    Serial.print("Hello, ");
    Serial.print(parsed.getValue());
    Serial.println('!');
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
