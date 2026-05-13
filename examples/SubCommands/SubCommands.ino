/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SubCommands - Demonstrates: addSubCommand, per-subcommand argument registration
 *
 * Available commands:
 *   wifi scan                         - Lists nearby networks (placeholder)
 *   wifi connect -ssid <name> -pass   - Connects to a network (-pass defaults to "")
 *   wifi disconnect                   - Disconnects from the current network
 *   help                              - Prints the full help listing
 *   help <command>                    - Prints help for a single command (positional)
 */

#include <Arduino.h>

#include <AdvancedCLI.h>
using namespace ACLI;

// CLI instance
static AdvancedCLI cli;

// Command arguments (defined globally for easy access in callbacks)
static ArgStr connect_ssid;
static ArgStr connect_pass;
static ArgStr help_target;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("---------------------------------");
  Serial.println("AdvancedCLI - SubCommands Example");
  Serial.println("---------------------------------");

  // Configure the output sink for help and error messages
  cli.setOutput([](const char* msg) { Serial.println(msg); });

  // Configure the "wifi" parent command and its three sub-commands
  Command& wifi_cmd = cli.addCommand("wifi");
  wifi_cmd.setDescription("Wi-Fi management commands.");

  // Sub-command: wifi scan
  wifi_cmd.addSubCommand("scan")
    .setDescription("Scans for nearby networks.")
    .onExecute([](Command&) {
      Serial.println("Scanning...");
      Serial.println("  1. HomeNet     [-72 dBm]");
      Serial.println("  2. OfficeWifi  [-85 dBm]");
    });

  // Sub-command: wifi connect -ssid <name> [-pass <password>]
  Command& connect_cmd = wifi_cmd.addSubCommand("connect");
  connect_cmd.setDescription("Connects to a Wi-Fi network.");

  connect_ssid = connect_cmd.addArg("ssid").setDescription("Network SSID.").setRequired();
  connect_pass = connect_cmd.addArg("pass", "").setDescription("Password (empty = open network).");

  connect_cmd.onExecute([](Command& cmd) {
    ParsedStr ssid = cmd.getArg(connect_ssid);
    ParsedStr pass = cmd.getArg(connect_pass);
    Serial.print("Connecting to \"");
    Serial.print(ssid.getValue());
    Serial.print("\"");
    if (pass.getValue()[0] != '\0') Serial.print(" with password");
    Serial.println("...");
  });

  // Sub-command: wifi disconnect
  wifi_cmd.addSubCommand("disconnect")
    .setDescription("Disconnects from the current network.")
    .onExecute([](Command&) { Serial.println("Disconnected."); });

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
