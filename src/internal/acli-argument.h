/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "acli-config.h"

namespace ACLI {

// Forward declarations - full definitions in their respective headers.
class Command;
class AdvancedCLI;

// Internal implementation types. Do NOT use anything in this namespace directly; it is not part of
// the public API and may change without notice.
namespace detail {

// How the parser represents argument types internally.
enum class ArgType : uint8_t {
  Named,     // -name value or -name value
  Flag,      // -flag (presence means true, absence means false)
  Positional // value (matched by position, not by name)
};

// Internal tag stored in ArgDef, the typed handle classes set it.
enum class ArgValueType : uint8_t {
  Any,  // Accept as-is (ArgStr / ArgString)
  Int,  // Must parse as whole integer (ArgInt)
  Float // Must parse as floating-point (ArgFloat)
};

// Describes a single argument as registered by the user.
struct ArgDef {
  const char* name                         = nullptr;
  const char* aliases[Config::MAX_ALIASES] = {};
  uint8_t alias_count                      = 0;

  // Default value is stored as a union of possible types, but only one is active based on
  // value_type.
  union DefaultVal {
    const char* str;
    int32_t i;
    float f;
  } default_value = {};

  const char* description = nullptr;
  ArgType type            = ArgType::Named;
  bool is_required        = false;
  bool has_default        = false;
  bool is_persistent      = false; // parsed before sub-command token (e.g. "joy -n 2 cal")

  // --- Value validation ---
  ArgValueType value_type = ArgValueType::Any; // Int or Float enables type checking

  // Internal validator: wraps the user's typed ValidationFn<T>.
  // Set by ArgStr/ArgInt/ArgFloat::setValidator().
  // Compiled out entirely when ACLI_ENABLE_VALIDATION_FN=0.
#if ACLI_ENABLE_VALIDATION_FN
#  if ACLI_USE_STD_FUNCTION
  std::function<bool(const char*)> validation_fn = {};
#  else
  // Only one member is ever active; which one is determined by value_type.
  union ValidationFnUnion {
    bool (*fn_i)(const int32_t);
    bool (*fn_f)(const float);
    bool (*fn_s)(const char*);
  } validation_fn = {};
#  endif
#endif

  // Per-argument invalid-value callback. Compiled out when ACLI_ENABLE_INVALID_FN=0.
#if ACLI_ENABLE_INVALID_FN
  InvalidFn on_invalid_fn = {}; // called instead of default error when set
#endif
};

// After parsing, the parser fills in a parallel array of these with the actual values for each
// argument.
struct ParsedArg {
  const ArgDef* def = nullptr;
  const char* token = nullptr;
  bool is_set       = false;
};

// Non-template base for all builder-time argument handles.
// Holds data, friend declarations, and the non-typed query methods.
// Users work with the typed subclasses: ArgFlag, ArgStr, ArgInt, ArgFloat.
class ArgBaseImpl {
  public:
  ArgBaseImpl() = default;
  ArgBaseImpl(Command* cmd, int8_t arg_index);

  /**
   * @brief Verify if this argument was set in the input. For flags, this means the flag was
   * present; for named/positional args, this means the argument was provided (or has a default
   * value).
   * @return `true` if the argument was set, `false` otherwise.
   */
  bool isSet() const;

  /**
   * @brief Verify if this handle is valid (was properly returned by an `add*Arg()` method).
   * @return `true` if valid; `false` if this is an uninitialized dummy handle.
   */
  bool isValid() const;

  /** @brief Equivalent to `isValid()`. Allows use in boolean contexts. */
  operator bool() const;

  protected:
  Command* _cmd     = nullptr;
  int8_t _arg_index = -1;

  ArgDef* _def() const;
  ParsedArg* _parsed() const;

  friend class ::ACLI::Command;
  friend class ::ACLI::AdvancedCLI;
};

// CRTP mixin: provides the fluent builder API, returning the correct derived type on each call.
// All public argument handle types (ArgStr, ArgInt, ArgFloat, ArgFlag) inherit from this.
// Method bodies are defined in acli-argument.cpp and explicitly instantiated for each handle type.
template <typename Derived>
class ArgBase : public ArgBaseImpl {
  public:
  ArgBase() = default;
  ArgBase(Command* cmd, int8_t arg_index)
      : ArgBaseImpl(cmd, arg_index) {}

