/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Validation - Demonstrates: setValidator, onInvalid
 *
 * Available commands:
 *   servo -angle <0-180>  - Sets servo angle; validator rejects values outside [0, 180]
 *   speed -rpm <0-3000>   - Sets motor speed; onInvalid provides a custom error message
 *   help                  - Prints the full help listing
 *   help <command>        - Prints help for a single command (positional)
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

#if !ACLI_ENABLE_VALIDATION_FN || !ACLI_ENABLE_INVALID_FN
#  error "Validation and onInvalid features must be enabled to compile this example."
#endif

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgInt servo_angle;
static ArgInt speed_rpm;
static ArgStr help_target;

// Validators: return true to accept the value, false to reject it
static bool validateAngle(const int32_t value) { return value >= 0 && value <= 180; }
static bool validateRpm(const int32_t value) { return value >= 0 && value <= 3000; }

// Custom invalid-value callback for -rpm: replaces the default CLI error output
static void onInvalidRpm(const char* arg_name, const char* value, const char*) {
  Serial.print("[Motor] \"");
  Serial.print(arg_name);
  Serial.print("\" ");
  Serial.print(value);
  Serial.println(" is out of range [0, 3000].");
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("--------------------------------");
  Serial.println("AdvancedCLI - Validation Example");
  Serial.println("--------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Configure the "servo" command - setValidator rejects angles outside [0, 180]
  Command& servo_cmd = cli.addCommand("servo");
  servo_cmd.setDescription("Sets the servo angle [0, 180] degrees.");

  servo_angle = servo_cmd.addIntArg("angle")
                  .setDescription("Angle in degrees.")
                  .setRequired()
                  .setValidator(validateAngle);

  servo_cmd.onExecute([](Command& cmd) {
    ParsedInt angle = cmd.getArg(servo_angle);
    Serial.print("Servo -> ");
    Serial.print(angle.getValue());
    Serial.println(" deg");
  });

  // Configure the "speed" command - setValidator rejects RPMs outside [0, 3000];
  // onInvalid provides a custom error message instead of the default CLI error
  Command& speed_cmd = cli.addCommand("speed");
  speed_cmd.setDescription("Sets motor speed [0, 3000] RPM.");

  speed_rpm = speed_cmd.addIntArg("rpm")
                .setDescription("Speed in RPM.")
                .setRequired()
                .setValidator(validateRpm)
                .onInvalid(onInvalidRpm);

  speed_cmd.onExecute([](Command& cmd) {
    ParsedInt rpm = cmd.getArg(speed_rpm);
    Serial.print("Speed -> ");
    Serial.print(rpm.getValue());
    Serial.println(" RPM");
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
