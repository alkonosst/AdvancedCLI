/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * PositionalArgs - Demonstrates: addPosArg, addPosIntArg, addPosFloatArg, negative values
 *
 * Available commands:
 *   add <a> <b>    - Prints the sum of two integers (e.g. "add 3 -5")
 *   scale <factor> - Multiplies 100.0 by a float factor (e.g. "scale -2.5")
 *   greet <name>   - Greets by positional string (e.g. "greet Arduino")
 *   help           - Prints the full help listing
 *   help <command> - Prints help for a single command (positional)
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgInt add_a;
static ArgInt add_b;
static ArgFloat scale_factor;
static ArgStr greet_name;
static ArgStr help_target;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("------------------------------------");
  Serial.println("AdvancedCLI - PositionalArgs Example");
  Serial.println("------------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Configure the "add" command - two required positional integers; negative values (e.g. -5) work
  Command& add_cmd = cli.addCommand("add");
  add_cmd.setDescription("Adds two integers (positional).");

  add_a = add_cmd.addPosIntArg("a").setDescription("First operand.").setRequired();
  add_b = add_cmd.addPosIntArg("b").setDescription("Second operand.").setRequired();

  add_cmd.onExecute([](Command& cmd) {
    ParsedInt a = cmd.getArg(add_a);
    ParsedInt b = cmd.getArg(add_b);
    Serial.print("Result: ");
    Serial.println(a.getValue() + b.getValue());
  });

  // Configure the "scale" command - one required positional float
  Command& scale_cmd = cli.addCommand("scale");
  scale_cmd.setDescription("Multiplies 100.0 by a float factor (positional).");
  scale_factor = scale_cmd.addPosFloatArg("factor").setDescription("Scale factor.").setRequired();

  scale_cmd.onExecute([](Command& cmd) {
    ParsedFloat factor = cmd.getArg(scale_factor);
    Serial.print("Result: ");
    Serial.println(100.0f * factor.getValue());
  });

  // Configure the "greet" command - one optional positional string with a default
  Command& greet_cmd = cli.addCommand("greet");
  greet_cmd.setDescription("Greets a name (positional string).");
  greet_name = greet_cmd.addPosArg("name", "Stranger").setDescription("Name to greet.");

  greet_cmd.onExecute([](Command& cmd) {
    ParsedStr name = cmd.getArg(greet_name);
    Serial.print("Hey, ");
    Serial.print(name.getValue());
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
