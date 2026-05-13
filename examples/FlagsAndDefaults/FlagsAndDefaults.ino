/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * FlagsAndDefaults - Demonstrates: addFlag, addIntArg with default, ParsedFlag, ParsedInt
 *
 * Available commands:
 *   led -on -pin 13         - Turns on the LED on the given pin (-pin defaults to 13)
 *   led                     - Turns off the LED (flag absent = off, default pin used)
 *   blink -pin 13 -times 1  - Blinks a pin N times (both have defaults)
 *   help                    - Prints the full help listing
 *   help <command>          - Prints help for a single command (positional)
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgFlag led_on;
static ArgInt led_pin;
static ArgInt blink_pin;
static ArgInt blink_times;
static ArgStr help_target;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("--------------------------------------");
  Serial.println("AdvancedCLI - FlagsAndDefaults Example");
  Serial.println("--------------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Configure the "led" command - -on is a flag (presence = on), -pin defaults to 13
  Command& led_cmd = cli.addCommand("led");
  led_cmd.setDescription("Controls an LED via GPIO.");

  led_on  = led_cmd.addFlag("on").setDescription("Turn LED on; absent = off.");
  led_pin = led_cmd.addIntArg("pin", 13).setDescription("Target GPIO pin.");

  led_cmd.onExecute([](Command& cmd) {
    ParsedFlag on_arg = cmd.getArg(led_on);
    ParsedInt pin_arg = cmd.getArg(led_pin);

    bool on     = on_arg.isSet();
    int32_t pin = pin_arg.getValue();

    pinMode(static_cast<uint8_t>(pin), OUTPUT);
    digitalWrite(static_cast<uint8_t>(pin), on ? HIGH : LOW);

    Serial.print("LED pin ");
    Serial.print(pin);
    Serial.println(on ? " -> ON" : " -> OFF");
  });

  // Configure the "blink" command - both -pin and -times have defaults
  Command& blink_cmd = cli.addCommand("blink");
  blink_cmd.setDescription("Blinks a GPIO pin.");

  blink_pin   = blink_cmd.addIntArg("pin", 13).setDescription("Target GPIO pin.");
  blink_times = blink_cmd.addIntArg("times", 1).setDescription("Number of times to blink.");

  blink_cmd.onExecute([](Command& cmd) {
    ParsedInt pin_arg   = cmd.getArg(blink_pin);
    ParsedInt times_arg = cmd.getArg(blink_times);
    int32_t pin         = pin_arg.getValue();
    int32_t times       = times_arg.getValue();

    pinMode(static_cast<uint8_t>(pin), OUTPUT);
    for (int32_t i = 0; i < times; ++i) {
      digitalWrite(static_cast<uint8_t>(pin), HIGH);
      delay(200);
      digitalWrite(static_cast<uint8_t>(pin), LOW);
      delay(200);
    }

    Serial.print("Blinked pin ");
    Serial.print(pin);
    Serial.print(' ');
    Serial.print(times);
    Serial.println(" time(s).");
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
