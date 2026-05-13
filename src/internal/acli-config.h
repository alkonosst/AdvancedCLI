/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/* ---------------------------------------------------------------------------------------------- */
/*                    AdvancedCLI - Platform detection and static configuration                   */
/* ---------------------------------------------------------------------------------------------- */

/* --------------------------------- std::function availability --------------------------------- */
// AVR does not ship <functional>. On all other known Arduino targets (ESP32, ESP8266, ARM
// Cortex-M/A, RP2040, SAMD, STM32, nRF52...) it is available.
//
// Override before including this header, or via build_flags:
//
// `-D ACLI_USE_STD_FUNCTION=0`: force plain function pointers
// `-D ACLI_USE_STD_FUNCTION=1`: force std::function
//
// NOTE: when `ACLI_USE_STD_FUNCTION=0`, lambdas without captures still work (they decay to function
// pointers), but lambdas with captures do not.
/* ---------------------------------------------------------------------------------------------- */

#if !defined(ACLI_USE_STD_FUNCTION)
#  if defined(__AVR__)
#    define ACLI_USE_STD_FUNCTION 0
#  else
#    define ACLI_USE_STD_FUNCTION 1
#  endif
#endif

#if ACLI_USE_STD_FUNCTION
#  include <functional>
#endif

/* -------------------------------------- Capacity defaults ------------------------------------- */
// Platform-specific defaults are chosen conservatively for AVR and generously for 32-bit targets.
// Every value can be overridden before including this header or via build_flags, e.g.:
// `-D ACLI_MAX_COMMANDS=16`
/* ---------------------------------------------------------------------------------------------- */

#if defined(__AVR__)
// AVR RAM is scarce (2-8 kB typical)
#  ifndef ACLI_MAX_COMMANDS
#    define ACLI_MAX_COMMANDS 4
#  endif
#  ifndef ACLI_MAX_ARGS_TOTAL
#    define ACLI_MAX_ARGS_TOTAL 10
#  endif
#  ifndef ACLI_MAX_NAME_LEN
#    define ACLI_MAX_NAME_LEN 8
#  endif
#  ifndef ACLI_MAX_VALUE_LEN
#    define ACLI_MAX_VALUE_LEN 32
#  endif
#  ifndef ACLI_MAX_DESC_LEN
#    define ACLI_MAX_DESC_LEN 16
#  endif
#  ifndef ACLI_MAX_INPUT_LEN
#    define ACLI_MAX_INPUT_LEN 64
#  endif
#  ifndef ACLI_MAX_ALIASES
#    define ACLI_MAX_ALIASES 1
#  endif
#  ifndef ACLI_MAX_TOKENS
#    define ACLI_MAX_TOKENS 9 // 1 (command) + 4 named-arg pairs (name + value)
#  endif
#  ifndef ACLI_ENABLE_VALIDATION_FN
#    define ACLI_ENABLE_VALIDATION_FN 0
#  endif
#  ifndef ACLI_ENABLE_INVALID_FN
#    define ACLI_ENABLE_INVALID_FN 0
#  endif
#else
// 32-bit platforms (ESP32, ESP8266, ARM, RP2040...)
#  ifndef ACLI_MAX_COMMANDS
#    define ACLI_MAX_COMMANDS 16
#  endif
#  ifndef ACLI_MAX_ARGS_TOTAL
#    define ACLI_MAX_ARGS_TOTAL 48
#  endif
#  ifndef ACLI_MAX_NAME_LEN
#    define ACLI_MAX_NAME_LEN 24
#  endif
#  ifndef ACLI_MAX_VALUE_LEN
#    define ACLI_MAX_VALUE_LEN 64
#  endif
#  ifndef ACLI_MAX_DESC_LEN
#    define ACLI_MAX_DESC_LEN 64
#  endif
#  ifndef ACLI_MAX_INPUT_LEN
#    define ACLI_MAX_INPUT_LEN 256
#  endif
#  ifndef ACLI_MAX_ALIASES
#    define ACLI_MAX_ALIASES 4
#  endif
#  ifndef ACLI_MAX_TOKENS
#    define ACLI_MAX_TOKENS 21 // 1 (command) + 10 named-arg pairs (name + value)
#  endif
#endif

// Derived - also overridable
#ifndef ACLI_MAX_TOKEN_LEN
#  define ACLI_MAX_TOKEN_LEN ACLI_MAX_VALUE_LEN
#endif

/* ----------------------------------- Optional feature flags ----------------------------------- */
// These flags control optional argument features. Disabling them removes the corresponding fields
// from ArgDef, which reduces the size of every registered argument and therefore the total
// sizeof(AdvancedCLI).
//
// On ESP32 with the default configuration (ACLI_MAX_ARGS_TOTAL = 48 ArgDefs) and
// sizeof(std::function) == 32:
//   ACLI_ENABLE_VALIDATION_FN=0 -> saves ~1536 bytes
//   ACLI_ENABLE_INVALID_FN=0    -> saves ~1536 bytes
//   both disabled               -> saves ~3072 bytes
//
// Attempting to call setValidator() when ACLI_ENABLE_VALIDATION_FN=0, or onInvalid() when
// ACLI_ENABLE_INVALID_FN=0, will produce a compile-time error.
/* ---------------------------------------------------------------------------------------------- */

// Set to 0 to remove per-argument validation callbacks (setValidator()).
#ifndef ACLI_ENABLE_VALIDATION_FN
#  define ACLI_ENABLE_VALIDATION_FN 1
#endif