  /**
   * @brief Add an alias for this argument (e.g. "v" for a "verbose" arg).
   * @param alias_name Alias string without the leading dash.
   * @return Reference to `*this` for chaining.
   */
  Derived& setAlias(const char* alias_name);

  /**
   * @brief Set a human-readable description shown by `AdvancedCLI::printHelp()`.
   * @param description Null-terminated string literal (zero-copy).
   * @return Reference to `*this` for chaining.
   */
  Derived& setDescription(const char* description);

  /**
   * @brief Mark this argument as required. Parsing fails with an error if the argument is absent
   * from the input.
   * @return Reference to `*this` for chaining.
   */
  Derived& setRequired();

#if ACLI_ENABLE_INVALID_FN
  /**
   * @brief Register a callback invoked when this argument fails validation. Takes priority over the
   * command-level `onError()` handler.
   * @param fn Callback of type `InvalidFn`.
   * @return Reference to `*this` for chaining.
   */
  Derived& onInvalid(InvalidFn fn);
#endif
};

// Internal base for read-only callback-time handles.
// Users work with the typed reader classes: ParsedFlag, ParsedInt, ParsedFloat, ParsedStr.
class ArgReaderBase {
  public:
  ArgReaderBase() = default;
  ArgReaderBase(Command* cmd, int8_t arg_index);

  /**
   * @brief Verify if this argument was set in the input. For flags, this means the flag was
   * present; for named/positional args, this means the argument was provided (or has a default
   * value).
   * @return `true` if the argument was set, `false` otherwise.
   */
  bool isSet() const;

  /**
   * @brief Verify if this handle is valid (was properly returned by an `add*Arg()` method).
   * @return `true` if valid; `false` if this is an uninitialized dummy handle.
   */
  bool isValid() const;

  /** @brief Equivalent to `isValid()`. Allows use in boolean contexts. */
  operator bool() const;

  protected:
  Command* _cmd     = nullptr;
  int8_t _arg_index = -1;

  const ArgDef* _def() const;
  const ParsedArg* _parsed() const;
  const char* _rawValue() const; // token or default or ""

  friend class ::ACLI::Command;
  friend class ::ACLI::AdvancedCLI;
};

// Maps a builder type to its reader type for Command::getArg(). Specializations are defined after
// the public reader types are declared below.
template <typename T>
struct ReaderOf; // deliberately undefined for unknown types

} // namespace detail

/**
 * @brief Builder handle for a named string argument.
 *
 * Returned by `Command::addArg()` or `Command::addPosArg()`. Chain builder methods at registration
 * time. Inside the execution callback, call `Command::getArg()` to obtain a `ParsedStr` reader.
 */
class ArgStr : public detail::ArgBase<ArgStr> {
  public:
  ArgStr() = default;
  ArgStr(Command* cmd, int8_t arg_index);

#if ACLI_ENABLE_VALIDATION_FN
  /**
   * @brief Register a string validator function.
   * @param fn Receives the parsed `const char*` value. Returns `true` to accept; `false` to reject.
   * @return `ArgStr&` Reference to this for chaining.
   */
  ArgStr& setValidator(ValidationFn<const char*> fn);
#endif
};

/**
 * @brief Builder handle for a boolean flag argument.
 *
 * Returned by Command::addFlag(). The flag evaluates to `true` when present in the input and
 * `false` when absent. Chain builder methods at registration time. Inside the execution callback,
 * call Command::getArg() to obtain a `ParsedFlag` reader.
 */
class ArgFlag : public detail::ArgBase<ArgFlag> {
  public:
  ArgFlag() = default;
  ArgFlag(Command* cmd, int8_t arg_index);
};

/**
 * @brief Builder handle for a named integer argument.
 *
 * Returned by `Command::addIntArg()` or `Command::addPosIntArg()`. The parser enforces that the
 * provided value is a valid integer. Chain builder methods at registration time. Inside the
 * execution callback, call `Command::getArg()` to obtain a `ParsedInt` reader.
 */
class ArgInt : public detail::ArgBase<ArgInt> {
  public:
  ArgInt() = default;
  ArgInt(Command* cmd, int8_t arg_index);

#if ACLI_ENABLE_VALIDATION_FN
  /**
   * @brief Register an integer validator function.
   * @param fn Receives the parsed `int32_t` value. Returns `true` to accept; `false` to reject.
   * @return `ArgInt&` Reference to this for chaining.
   */
  ArgInt& setValidator(ValidationFn<int32_t> fn);
#endif
};

/**
 * @brief Builder handle for a named floating-point argument.
 *
 * Returned by `Command::addFloatArg()` or `Command::addPosFloatArg()`. The parser enforces that the
 * provided value is a valid number. Chain builder methods at registration time. Inside the
 * execution callback, call `Command::getArg()` to obtain a `ParsedFloat` reader.
 */
class ArgFloat : public detail::ArgBase<ArgFloat> {
  public:
  ArgFloat() = default;
  ArgFloat(Command* cmd, int8_t arg_index);

#if ACLI_ENABLE_VALIDATION_FN
  /**
   * @brief Register a float validator function.
   * @param fn Receives the parsed `float` value. Returns `true` to accept; `false` to reject.
   * @return `ArgFloat&` Reference to this for chaining.
   */
  ArgFloat& setValidator(ValidationFn<float> fn);
#endif
};

/**
 * @brief Generic parsed argument reader. Returned by `Command::getArgByName()`.
 *
 * Provides access to any argument regardless of its type. The value is always returned as a raw
 * string token, the registered default, or an empty string.
 */
class ParsedAny : public detail::ArgReaderBase {
  public:
  ParsedAny() = default;
  ParsedAny(Command* cmd, int8_t arg_index);

