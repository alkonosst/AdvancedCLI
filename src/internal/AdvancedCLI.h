/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "acli-command.h"
#include "acli-config.h"

#include <stddef.h>

namespace ACLI {

/**
 * @brief Main CLI class. Owns a fixed-size array of registered commands.
 *
 * Typical usage:
 * 1. Call `setOutput()` to attach a print function.
 * 2. Call `addCommand()` to register commands and configure them with builder methods.
 * 3. In the main loop, call `parse()` with each incoming input line.
 */
class AdvancedCLI {
  friend class Command; // allows Command::fail() to set _last_parse_ok and call _outputf

  public:
  AdvancedCLI() = default;

  /* --------------------- Configuration (call before registering commands) --------------------- */

  /**
   * @brief Set the output sink for all library-generated text (errors, help).
   *
   * Defaults to silent (no output) when not set.
   *
   * @param output_fn Callback of type `OutputFn`.
   */
  void setOutput(OutputFn output_fn);

  /**
   * @brief Register a handler called when no command matches the first token.
   *
   * If not set, the library prints `[CLI] Unknown command: ...` to the output sink.
   *
   * @param fn Callback of type `UnknownCommandFn`.
   */
  void onUnknownCommand(UnknownCommandFn fn);

  /**
   * @brief Enable or disable case-sensitive command and argument matching.
   * @param enable `true` = case-sensitive; `false` = case-insensitive (default).
   */
  void setCaseSensitive(bool enable);

  /* ----------------------------------- Command registration ----------------------------------- */

  /**
   * @brief Register a new top-level command.
   *
   * If the command table is full, returns a reference to an internal dummy command that is safely
   * discarded.
   *
   * @param name Command name string literal (zero-copy, must remain valid for the lifetime of the
   * `AdvancedCLI` instance).
   * @return `Command&` Reference to the new `Command` for further configuration.
   */
  Command& addCommand(const char* name);

  /* ------------------------------------------ Parsing ----------------------------------------- */

  /**
   * @brief Parse a null-terminated input line.
   *
   * Finds the matching command, parses arguments, validates required arguments and types, and
   * invokes the execution callback - all synchronously, no queue.
   *
   * @param input Null-terminated input string.
   * @return `true` if the line was dispatched without any parse or runtime errors.
   */
  bool parse(const char* input);

  /**
   * @brief Parse an input buffer of known length (does not require a null terminator).
   * @param input Input buffer.
   * @param len Number of characters to process.
   * @return `true` if dispatched without errors.
   */
  bool parse(const char* input, size_t len);

  /* ------------------------------------------- Help ------------------------------------------- */

  /**
   * @brief Print registered commands to the configured output sink.
   *
   * The optional `depth` argument controls how much detail is printed:
   * - `1`: command names and descriptions only.
   * - `2`: commands + sub-command names and descriptions (no argument lines).
   * - `3` (default): full output - commands, sub-commands, and all argument lines.
   *
   * @param depth Detail level (1-3). Values outside [1, 3] are clamped.
   */
  void printHelp(uint8_t depth = 3) const;

  /**
   * @brief Print the help entry for a single named command (and its sub-commands). Does nothing if
   * the command is not found.
   * @param cmd_name Name of the command to print.
   * @param depth Detail level (1-3). See `printHelp(uint8_t)` for depth semantics.
   */
  void printHelp(const char* cmd_name, uint8_t depth = 3) const;

  /**
   * @brief Print the help entry for a specific `Command` instance (and its sub-commands).
   *
   * Unlike `printHelp(const char*)`, this overload is unambiguous when multiple commands share the
   * same name (e.g. two "control" sub-commands under different parents). Pass the `Command&`
   * reference returned by `addCommand()` or `addSubCommand()` at registration time.
   *
   * @param cmd Command instance to print.
   * @param depth Detail level (1-3). See `printHelp(uint8_t)` for depth semantics.
   */
  void printHelp(const Command& cmd, uint8_t depth = 3) const;

  /* ----------------------------------- Inject (unit-testing) ---------------------------------- */

  /**
   * @brief Inject a command string as if the user typed it. Equivalent to parse(), but returns the
   * result directly.
   *
   * E.g.:
   * ```
   * assert(cli.inject("led --on --pin 4"));
   * ```
   *
   * @param input Null-terminated command string.
   * @return `true` if the command executed without errors.
   */
  bool inject(const char* input);

#if ACLI_USE_STD_FUNCTION
  /**
   * @brief Inject a command string and capture all library output into a buffer.
   *
   * Redirects the output sink to `output_buf` for the duration of the call, then restores
   * the original sink. Only available when `ACLI_USE_STD_FUNCTION=1`.
   *
   * @param input Null-terminated command string.
   * @param output_buf Buffer that receives all output lines, newline-separated.
   * @param buf_size Size of `output_buf` in bytes.
   * @return `true` if the command executed without errors.
   */
  bool inject(const char* input, char* output_buf, size_t buf_size);
#endif