// Set to 0 to remove per-argument invalid-value callbacks (onInvalid()).
#ifndef ACLI_ENABLE_INVALID_FN
#  define ACLI_ENABLE_INVALID_FN 1
#endif

/* -------------------- Namespace-scoped constants and callback type aliases -------------------- */

namespace ACLI {

/**
 * @brief Compile-time capacity constants. All values are overridable via build flags.
 *
 *  Platform-specific defaults are chosen conservatively for AVR and generously for 32-bit
 *  targets. Override any constant before including this header or via build flags, e.g.:
 *  `-D ACLI_MAX_COMMANDS=16`
 */
namespace Config {

// Maximum number of commands that can be registered in a single AdvancedCLI instance.
static constexpr uint8_t MAX_COMMANDS = ACLI_MAX_COMMANDS;

// Total argument slots in the shared pool, consumed one slot per add*Arg() call across all
// registered commands. Tune this to the exact sum of arguments you actually register across all
// commands and sub-commands.
// Example: 3 commands with 2, 1, and 4 args respectively -> set MAX_ARGS_TOTAL = 7.
static constexpr uint8_t MAX_ARGS_TOTAL = ACLI_MAX_ARGS_TOTAL;

// Maximum length (including null terminator) of a command or argument name.
static constexpr uint8_t MAX_NAME_LEN = ACLI_MAX_NAME_LEN;

// Maximum length (including null terminator) of an argument value or string default.
static constexpr uint8_t MAX_VALUE_LEN = ACLI_MAX_VALUE_LEN;

// Maximum length (including null terminator) of a command description.
static constexpr uint8_t MAX_DESC_LEN = ACLI_MAX_DESC_LEN;

// Maximum length of the raw input line passed to `AdvancedCLI::parse()`.
static constexpr uint16_t MAX_INPUT_LEN = ACLI_MAX_INPUT_LEN;

// Maximum number of aliases per argument.
static constexpr uint8_t MAX_ALIASES = ACLI_MAX_ALIASES;

// Maximum number of tokens the parser splits an input line into.
// A command with N named arguments produces at most 1 (command/subcommand name) + N*2
// tokens (alternating -name / value). Set per-platform to cover the widest expected
// command; override via ACLI_MAX_TOKENS if your commands have more arguments.
static constexpr uint8_t MAX_TOKENS = ACLI_MAX_TOKENS;

// Maximum length (including null terminator) of each individual token.
static constexpr uint8_t MAX_TOKEN_LEN = ACLI_MAX_TOKEN_LEN;

// Validation function enabled for arguments
static constexpr bool VALIDATION_FN_ENABLED = ACLI_ENABLE_VALIDATION_FN;

// Invalid-value callback enabled for arguments
static constexpr bool INVALID_FN_ENABLED = ACLI_ENABLE_INVALID_FN;

} // namespace Config

// Forward declaration required by the type aliases below.
class Command;

/**
 * @brief Output sink callback. Receives each null-terminated line of library output. Passed to
 * `AdvancedCLI::setOutput()`.
 */
#if ACLI_USE_STD_FUNCTION
using OutputFn = std::function<void(const char*)>;
#else
using OutputFn = void (*)(const char*);
#endif

/**
 * @brief Command execution callback. Invoked when a command is successfully parsed. Passed to
 * `Command::onExecute()`.
 * @param cmd Reference to the matched command. Use `getArg()` to read argument values.
 */
#if ACLI_USE_STD_FUNCTION
using CallbackFn = std::function<void(Command&)>;
#else
using CallbackFn = void (*)(Command&);
#endif

/**
 * @brief Per-argument validation callback. Registered via `Arg*::setValidator()`.
 *
 * @tparam T Native argument type: `int32_t`, `float`, or `const char*`.
 * @param value Parsed argument value in its native type.
 * @return `true` to accept the value; `false` to reject it.
 */
#if ACLI_USE_STD_FUNCTION
template <typename T>
using ValidationFn = std::function<bool(const T value)>;
#else
template <typename T>
using ValidationFn = bool (*)(const T value);
#endif

/**
 * @brief Per-argument invalid-value notification callback. Registered via `Arg*::onInvalid()`.
 *
 * Replaces the default error output when an argument fails validation.
 *
 * @param arg_name Name of the argument that failed.
 * @param value Rejected value as a string.
 * @param reason Human-readable reason. Empty when rejected by a user validator.
 */
#if ACLI_USE_STD_FUNCTION
using InvalidFn = std::function<void(const char* arg_name, const char* value, const char* reason)>;
#else
using InvalidFn = void (*)(const char* arg_name, const char* value, const char* reason);
#endif

/**
 * @brief Command-level error callback. Registered with `Command::onError()`.
 *
 * Called when parsing or execution of a command fails, or when the callback calls
 * `Command::fail()`. Overrides the default error output to the sink.
 *
 * @param cmd The command that failed.
 * @param message Human-readable error description.
 */
#if ACLI_USE_STD_FUNCTION
using ErrorFn = std::function<void(Command&, const char* message)>;
#else
using ErrorFn = void (*)(Command&, const char* message);
#endif

/**
 * @brief Handler for unknown commands. Registered with `AdvancedCLI::onUnknownCommand()`.
 *
 * Called when the first token of the input does not match any registered top-level command.
 *
 * @param cmd_name The unrecognised command name.
 */
#if ACLI_USE_STD_FUNCTION
using UnknownCommandFn = std::function<void(const char* cmd_name)>;
#else
using UnknownCommandFn = void (*)(const char* cmd_name);
#endif

} // namespace ACLI