  /** @brief Implicit conversion from any typed reader (`ParsedFlag`, `ParsedInt`, etc.). */
  ParsedAny(const detail::ArgReaderBase& base);

  /**
   * @brief Returns the raw token string, the registered default, or "" if not set.
   * @return `const char*` Argument value as a string.
   */
  const char* getValue() const;
};

/**
 * @brief Parsed boolean flag argument. Returned by `Command::getArg()` for `ArgFlag` handles.
 */
class ParsedFlag : public detail::ArgReaderBase {
  public:
  ParsedFlag() = default;
  ParsedFlag(Command* cmd, int8_t arg_index);
};

/**
 * @brief Parsed integer argument. Returned by `Command::getArg()` for `ArgInt` handles.
 */
class ParsedInt : public detail::ArgReaderBase {
  public:
  ParsedInt() = default;
  ParsedInt(Command* cmd, int8_t arg_index);

  /**
   * @brief Returns the parsed integer value.
   * @param default_value Fallback used when the argument was not provided and has no registered
   * default.
   * @return Parsed `int32_t` value, the registered default, or `default_value`.
   */
  int32_t getValue(int32_t default_value = 0) const;
};

/**
 * @brief Parsed floating-point argument. Returned by `Command::getArg()` for `ArgFloat` handles.
 */
class ParsedFloat : public detail::ArgReaderBase {
  public:
  ParsedFloat() = default;
  ParsedFloat(Command* cmd, int8_t arg_index);

  /**
   * @brief Returns the parsed float value.
   * @param default_value  Fallback used when the argument was not provided and has no registered
   * default.
   * @return Parsed `float` value, the registered default, or `default_value`.
   */
  float getValue(float default_value = 0.f) const;
};

/**
 * @brief Parsed string argument. Returned by `Command::getArg()` for `ArgStr` handles.
 */
class ParsedStr : public detail::ArgReaderBase {
  public:
  ParsedStr() = default;
  ParsedStr(Command* cmd, int8_t arg_index);

  /**
   * @brief Returns the argument value as a string, the registered default, or "".
   */
  const char* getValue() const;
};

// ReaderOf<T> specializations. Live in detail, reference public types above.
namespace detail {
template <>
struct ReaderOf<ArgInt> {
  using type = ParsedInt;
};

template <>
struct ReaderOf<ArgFloat> {
  using type = ParsedFloat;
};

template <>
struct ReaderOf<ArgFlag> {
  using type = ParsedFlag;
};

template <>
struct ReaderOf<ArgStr> {
  using type = ParsedStr;
};
} // namespace detail

} // namespace ACLI
