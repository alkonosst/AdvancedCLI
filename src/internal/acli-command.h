/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "acli-argument.h"
#include "acli-config.h"

namespace ACLI {

// Forward declaration - full definition in AdvancedCLI.h
class AdvancedCLI;

/**
 * @brief Represents a single CLI command. Returned by `AdvancedCLI::addCommand()`.
 *
 * Use builder methods (`addArg`, `addFlag`, `onExecute`, etc.) during setup to configure the
 * command. Inside the execution callback, use `getArg()` and `getArgByName()` to read parsed
 * argument values.
 */
class Command {
  friend class detail::ArgBaseImpl;
  friend class detail::ArgReaderBase;
  friend class AdvancedCLI;

  public:
  Command() = default;

  /* -------------------------- Builder API (used during registration) -------------------------- */

  /**
   * @brief Set the description shown by `AdvancedCLI::printHelp()`.
   * @param description Null-terminated string literal.
   * @return `Command&` Reference to this for chaining.
   */
  Command& setDescription(const char* description);

  /**
   * @brief Register a named string argument (syntax: `-name value` or `--name value`).
   * @param name Argument name without the leading dash.
   * @param default_value Optional default string literal (zero-copy).
   * @return `ArgStr` Handle to the registered argument.
   */
  ArgStr addArg(const char* name, const char* default_value = nullptr);

  /**
   * @brief Register a boolean flag argument (presence = `true`, absence = `false`; syntax: `-name`
   * or `--name`).
   * @param name Flag name without the leading dash.
   * @return `ArgFlag` Handle to the registered flag.
   */
  ArgFlag addFlag(const char* name);

  /**
   * @brief Register a named integer argument (syntax: `-name value`).
   * @param name Argument name without the leading dash.
   * @return `ArgInt` Handle to the registered argument.
   */
  ArgInt addIntArg(const char* name);

  /**
   * @brief Register a named integer argument with a default value (syntax: `-name value`).
   * @param name Argument name without the leading dash.
   * @param default_value Default `int32_t` used when the argument is not provided.
   * @return `ArgInt` Handle to the registered argument.
   */
  ArgInt addIntArg(const char* name, int32_t default_value);

  /**
   * @brief Register a named `float` argument (syntax: `-name value`).
   * @param name Argument name without the leading dash.
   * @return `ArgFloat` Handle to the registered argument.
   */
  ArgFloat addFloatArg(const char* name);

  /**
   * @brief Register a named float argument with a default value (syntax: `-name value`).
   * @param name Argument name without the leading dash.
   * @param default_value Default `float` used when the argument is not provided.
   * @return `ArgFloat` Handle to the registered argument.
   */
  ArgFloat addFloatArg(const char* name, float default_value);

  /**
   * @brief Register a positional string argument (matched by position, no dash prefix).
   * @param name Argument name used in help output.
   * @param default_value Optional default string literal (zero-copy).
   * @return `ArgStr` Handle to the registered argument.
   */
  ArgStr addPosArg(const char* name, const char* default_value = nullptr);

  /**
   * @brief Register a positional integer argument (matched by position, no dash prefix).
   * @param name Argument name used in help output.
   * @return `ArgInt` Handle to the registered argument.
   */
  ArgInt addPosIntArg(const char* name);

  /**
   * @brief Register a positional float argument (matched by position, no dash prefix).
   * @param name Argument name used in help output.
   * @return `ArgFloat` Handle to the registered argument.
   */
  ArgFloat addPosFloatArg(const char* name);

  /**
   * @brief Register the execution callback, invoked after successful parsing.
   * @param cb Callback of type `CallbackFn`.
   * @return `Command&` Reference to this for chaining.
   */
  Command& onExecute(CallbackFn cb);

  /**
   * @brief Register a command-level error handler.
   *
   * Called on any parse error for this command, or when the callback calls fail(). Per-argument
   * onInvalid() callbacks take priority over this handler. If not set, errors are printed to the
   * output sink.
   *
   * @param cb Callback of type `ErrorFn`.
   */
  Command& onError(ErrorFn cb);

  /* ------------------------ Runtime accessors (valid inside a callback) ----------------------- */

