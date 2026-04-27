/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * HelpDepthAndParsedCount - Demonstrates: parsedArgCount, printHelp(depth), printHelp(cmd_name,
 * depth)
 *
 * The "help" command accepts an optional depth argument (1-3) to control how much
 * detail is printed. The "wifi" command uses parsedArgCount() to report how many
 * arguments were actually provided by the user.
 *
 * Available commands:
 *   wifi -ssid -pass -verbose  - Connect to a Wi-Fi network
 *     scan                     - Scan for nearby networks
 *     status                   - Print connection status
 *   help [command] [-depth N]  - Print help at the requested detail level
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgStr wifi_ssid;
static ArgStr wifi_pass;
static ArgFlag wifi_verbose;
static ArgStr help_target;
static ArgInt help_depth;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("---------------------------------------------");
  Serial.println("AdvancedCLI - HelpDepthAndParsedCount Example");
  Serial.println("---------------------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Configure the "wifi" command
  Command& wifi_cmd = cli.addCommand("wifi");
  wifi_cmd.setDescription("Connect to a Wi-Fi network.");

  wifi_ssid    = wifi_cmd.addArg("ssid").setRequired().setDescription("Network SSID.");
  wifi_pass    = wifi_cmd.addArg("pass", "").setDescription("Network password (optional).");
  wifi_verbose = wifi_cmd.addFlag("verbose").setAlias("v").setDescription("Print extra details.");

  wifi_cmd.onExecute([](Command& cmd) {
    // getParsedArgCount() tells us how many args were explicitly provided - no need to chain
    // isSet() calls. Useful for dispatching or logging without touching each arg individually.
    Serial.print("wifi: ");
    Serial.print(cmd.getParsedArgCount());
    Serial.println(" arg(s) provided.");

    ParsedStr ssid     = cmd.getArg(wifi_ssid);
    ParsedStr pass     = cmd.getArg(wifi_pass);
    ParsedFlag verbose = cmd.getArg(wifi_verbose);

    Serial.print("  Connecting to: ");
    Serial.println(ssid.getValue());

    if (pass.isSet() && pass.getValue()[0] != '\0') {
      Serial.println("  Using password.");
    }

    if (verbose.isSet()) {
      Serial.print("  SSID length : ");
      Serial.println(strlen(ssid.getValue()));
      Serial.print("  Secure      : ");
      Serial.println(pass.isSet() && pass.getValue()[0] != '\0' ? "yes" : "no");
    }
  });

  // Configure sub-commands for "wifi"
  wifi_cmd.addSubCommand("scan")
    .setDescription("Scan for nearby networks.")
    .onExecute([](Command&) { Serial.println("Scanning..."); });

  wifi_cmd.addSubCommand("status")
    .setDescription("Print connection status.")
    .onExecute([](Command&) { Serial.println("Status: disconnected."); });

  // Configure the "help" command with an optional positional target and optional depth
  Command& help_cmd = cli.addCommand("help");

  help_cmd.setDescription("Prints available commands.");
  help_target = help_cmd.addPosArg("command").setDescription("Command name (optional).");
  help_depth =
    help_cmd.addIntArg("depth", 3).setDescription("Detail level: 1=cmds, 2=+sub, 3=full.");

  help_cmd.onExecute([](Command& cmd) {
    ParsedStr target = cmd.getArg(help_target);
    ParsedInt depth  = cmd.getArg(help_depth);

    // Clamp depth to [1, 3]
    int32_t d = depth.getValue();
    if (d < 1) d = 1;
    if (d > 3) d = 3;

    if (target.isSet()) {
      cli.printHelp(target.getValue(), static_cast<uint8_t>(d));
    } else {
      cli.printHelp(static_cast<uint8_t>(d));
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
