/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * RequiredArg - Demonstrates: setRequired, parse error on missing argument
 *
 * Available commands:
 *   echo -msg <text>                   - Prints the message (-msg is required)
 *   stamp -msg <text> -prefix <label>  - Prints "[LABEL] message" (-prefix defaults to "INFO")
 *   help                               - Prints the full help listing
 *   help <command>                     - Prints help for a single command (positional)
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgStr echo_msg;
static ArgStr stamp_msg;
static ArgStr stamp_prefix;
static ArgStr help_target;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("---------------------------------");
  Serial.println("AdvancedCLI - RequiredArg Example");
  Serial.println("---------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Configure the "echo" command - -msg is required; omitting it triggers an error
  Command& echo_cmd = cli.addCommand("echo");
  echo_cmd.setDescription("Prints a required message.");
  echo_msg = echo_cmd.addArg("msg").setDescription("Text to print.").setRequired();

  echo_cmd.onExecute([](Command& cmd) {
    ParsedStr msg = cmd.getArg(echo_msg);
    Serial.println(msg.getValue());
  });

  // Configure the "stamp" command - -msg is required, -prefix has a default value of "INFO"
  Command& stamp_cmd = cli.addCommand("stamp");
  stamp_cmd.setDescription("Prints a prefixed message.");

  stamp_msg    = stamp_cmd.addArg("msg").setDescription("Text to print.").setRequired();
  stamp_prefix = stamp_cmd.addArg("prefix", "INFO").setDescription("Log prefix.");

  stamp_cmd.onExecute([](Command& cmd) {
    ParsedStr msg    = cmd.getArg(stamp_msg);
    ParsedStr prefix = cmd.getArg(stamp_prefix);
    Serial.print('[');
    Serial.print(prefix.getValue());
    Serial.print("] ");
    Serial.println(msg.getValue());
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
  cli.parse(buf);
}
