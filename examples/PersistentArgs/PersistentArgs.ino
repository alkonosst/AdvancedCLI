/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * PersistentArgs - Demonstrates persistent arguments shared across sub-commands.
 *
 * A persistent argument is registered on the parent command and may be supplied before the
 * sub-command name. Sub-command callbacks read it via getArg() or getArgByName(), exactly like
 * their own args.
 *
 * Example commands (try in the Serial Monitor):
 *
 *   joy -n 0 cal                          - Calibrate joystick 0
 *   joy -n 1 curve -type expo             - Set expo curve for joystick 1
 *   joy -n 2 filter -cutoff 80            - Set filter cutoff for joystick 2
 *   joy -n 3 filter -cutoff 120 -order 4  - Filter with both args set
 *   joy cal                               - Calibrate (joystick index defaults to 0)
 *   help                                  - Print all commands
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

static AdvancedCLI cli;

// Persistent arg handle (registered on the "joy" parent command)
static ArgInt joy_n;

// Sub-command arg handles
static ArgStr curve_type;
static ArgInt filter_cutoff;
static ArgInt filter_order;

static ArgStr help_target;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("------------------------------------");
  Serial.println("AdvancedCLI - PersistentArgs Example");
  Serial.println("------------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // "joy" parent command
  // -n is a persistent arg: it can be placed before any sub-command name.
  Command& joy = cli.addCommand("joy");
  joy.setDescription("Joystick control (persistent: -n <0-3>).");
  joy_n = joy.addPersistentIntArg("n", 0).setDescription("Joystick index (0-3).");

  // Sub-command: joy [-n <idx>] cal
  joy.addSubCommand("cal")
    .setDescription("Run the full calibration sequence for a joystick.")
    .onExecute([](Command& cmd) {
      int32_t n = cmd.getArg(joy_n).getValue();
      Serial.print("Calibrating joystick ");
      Serial.print(n);
      Serial.println("...");
      Serial.println("  Move to all extremes, then press Enter.");
    });

  // Sub-command: joy [-n <idx>] curve -type <name>
  Command& curve_cmd = joy.addSubCommand("curve");
  curve_cmd.setDescription("Set the response curve for a joystick.");
  curve_type =
    curve_cmd.addArg("type", "linear").setDescription("Curve type: linear, expo, scurve.");

  curve_cmd.onExecute([](Command& cmd) {
    int32_t n        = cmd.getArg(joy_n).getValue();
    const char* type = cmd.getArg(curve_type).getValue();
    Serial.print("Joystick ");
    Serial.print(n);
    Serial.print(": curve set to \"");
    Serial.print(type);
    Serial.println("\"");
  });

  // Sub-command: joy [-n <idx>] filter [-cutoff <hz>] [-order <1-8>]
  Command& filter_cmd = joy.addSubCommand("filter");
  filter_cmd.setDescription("Configure the low-pass filter for a joystick.");
  filter_cutoff =
    filter_cmd.addIntArg("cutoff", 100).setDescription("Filter cutoff frequency (Hz).");
  filter_order = filter_cmd.addIntArg("order", 2).setDescription("Filter order (1-8).");

  filter_cmd.onExecute([](Command& cmd) {
    int32_t n      = cmd.getArg(joy_n).getValue();
    int32_t cutoff = cmd.getArg(filter_cutoff).getValue();
    int32_t order  = cmd.getArg(filter_order).getValue();
    Serial.print("Joystick ");
    Serial.print(n);
    Serial.print(": filter cutoff=");
    Serial.print(cutoff);
    Serial.print(" Hz, order=");
    Serial.println(order);
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