  /**
   * @brief Get the result of the last parse or inject operation.
   * @return `true` if the last command executed without errors; `false` if there was a parse or
   * runtime error.
   */
  bool lastParseOk() const;

  /* ------------------------------------------ Utility ----------------------------------------- */

  /**
   * @brief Get the number of successfully registered commands.
   *
   * Compare with `getAttemptedCommandCount()` to detect silent overflow.
   *
   * @return `uint16_t` Number of commands actually in the command table.
   */
  uint16_t getCommandCount() const;

  /**
   * @brief Get the total number of argument pool slots consumed by registered commands.
   *
   * Compare with `getAttemptedArgCount()` to detect silent overflow.
   *
   * @return `uint16_t` Number of pool slots consumed.
   */
  uint16_t getArgCount() const;

  /**
   * @brief Get the total number of addCommand() / addSubCommand() calls attempted.
   *
   * Includes calls that were silently dropped due to table overflow. When no overflow occurred this
   * equals `getCommandCount()`. When overflow occurred, use this value to determine the minimum
   * `ACLI_MAX_COMMANDS` value required.
   *
   * @return `uint16_t` Number of command registrations attempted.
   */
  uint16_t getAttemptedCommandCount() const;

  /**
   * @brief Get the total number of add*Arg() calls that reached the pool-overflow check.
   *
   * Includes calls that were silently dropped because the argument pool was full. When no overflow
   * occurred this equals `getArgCount()`. When overflow occurred, use this value to determine the
   * minimum `ACLI_MAX_ARGS_TOTAL` value required.
   *
   * @return `uint16_t` Number of argument registrations attempted against the pool.
   */
  uint16_t getAttemptedArgCount() const;

  /**
   * @brief Verify that no registration overflow has occurred.
   *
   * Returns `false` if any `addCommand()` or `addSubCommand()` call was silently dropped because
   * the command table (`MAX_COMMANDS`) was full, or a command could not receive argument capacity
   * because the pool (`MAX_ARGS_TOTAL`) was exhausted.
   *
   * When `false` is returned, call `getAttemptedCommandCount()` and `getAttemptedArgCount()` to
   * determine the minimum macro values required.
   *
   * Call this once at the end of `setup()` to confirm that all registrations succeeded.
   *
   * @return `true` if all registrations succeeded; `false` on overflow.
   */
  bool isValid() const;

  private:
  Command _commands[Config::MAX_COMMANDS] = {};
  uint16_t _cmd_count                     = 0;
  uint16_t _cmd_attempted = 0; // counts all addCommand/addSubCommand calls, including overflow

  // Argument pool: all ArgDef and ParsedArg instances live here.
  // Each add*Arg() call consumes exactly one slot.
  detail::ArgDef _arg_def_pool[Config::MAX_ARGS_TOTAL]   = {};
  detail::ParsedArg _parsed_pool[Config::MAX_ARGS_TOTAL] = {};
  uint16_t _arg_pool_used                                = 0;
  uint16_t _arg_attempted = 0; // counts add*Arg calls that passed contiguity/sealed checks

  bool _overflow       = false; // true when MAX_COMMANDS or MAX_ARGS_TOTAL is exceeded
  bool _case_sensitive = false;
  bool _last_parse_ok  = true; // false when parse() encounters any error

  OutputFn _output_fn{};

  UnknownCommandFn _unknown_cmd_fn{};

  Command _dummy = {}; // returned when addCommand table is full

  // Internal helpers
  Command& _addCommandInternal(const char* name, int16_t parent_idx);
  Command* _findCommand(const char* name, size_t name_len);
  Command* _findSubCommand(const Command* parent, const char* name);
  uint8_t _tokenize(const char* input, size_t input_len, char tokens[][Config::MAX_TOKEN_LEN],
    uint8_t max_tokens) const;

  void _output(const char* str) const;
  void _outputf(const char* fmt, ...) const;

  // Build "cmdname [-flag] [-arg <arg>] <pos>" into buf
  void _buildUsageStr(const Command& cmd, char* buf, size_t buf_size) const;

  // Emit validation error: calls d.onInvalidFn if set (per-arg wins),
  // otherwise routes through fireError().
  void _fireInvalid(Command& cmd, const detail::ArgDef& d, const char* value, const char* reason,
    const char* usage_str);

  // Route a command-level error: sets _last_parse_ok, calls cmd._error_callback
  // if registered, otherwise prints to the output sink.
  // usage_str is appended to the sink output when no callback is registered.
  void _fireError(Command& cmd, const char* message, const char* usage_str = nullptr);

  // Print one command entry (name, description, and optionally args) at the given indent level
  void _printCommandEntry(const Command& cmd, uint8_t indent, bool print_args) const;
};

} // namespace ACLI
