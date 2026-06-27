/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * NativeBatch - Runs a built-in sequence of command lines to show many features at once.
 *
 * Demonstrates: positional arguments, sub-commands, persistent arguments (parsed before the
 * sub-command name), aliases, getArgByName() with parent-persistent fallback, printHelp(depth), and
 * the output-capturing inject(input, buf, size) overload.
 *
 * Run-to-completion: there is no interactive input. main() feeds a fixed script through inject()
 * and prints the result of each line, so a single run shows the whole feature set.
 *
 * Build & run:
 * - PowerShell: $env:EXAMPLE="examples/NativeBatch"; pio run -e native-example -t exec
 * - bash/WSL  : export EXAMPLE="examples/NativeBatch"; pio run -e native-example -t exec
 */

#include <AdvancedCLI.h>

#include <cstdio>
#include <cstring>

using namespace ACLI;

static AdvancedCLI cli;

static ArgStr copy_src;
static ArgStr copy_dst;

static ArgInt read_addr;

static void setupCli() {
  // Indent library output so it stands out from the "$ <command>" echo lines.
  cli.setOutput([](const char* msg) { std::printf("    %s\n", msg); });

  // copy <src> <dst> - two positional arguments matched by order, no leading dashes.
  Command& copy = cli.addCommand("copy");
  copy.setDescription("Copy a file.");
  copy_src = copy.addPosArg("src").setDescription("Source path.");
  copy_dst = copy.addPosArg("dst").setDescription("Destination path.");
  copy.onExecute([](Command& cmd) {
    std::printf("    copy \"%s\" -> \"%s\"\n",
      cmd.getArg(copy_src).getValue(),
      cmd.getArg(copy_dst).getValue());
  });

  // dev - a parent command with persistent arguments shared by all of its sub-commands.
  // IMPORTANT: register every persistent argument BEFORE adding sub-commands. Adding a sub-command
  // seals the parent, so it can no longer accept new argument registrations.
  Command& dev = cli.addCommand("dev");
  dev.setDescription("Talk to a device bus.");
  dev.addPersistentArg("bus", "i2c").setDescription("Bus name (default i2c).");
  dev.addPersistentFlag("verbose").setAlias("v").setDescription("Verbose output (alias: -v).");

  // dev scan - reads the parent's persistent args by name (getArgByName falls back to the parent).
  Command& scan = dev.addSubCommand("scan");
  scan.setDescription("Scan the bus for devices.");
  scan.onExecute([](Command& cmd) {
    const bool verbose = cmd.getArgByName("verbose").isSet();
    std::printf("    scan bus=%s%s\n",
      cmd.getArgByName("bus").getValue(),
      verbose ? " (verbose)" : "");
  });

  // dev read -addr <n> - has its own argument plus access to the persistent ones.
  Command& read = dev.addSubCommand("read");
  read.setDescription("Read one register.");
  read_addr = read.addIntArg("addr", 0).setDescription("Register address (accepts 0x.. hex).");
  read.onExecute([](Command& cmd) {
    const bool verbose = cmd.getArgByName("verbose").isSet();
    std::printf("    read bus=%s addr=%d%s\n",
      cmd.getArgByName("bus").getValue(),
      cmd.getArg(read_addr).getValue(),
      verbose ? " (verbose)" : "");
  });
}

// Echo the line, then dispatch it through the parser.
static void run(const char* line) {
  std::printf("$ %s\n", line);
  cli.inject(line);
  std::printf("\n");
}

int main() {
  std::puts("----------------------------------");
  std::puts("AdvancedCLI - Native Batch Example");
  std::puts("----------------------------------");

  setupCli();

  std::puts("=== Positional arguments ===");
  run("copy notes.txt backup/notes.txt");

  std::puts("=== Sub-commands + persistent args (given before the sub-command name) ===");
  run("dev scan");
  run("dev -bus spi scan");
  run("dev -bus spi -verbose read -addr 0x1F");

  std::puts("=== Alias: -v is an alias for -verbose ===");
  run("dev -v read -addr 42");

  std::puts("=== printHelp at each depth ===");
  std::puts("-- depth 1 (commands only) --");
  cli.printHelp(1);
  std::puts("-- depth 2 (+ sub-commands) --");
  cli.printHelp(2);
  std::puts("-- depth 3 (+ argument lines) --");
  cli.printHelp(3);

  // The inject(input, buf, size) overload redirects the output sink into a buffer for the duration
  // of the call. It captures what the LIBRARY writes to the sink (help and error messages), not a
  // command callback's own printf(), which goes straight to stdout. Here we capture the error from
  // an invalid address so nothing reaches the console.
  std::puts("\n=== Capturing library output with inject(input, buf, size) ===");
  char captured[256];
  cli.inject("dev read -addr xyz", captured, sizeof(captured));
  std::printf("captured %zu byte(s):\n%s\n", std::strlen(captured), captured);

  return 0;
}
