/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "acli-argument.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ACLI {
namespace detail {

// String comparison Case Insensitive
inline bool strEqualCI(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
    if (ca != cb) return false;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

inline bool strEqual(const char* a, const char* b, bool case_sensitive) {
  return case_sensitive ? (strcmp(a, b) == 0) : strEqualCI(a, b);
}

// Argument name matching
// Strips leading '-' from token, then compares against name + aliases.
inline bool matchArgName(const ArgDef& arg_def, const char* token, bool case_sensitive) {
  if (!token) return false;
  while (*token == '-')
    ++token;
  if (*token == '\0') return false;

  if (strEqual(arg_def.name, token, case_sensitive)) return true;
  for (uint8_t i = 0; i < arg_def.alias_count; ++i) {
    if (arg_def.aliases[i] && strEqual(arg_def.aliases[i], token, case_sensitive)) return true;
  }
  return false;
}

// Default value formatting
// Resolves the effective string value for a parsed argument:
// - returns parsed->token when the arg was explicitly provided
// - formats Int/Float defaults into buf (caller must supply >= 24 bytes)
// - returns the str pointer for Any defaults (no buffer needed)
// - returns "" when no value and no default
inline const char* resolveValue(const ParsedArg* parsed, const ArgDef* arg_def, char* buf,
  size_t buf_size) {
  if (parsed && parsed->is_set && parsed->token) return parsed->token;
  if (!arg_def || !arg_def->has_default) return "";

  switch (arg_def->value_type) {
    case ArgValueType::Int:
      snprintf(buf, buf_size, "%" PRId32, arg_def->default_value.i);
      return buf;

    case ArgValueType::Float:
      snprintf(buf, buf_size, "%g", static_cast<double>(arg_def->default_value.f));
      return buf;

    default: // Any: zero-copy string literal pointer
      return arg_def->default_value.str ? arg_def->default_value.str : "";
  }
}

// Validator helpers
// hasValidator: returns true if a user validator was registered for this arg.
// callValidator: invokes the validator with the correct typed value; returns true = valid.
// Compiled out entirely when ACLI_ENABLE_VALIDATION_FN=0.
#if ACLI_ENABLE_VALIDATION_FN
inline bool hasValidator(const ArgDef& d) {
#  if ACLI_USE_STD_FUNCTION
  return (d.validation_fn != nullptr);
#  else
  switch (d.value_type) {
    case ArgValueType::Int: return d.validation_fn.fn_i != nullptr;
    case ArgValueType::Float: return d.validation_fn.fn_f != nullptr;
    default: return d.validation_fn.fn_s != nullptr;
  }
#  endif
}

inline bool callValidator(const ArgDef& d, const char* pv) {
#  if ACLI_USE_STD_FUNCTION
  // Type-erased: lambda already parses pv to the right type internally.
  return d.validation_fn(pv);
#  else
  switch (d.value_type) {
    case ArgValueType::Int:
      return d.validation_fn.fn_i(static_cast<int32_t>(strtol(pv, nullptr, 0)));
    case ArgValueType::Float: return d.validation_fn.fn_f(static_cast<float>(strtod(pv, nullptr)));
    default: return d.validation_fn.fn_s(pv);
  }
#  endif
}
#endif

// Returns true if token is a negative number literal (-5, -3.14, -.5).
inline bool isNumToken(const char* token) {
  return token[0] == '-' && (token[1] == '.' || (token[1] >= '0' && token[1] <= '9'));
}

// Returns true if tokens[t+1] is a value token (not a flag name).
inline bool isNextTokenValue(uint8_t t, uint8_t count, const char tokens[][Config::MAX_TOKEN_LEN]) {
  if (t + 1 >= count) return false;
  const char* next_token = tokens[t + 1];
  return next_token[0] != '-' || isNumToken(next_token);
}

// Clamps write_pos to [0, buf_size-1] to prevent snprintf return-value overflow.
inline void clampWritePos(int& write_pos, size_t buf_size) {
  if (write_pos >= static_cast<int>(buf_size)) write_pos = static_cast<int>(buf_size) - 1;
}

} // namespace detail
} // namespace ACLI
