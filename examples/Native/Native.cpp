/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Native - A run-to-completion command-line program driven by argv.
 *
 * Demonstrates: setOutput, addCommand, addFlag, addIntArg, addArg (with defaults), setRequired,
 * onExecute, onUnknownCommand, parse, printHelp, and using lastParseOk() as the process exit code.
 *
 * Unlike the Arduino examples (setup/loop + Serial), a native program receives its command on the
 * command line and runs once. The tokens after the program name are joined into a single line and
 * handed to the parser, exactly as if the user had typed them into a serial console.
 *
 * Build:
 * - PowerShell: $env:EXAMPLE="examples/Native"; pio run -e native-example
 * - bash/WSL  : export EXAMPLE="examples/Native"; pio run -e native-example
 *
 * Run the resulting binary directly so you can pass arguments:
 *   .pio/build/native-example/program led -pin 13 -on -label kitchen
 *   .pio/build/native-example/program add -a 20 -b 22
 *   .pio/build/native-example/program help
 *   .pio/build/native-example/program            (no args: prints the help listing)
 *
 * The process exit code is 0 on success and 1 when parsing or execution fails.
 */

#include <AdvancedCLI.h>

#include <cstddef>
#include <cstdio>

using namespace ACLI;

// CLI instance and argument handles at file scope, so the (non-capturing) callbacks can reach them.
static AdvancedCLI cli;

static ArgInt led_pin;
static ArgFlag led_on;
static ArgStr led_label;

static ArgInt add_a;
static ArgInt add_b;

static ArgStr help_target;

static void setupCli() {
  // Route all library output (help and error messages) to stdout.
  cli.setOutput([](const char* msg) { std::puts(msg); });

  // Replace the default "[CLI] Unknown command" output with our own message.
  cli.onUnknownCommand(
    [](const char* name) { std::printf("Unknown command: \"%s\". Try \"help\".\n", name); });

  // led -pin <n> -on -label <s>
  Command& led = cli.addCommand("led");
  led.setDescription("Turn an LED on or off.");
  led_pin   = led.addIntArg("pin", 13).setDescription("GPIO pin number (default 13).");
  led_on    = led.addFlag("on").setDescription("Turn the LED on (absent means off).");
  led_label = led.addArg("label", "led").setDescription("Friendly name for the LED.");
  led.onExecute([](Command& cmd) {
    std::printf("[led] \"%s\" on pin %d -> %s\n",
      cmd.getArg(led_label).getValue(),
      cmd.getArg(led_pin).getValue(),
      cmd.getArg(led_on).isSet() ? "ON" : "OFF");
  });

  // add -a <int> -b <int>
  Command& add = cli.addCommand("add");
  add.setDescription("Add two integers.");
  add_a = add.addIntArg("a").setRequired().setDescription("First addend (required).");
  add_b = add.addIntArg("b", 0).setDescription("Second addend (default 0).");
  add.onExecute([](Command& cmd) {
    const int32_t a = cmd.getArg(add_a).getValue();
    const int32_t b = cmd.getArg(add_b).getValue();
    std::printf("[add] %d + %d = %d\n", a, b, a + b);
  });

  // help [command]
  Command& help = cli.addCommand("help");
  help.setDescription("List all commands, or show help for one command.");
  help_target = help.addPosArg("command").setDescription("Command name (optional).");
  help.onExecute([](Command& cmd) {
    ParsedStr target = cmd.getArg(help_target);
    if (target.isSet()) {
      cli.printHelp(target.getValue());
    } else {
      cli.printHelp();
    }
  });
}

int main(int argc, char** argv) {
  std::puts("----------------------------");
  std::puts("AdvancedCLI - Native Example");
  std::puts("----------------------------");

  setupCli();

  if (!cli.isValid()) {
    std::fprintf(stderr,
      "[WARN] CLI registration overflowed: check MAX_COMMANDS/MAX_ARGS_TOTAL.\n");
  }

  // No command given: show the help listing and exit successfully.
  if (argc < 2) {
    std::puts("AdvancedCLI - Native example. Available commands:");
    cli.printHelp();
    return 0;
  }

  // Join argv[1..] into a single line (bounded by MAX_INPUT_LEN), then parse it once.
  char line[Config::MAX_INPUT_LEN] = {};
  size_t pos                       = 0;
  for (int i = 1; i < argc; ++i) {
    if (i > 1 && pos < sizeof(line) - 1) line[pos++] = ' ';
    for (const char* p = argv[i]; *p && pos < sizeof(line) - 1; ++p) {
      line[pos++] = *p;
    }
  }
  line[pos] = '\0';

  cli.parse(line);

  // Run-to-completion: report success or failure through the process exit code.
  return cli.lastParseOk() ? 0 : 1;
}