  /**
   * @brief Retrieve a parsed argument by its builder handle.
   *
   * The return type is deduced from the handle type:
   * `ArgStr -> ParsedStr`, `ArgInt -> ParsedInt`,
   * `ArgFloat -> ParsedFloat`, `ArgFlag -> ParsedFlag`.
   *
   * Use `auto` to let the compiler deduce the type.
   *
   * @param handle Handle returned from addArg(), addIntArg(), addFloatArg(), addFlag(), etc.
   * @return Typed parsed reader; invalid (`isValid() == false`) if the handle does not belong to
   * this command.
   */
  template <typename T>
  typename detail::ReaderOf<T>::type getArg(T& handle) {
    using R = typename detail::ReaderOf<T>::type;
    if (!handle.isValid() || handle._cmd != this) return R();
    return R(this, handle._arg_index);
  }

  /**
   * @brief Retrieve a parsed argument by name. Searches primary name and aliases.
   * @param name Argument name to look up (without the leading dash).
   * @return `ParsedAny` reader; invalid (`isValid() == false`) if not found.
   */
  ParsedAny getArgByName(const char* name);

  /* --------------------------------------- Sub-commands --------------------------------------- */

  /**
   * @brief Register a sub-command under this command.
   *
   * Example: if this command is "wifi", calling `addSubCommand("scan")` creates "wifi scan".
   * Requires that this command was registered through `AdvancedCLI::addCommand()`.
   *
   * @param name Sub-command name string literal.
   * @return `Command&` Reference to the new `Command` for further configuration.
   */
  Command& addSubCommand(const char* name);

  /**
   * @brief Verify if this command is a sub-command (has a parent command).
   * @return `true` if this command is a sub-command; `false` if it's a top-level command.
   */
  bool isSubCommand() const;

  /**
   * @brief Signal a runtime failure from inside the execution callback.
   *
   * Sets the parse result to failed and routes through `onError()` if registered, otherwise prints
   * message to the output sink.
   *
   * @param message Human-readable error description.
   */
  void fail(const char* message);

  /* ----------------------------------------- Accessors ---------------------------------------- */

  /**
   * @brief Get the name of this command.
   * @return `const char*` Command name string literal, or "" if not set.
   */
  const char* getName() const;

  /**
   * @brief Get the description of this command.
   * @return `const char*` Command description string literal, or "" if not set.
   */
  const char* getDescription() const;

  /**
   * @brief Verify if this command is valid (was registered).
   * @return `true` if valid; `false` if this is an unregistered dummy command.
   */
  bool isValid() const;

  /**
   * @brief Get the number of arguments registered for this command.
   * @return `uint8_t` Number of registered arguments.
   */
  uint8_t getArgCount() const;

  /**
   * @brief Get the number of arguments that were actually set during the last parse.
   *
   * Valid inside the execution callback. An argument counts as set when it was explicitly provided
   * in the input OR has a default value.
   *
   * @return `uint8_t` Number of set arguments.
   */
  uint8_t getParsedArgCount() const;

  private:
  const char* _name        = nullptr; // zero-copy: points to registration-time string literal
  const char* _description = nullptr; // zero-copy: points to user-supplied string literal

  detail::ArgDef _arg_defs[Config::MAX_ARGS_PER_CMD]  = {};
  detail::ParsedArg _parsed[Config::MAX_ARGS_PER_CMD] = {};
  uint8_t _arg_count                                  = 0;

  CallbackFn _callback{};
  ErrorFn _error_callback{};

  // Owner linkage (set by AdvancedCLI at registration time)
  AdvancedCLI* _owner = nullptr;
  int8_t _self_idx    = -1; // index of this Command in owner's _commands[]
  int8_t _parent_idx  = -1; // -1 = top-level; >=0 = index of parent Command

  // Init command (called by AdvancedCLI)
  void _init(const char* name, AdvancedCLI* owner, int8_t self_idx, int8_t parent_idx = -1);

  // Reset all parsed values before a new parse
  void _resetParsed();

  // Execute the callback
  void _execute();

  // Find ArgDef index by token (strips leading dashes before comparing)
  int8_t _findArgDefByName(const char* token) const; // returns index or -1

  // Returns the ArgDef index of the nth positional argument (0-based)
  int8_t _positionalArgIndex(int8_t pos_idx) const; // returns ArgDef index of the nth positional

  // Returns index of newly added ArgDef, or -1 if full
  int8_t _addArgInternal(const char* name, detail::ArgType type, detail::ArgValueType value_type);
};

} // namespace ACLI
