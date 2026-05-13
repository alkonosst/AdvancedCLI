/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "acli-argument.h"
#include "acli-command.h"
#include "acli-utils.h"

#include <stdlib.h>

namespace ACLI {
using namespace detail;

/* ----------------------------------------- ArgBaseImpl ---------------------------------------- */

ArgBaseImpl::ArgBaseImpl(Command* cmd, int8_t arg_index)
    : _cmd(cmd)
    , _arg_index(arg_index) {}

bool ArgBaseImpl::isSet() const {
  const ParsedArg* p = _parsed();
  return p && p->is_set;
}

bool ArgBaseImpl::isValid() const { return _cmd != nullptr && _arg_index >= 0; }

ArgBaseImpl::operator bool() const { return isValid(); }

ArgDef* ArgBaseImpl::_def() const {
  if (!_cmd || !_cmd->_arg_defs || _arg_index < 0 || _arg_index >= _cmd->_arg_count) return nullptr;
  return &_cmd->_arg_defs[_arg_index];
}

ParsedArg* ArgBaseImpl::_parsed() const {
  if (!_cmd || !_cmd->_parsed || _arg_index < 0 || _arg_index >= _cmd->_arg_count) return nullptr;
  return &_cmd->_parsed[_arg_index];
}

/* -------------------------------------- ArgBase<Derived> -------------------------------------- */

template <typename Derived>
Derived& ArgBase<Derived>::setAlias(const char* alias_name) {
  ArgDef* arg_def = _def();
  if (!arg_def || !alias_name) return static_cast<Derived&>(*this);
  if (arg_def->alias_count >= Config::MAX_ALIASES) return static_cast<Derived&>(*this);
  arg_def->aliases[arg_def->alias_count] =
    alias_name; // zero-copy: caller must pass a string literal
  ++arg_def->alias_count;
  return static_cast<Derived&>(*this);
}

template <typename Derived>
Derived& ArgBase<Derived>::setDescription(const char* description) {
  ArgDef* arg_def = _def();
  if (arg_def) arg_def->description = description; // zero-copy: caller must pass a string literal
  return static_cast<Derived&>(*this);
}

template <typename Derived>
Derived& ArgBase<Derived>::setRequired() {
  ArgDef* arg_def = _def();
  if (arg_def) arg_def->is_required = true;
  return static_cast<Derived&>(*this);
}

#if ACLI_ENABLE_INVALID_FN
template <typename Derived>
Derived& ArgBase<Derived>::onInvalid(InvalidFn fn) {
  ArgDef* arg_def = _def();
  if (arg_def) arg_def->on_invalid_fn = fn;
  return static_cast<Derived&>(*this);
}
#endif

// Explicit instantiations - one per public handle type.
template class ArgBase<ArgStr>;
template class ArgBase<ArgFlag>;
template class ArgBase<ArgInt>;
template class ArgBase<ArgFloat>;

/* ---------------------------------------- ArgReaderBase --------------------------------------- */

ArgReaderBase::ArgReaderBase(Command* cmd, int8_t arg_index)
    : _cmd(cmd)
    , _arg_index(arg_index) {}

bool ArgReaderBase::isSet() const {
  const ParsedArg* p = _parsed();
  return p && p->is_set;
}

bool ArgReaderBase::isValid() const { return _cmd != nullptr && _arg_index >= 0; }

ArgReaderBase::operator bool() const { return isValid(); }

const ArgDef* ArgReaderBase::_def() const {
  if (!_cmd || !_cmd->_arg_defs || _arg_index < 0 || _arg_index >= _cmd->_arg_count) return nullptr;
  return &_cmd->_arg_defs[_arg_index];
}

const ParsedArg* ArgReaderBase::_parsed() const {
  if (!_cmd || !_cmd->_parsed || _arg_index < 0 || _arg_index >= _cmd->_arg_count) return nullptr;
  return &_cmd->_parsed[_arg_index];
}

const char* ArgReaderBase::_rawValue() const {
  static char buf[24]; // safe: single-threaded, not called re-entrantly
  return resolveValue(_parsed(), _def(), buf, sizeof(buf));
}

/* ------------------------------------------- ArgStr ------------------------------------------- */

ArgStr::ArgStr(Command* cmd, int8_t arg_index)
    : detail::ArgBase<ArgStr>(cmd, arg_index) {}

#if ACLI_ENABLE_VALIDATION_FN
ArgStr& ArgStr::setValidator(ValidationFn<const char*> fn) {
  ArgDef* arg_def = _def();
  if (!arg_def || !fn) return *this;
#  if ACLI_USE_STD_FUNCTION
  arg_def->validation_fn = [fn](const char* raw_value) { return fn(raw_value); };
#  else
  arg_def->validation_fn.fn_s = fn;
#  endif
  return *this;
}
#endif

/* ------------------------------------------- ArgFlag ------------------------------------------ */

ArgFlag::ArgFlag(Command* cmd, int8_t arg_index)
    : detail::ArgBase<ArgFlag>(cmd, arg_index) {}

/* ------------------------------------------- ArgInt ------------------------------------------- */

ArgInt::ArgInt(Command* cmd, int8_t arg_index)
    : detail::ArgBase<ArgInt>(cmd, arg_index) {}

#if ACLI_ENABLE_VALIDATION_FN
ArgInt& ArgInt::setValidator(ValidationFn<int32_t> fn) {
  ArgDef* arg_def = _def();
  if (!arg_def || !fn) return *this;
#  if ACLI_USE_STD_FUNCTION
  arg_def->validation_fn = [fn](const char* raw_value) {
    char* parse_end = nullptr;
    long parsed_val = strtol(raw_value, &parse_end, 0);
    if (!parse_end || parse_end == raw_value || *parse_end != '\0') return false;
    return fn(static_cast<int32_t>(parsed_val));
  };
#  else
  arg_def->validation_fn.fn_i = fn;
#  endif
  return *this;
}
#endif

/* ------------------------------------------ ArgFloat ------------------------------------------ */

ArgFloat::ArgFloat(Command* cmd, int8_t arg_index)
    : detail::ArgBase<ArgFloat>(cmd, arg_index) {}

#if ACLI_ENABLE_VALIDATION_FN
ArgFloat& ArgFloat::setValidator(ValidationFn<float> fn) {
  ArgDef* arg_def = _def();
  if (!arg_def || !fn) return *this;
#  if ACLI_USE_STD_FUNCTION
  arg_def->validation_fn = [fn](const char* raw_value) {
    char* parse_end  = nullptr;
    float parsed_val = static_cast<float>(strtod(raw_value, &parse_end));
    if (!parse_end || parse_end == raw_value || *parse_end != '\0') return false;
    return fn(parsed_val);
  };
#  else
  arg_def->validation_fn.fn_f = fn;
#  endif
  return *this;
}
#endif

/* ------------------------------------------ ParsedAny ----------------------------------------- */

ParsedAny::ParsedAny(Command* cmd, int8_t arg_index)
    : detail::ArgReaderBase(cmd, arg_index) {}

ParsedAny::ParsedAny(const detail::ArgReaderBase& base)
    : detail::ArgReaderBase(base) {}

const char* ParsedAny::getValue() const { return _rawValue(); }

/* ----------------------------------------- ParsedFlag ----------------------------------------- */

ParsedFlag::ParsedFlag(Command* cmd, int8_t arg_index)
    : detail::ArgReaderBase(cmd, arg_index) {}

/* ------------------------------------------ ParsedInt ----------------------------------------- */

ParsedInt::ParsedInt(Command* cmd, int8_t arg_index)
    : detail::ArgReaderBase(cmd, arg_index) {}

int32_t ParsedInt::getValue(int32_t default_value) const {
  const char* raw_value = _rawValue();
  if (!raw_value || raw_value[0] == '\0') return default_value;
  return static_cast<int32_t>(strtol(raw_value, nullptr, 0));
}

/* ----------------------------------------- ParsedFloat ---------------------------------------- */

ParsedFloat::ParsedFloat(Command* cmd, int8_t arg_index)
    : detail::ArgReaderBase(cmd, arg_index) {}

float ParsedFloat::getValue(float default_value) const {
  const char* raw_value = _rawValue();
  if (!raw_value || raw_value[0] == '\0') return default_value;
  return static_cast<float>(strtod(raw_value, nullptr));
}

/* ------------------------------------------ ParsedStr ----------------------------------------- */

ParsedStr::ParsedStr(Command* cmd, int8_t arg_index)
    : detail::ArgReaderBase(cmd, arg_index) {}

const char* ParsedStr::getValue() const { return _rawValue(); }

} // namespace ACLI
