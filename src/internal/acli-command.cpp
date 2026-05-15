/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "acli-command.h"
#include "AdvancedCLI.h"
#include "acli-utils.h"

#include <string.h>

namespace ACLI {
using namespace detail;

/* ----------------------------------------- Builder API ---------------------------------------- */

Command& Command::setDescription(const char* description) {
  _description = description; // zero-copy: caller must pass a string literal
  return *this;
}

ArgStr Command::addArg(const char* name, const char* default_value) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Any);
  if (idx < 0) return ArgStr();
  if (default_value) {
    _arg_defs[idx].default_value.str = default_value;
    _arg_defs[idx].has_default       = true;
  }
  return ArgStr(this, idx);
}

ArgFlag Command::addFlag(const char* name) {
  int16_t idx = _addArgInternal(name, ArgType::Flag, ArgValueType::Any);
  if (idx < 0) return ArgFlag();
  return ArgFlag(this, idx);
}

ArgInt Command::addIntArg(const char* name) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Int);
  if (idx < 0) return ArgInt();
  return ArgInt(this, idx);
}

ArgInt Command::addIntArg(const char* name, int32_t default_value) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Int);
  if (idx < 0) return ArgInt();
  _arg_defs[idx].default_value.i = default_value;
  _arg_defs[idx].has_default     = true;
  return ArgInt(this, idx);
}

ArgFloat Command::addFloatArg(const char* name) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Float);
  if (idx < 0) return ArgFloat();
  return ArgFloat(this, idx);
}

ArgFloat Command::addFloatArg(const char* name, float default_value) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Float);
  if (idx < 0) return ArgFloat();
  _arg_defs[idx].default_value.f = default_value;
  _arg_defs[idx].has_default     = true;
  return ArgFloat(this, idx);
}

ArgStr Command::addPosArg(const char* name, const char* default_value) {
  int16_t idx = _addArgInternal(name, ArgType::Positional, ArgValueType::Any);
  if (idx < 0) return ArgStr();
  if (default_value) {
    _arg_defs[idx].default_value.str = default_value;
    _arg_defs[idx].has_default       = true;
  }
  return ArgStr(this, idx);
}

ArgInt Command::addPosIntArg(const char* name) {
  int16_t idx = _addArgInternal(name, ArgType::Positional, ArgValueType::Int);
  if (idx < 0) return ArgInt();
  return ArgInt(this, idx);
}

ArgFloat Command::addPosFloatArg(const char* name) {
  int16_t idx = _addArgInternal(name, ArgType::Positional, ArgValueType::Float);
  if (idx < 0) return ArgFloat();
  return ArgFloat(this, idx);
}

ArgStr Command::addPersistentArg(const char* name, const char* default_value) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Any);
  if (idx < 0) return ArgStr();
  _arg_defs[idx].is_persistent = true;
  if (default_value) {
    _arg_defs[idx].default_value.str = default_value;
    _arg_defs[idx].has_default       = true;
  }
  return ArgStr(this, idx);
}

ArgFlag Command::addPersistentFlag(const char* name) {
  int16_t idx = _addArgInternal(name, ArgType::Flag, ArgValueType::Any);
  if (idx < 0) return ArgFlag();
  _arg_defs[idx].is_persistent = true;
  return ArgFlag(this, idx);
}

ArgInt Command::addPersistentIntArg(const char* name) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Int);
  if (idx < 0) return ArgInt();
  _arg_defs[idx].is_persistent = true;
  return ArgInt(this, idx);
}

ArgInt Command::addPersistentIntArg(const char* name, int32_t default_value) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Int);
  if (idx < 0) return ArgInt();
  _arg_defs[idx].is_persistent   = true;
  _arg_defs[idx].default_value.i = default_value;
  _arg_defs[idx].has_default     = true;
  return ArgInt(this, idx);
}

ArgFloat Command::addPersistentFloatArg(const char* name) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Float);
  if (idx < 0) return ArgFloat();
  _arg_defs[idx].is_persistent = true;
  return ArgFloat(this, idx);
}

ArgFloat Command::addPersistentFloatArg(const char* name, float default_value) {
  int16_t idx = _addArgInternal(name, ArgType::Named, ArgValueType::Float);
  if (idx < 0) return ArgFloat();
  _arg_defs[idx].is_persistent   = true;
  _arg_defs[idx].default_value.f = default_value;
  _arg_defs[idx].has_default     = true;
  return ArgFloat(this, idx);
}

Command& Command::onExecute(CallbackFn cb) {
  _callback = cb;
  return *this;
}

Command& Command::onError(ErrorFn cb) {
  _error_callback = cb;
  return *this;
}

/* -------------------------------------- Runtime accessors ------------------------------------- */

ParsedAny Command::getArgByName(const char* name) {
  if (!name) return ParsedAny();

  bool case_sensitive = _owner ? _owner->_case_sensitive : false;
  for (uint16_t i = 0; i < _arg_count; ++i) {
    if (matchArgName(_arg_defs[i], name, case_sensitive)) {
      return ParsedAny(this, i);
    }
  }

  // Fall back to parent's persistent args when this is a sub-command
  if (_parent_idx >= 0 && _owner) {
    Command& parent = _owner->_commands[_parent_idx];
    for (uint16_t i = 0; i < parent._arg_count; ++i) {
      if (!parent._arg_defs[i].is_persistent) continue;
      if (matchArgName(parent._arg_defs[i], name, case_sensitive)) {
        return ParsedAny(&parent, i);
      }
    }
  }

  return ParsedAny();
}

/* ---------------------------------------- Sub-commands ---------------------------------------- */

Command& Command::addSubCommand(const char* name) {
  if (!_owner) return *this; // unregistered dummy command
  _args_sealed = true;       // parent can no longer accept new arg registrations
  return _owner->_addCommandInternal(name, _self_idx);
}

bool Command::isSubCommand() const { return _parent_idx >= 0; }

void Command::fail(const char* message) {
  if (!_owner) return;
  _owner->_fireError(*this, message ? message : "");
}

/* ------------------------------------------ Accessors ----------------------------------------- */

const char* Command::getName() const { return _name ? _name : ""; }
const char* Command::getDescription() const { return _description ? _description : ""; }
bool Command::isValid() const { return _name != nullptr; }
uint16_t Command::getArgCount() const { return _arg_count; }

uint16_t Command::getParsedArgCount() const {
  if (!_parsed) return 0;
  uint16_t count = 0;
  for (uint16_t i = 0; i < _arg_count; ++i) {
    if (_parsed[i].is_set) ++count;
  }
  return count;
}

/* --------------------------------------- Private methods -------------------------------------- */

void Command::_init(const char* name, AdvancedCLI* owner, int16_t self_idx, int16_t parent_idx) {
  _name       = name;
  _owner      = owner;
  _self_idx   = self_idx;
  _parent_idx = parent_idx;
}

void Command::_resetParsed() {
  if (!_arg_defs || !_parsed) return;
  for (uint16_t i = 0; i < _arg_count; ++i) {
    _parsed[i].def    = &_arg_defs[i];
    _parsed[i].is_set = false;
    _parsed[i].token  = nullptr;
  }
}

void Command::_execute() {
  if (_callback) _callback(*this);
}

int16_t Command::_findArgDefByName(const char* token) const {
  if (!token) return -1;

  bool case_sensitive = _owner ? _owner->_case_sensitive : false;
  for (uint16_t i = 0; i < _arg_count; ++i) {
    if (_arg_defs[i].type == ArgType::Positional) continue;
    if (matchArgName(_arg_defs[i], token, case_sensitive)) return static_cast<int16_t>(i);
  }
  return -1;
}

int16_t Command::_findPersistentArgDefByName(const char* token) const {
  int16_t idx = _findArgDefByName(token);
  if (idx < 0) return -1;
  return _arg_defs[idx].is_persistent ? idx : -1;
}

Command* Command::_getParent() const {
  if (_parent_idx < 0 || !_owner) return nullptr;
  return &_owner->_commands[_parent_idx];
}

int16_t Command::_positionalArgIndex(int16_t pos_idx) const {
  int16_t count = 0;
  for (int16_t i = 0; i < _arg_count; ++i) {
    if (_arg_defs[i].type == ArgType::Positional) {
      if (count == pos_idx) return i;
      ++count;
    }
  }
  return -1;
}

int16_t Command::_addArgInternal(const char* name, ArgType type, ArgValueType value_type) {
  if (!_owner || !name) return -1;

  // Sealed commands have already had sub-commands registered; adding args now would produce pool
  // overlap (the sub-command's _arg_defs pointer was set to the same pool offset).
  if (_args_sealed) {
    _owner->_overflow = true;
    return -1;
  }

  // Contiguity guard: all args for this command must be registered before any sibling or child
  // command is registered. If this command's "tail" in the pool no longer aligns with the current
  // pool end, another command was registered in between. Reject to avoid overlap.
  if (_arg_pool_start + _arg_count != _owner->_arg_pool_used) return -1;

  // Global pool overflow check.
  if (_owner->_arg_pool_used >= Config::MAX_ARGS_TOTAL) {
    _owner->_overflow = true;
    return -1;
  }

  // Detect duplicate argument names at registration time (debug guard).
  for (uint16_t i = 0; i < _arg_count; ++i) {
    if (_arg_defs[i].name && strcmp(_arg_defs[i].name, name) == 0) return -1;
  }

  int16_t new_idx = static_cast<int16_t>(_arg_count++);
  ++_owner->_arg_pool_used; // claim exactly one slot from the shared pool

  ArgDef& arg_def = _arg_defs[new_idx]; // already zero-initialised at AdvancedCLI construction

  arg_def.name       = name; // zero-copy: caller must pass a string literal
  arg_def.type       = type;
  arg_def.value_type = value_type;

  return new_idx;
}

} // namespace ACLI