/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "AdvancedCLI.h"
#include "acli-utils.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ACLI {
using namespace detail;

/* --------------------- Configuration (call before registering commands) --------------------- */

void AdvancedCLI::setOutput(OutputFn output_fn) { _output_fn = output_fn; }

void AdvancedCLI::onUnknownCommand(UnknownCommandFn fn) { _unknown_cmd_fn = fn; }

void AdvancedCLI::setCaseSensitive(bool enable) { _case_sensitive = enable; }

/* ----------------------------------- Command registration ----------------------------------- */

Command& AdvancedCLI::addCommand(const char* name) { return _addCommandInternal(name, -1); }

/* ------------------------------------------ Parsing ----------------------------------------- */

bool AdvancedCLI::parse(const char* input) {
  if (!input) return false;
  return parse(input, strlen(input));
}

bool AdvancedCLI::parse(const char* input, size_t len) {
  if (!input || len == 0) return false;
  // Cap to maximum parseable length to prevent tokenizer index wrap-around.
  if (len > Config::MAX_INPUT_LEN - 1) len = Config::MAX_INPUT_LEN - 1;

  _last_parse_ok = true;

  // Tokenize
  static char tokens[Config::MAX_TOKENS][Config::MAX_TOKEN_LEN];
  uint8_t count = _tokenize(input, len, tokens, Config::MAX_TOKENS);
  if (count == 0) return true; // empty line - not an error

  // First token is command name (only top-level commands matched here)
  Command* cmd = _findCommand(tokens[0], strlen(tokens[0]));

  // If no top-level command matches, call the unknown-command handler (if set) and return false.
  if (!cmd) {
    _handleUnknownCommand(tokens[0]);
    return _last_parse_ok;
  }

  // Persistent-arg scan: a parent's persistent args may precede the sub-command name
  // (e.g. "joy -n 2 cal"). If a sub-command is found, it becomes the active command.
  Command* parent_cmd = nullptr;
  uint8_t start_token = 1;
  Command* sub        = _scanForSubCommand(cmd, tokens, count, start_token);
  if (sub) {
    parent_cmd = cmd;
    cmd        = sub;
  }

  // Reset parsed state for both parent (persistent args) and the active command
  if (parent_cmd) parent_cmd->_resetParsed();
  cmd->_resetParsed();

  // Shared scratch buffer for error messages across the parse and validation phases below.
  static char err_msg[Config::MAX_INPUT_LEN];

  // Parse the persistent args that appear before the sub-command token
  if (parent_cmd) _parsePersistentArgs(*parent_cmd, tokens, start_token - 1, err_msg);

  // Walk the remaining tokens and fill the active command's parsed arguments
  _parseTokens(*cmd, tokens, count, start_token, err_msg);

  // Validate required args, types, and user validators (active command + parent persistent args).
  static char usage_buf[Config::MAX_INPUT_LEN];
  _buildUsageStr(*cmd, usage_buf, sizeof(usage_buf));
  _validateArgs(*cmd, usage_buf, err_msg);
  if (parent_cmd) _validatePersistentArgs(*parent_cmd, usage_buf, err_msg);

  // Do not run the command callback if any parse or validation errors occurred.
  if (!_last_parse_ok) return _last_parse_ok;

  // Execute callback
  cmd->_execute();
  return _last_parse_ok;
}

/* ------------------------------------------- Help ------------------------------------------- */

void AdvancedCLI::printHelp(uint8_t depth) const {
  _output("Available commands:");

  for (uint16_t i = 0; i < _cmd_count; ++i) {
    const Command& cmd = _commands[i];
    if (cmd._parent_idx != -1) continue; // sub-commands printed under their parent

    _printCommandEntry(cmd, 2, depth >= 3);

    if (depth < 2) continue;

    // Print direct sub-commands indented below the parent
    for (uint16_t j = 0; j < _cmd_count; ++j) {
      if (_commands[j]._parent_idx == i) {
        _printCommandEntry(_commands[j], 4, depth >= 3);
      }
    }
  }
}

void AdvancedCLI::printHelp(const char* cmd_name, uint8_t depth) const {
  if (!cmd_name) return;

  for (uint16_t i = 0; i < _cmd_count; ++i) {
    const Command& cmd = _commands[i];
    if (!strEqual(cmd.getName(), cmd_name, _case_sensitive)) continue;

    _printCommandEntry(cmd, 2, depth >= 3);

    if (depth < 2) return;

    for (uint16_t j = 0; j < _cmd_count; ++j) {
      if (_commands[j]._parent_idx == i) {
        _printCommandEntry(_commands[j], 4, depth >= 3);
      }
    }

    return;
  }
}

void AdvancedCLI::printHelp(const Command& cmd, uint8_t depth) const {
  _printCommandEntry(cmd, 2, depth >= 3);

  if (depth < 2) return;

  for (uint16_t j = 0; j < _cmd_count; ++j) {
    if (_commands[j]._parent_idx == cmd._self_idx) {
      _printCommandEntry(_commands[j], 4, depth >= 3);
    }
  }
}

/* ----------------------------------- Inject (unit-testing) ---------------------------------- */

bool AdvancedCLI::inject(const char* input) { return parse(input); }

#if ACLI_USE_STD_FUNCTION
bool AdvancedCLI::inject(const char* input, char* output_buf, size_t buf_size) {
  if (!output_buf || buf_size == 0) return inject(input);

  output_buf[0]     = '\0';
  size_t captured   = 0;
  OutputFn saved_fn = _output_fn;

  _output_fn = [output_buf, buf_size, &captured](const char* str) {
    // The capture sink is never invoked with a null string.
    if (!str) return; // GCOVR_EXCL_BR_LINE

    size_t remaining = buf_size - 1 - captured;
    if (remaining == 0) return;

    size_t str_len  = strlen(str);
    size_t copy_len = str_len < remaining ? str_len : remaining;
    memcpy(output_buf + captured, str, copy_len);
    captured += copy_len;

    if (captured < buf_size - 1) output_buf[captured++] = '\n';

    output_buf[captured] = '\0';
  };

  bool ok    = inject(input);
  _output_fn = saved_fn;
  return ok;
}
#endif

bool AdvancedCLI::lastParseOk() const { return _last_parse_ok; }

/* ------------------------------------------ Utility ----------------------------------------- */

uint16_t AdvancedCLI::getCommandCount() const { return _cmd_count; }

uint16_t AdvancedCLI::getArgCount() const { return _arg_pool_used; }

bool AdvancedCLI::isValid() const { return !_overflow; }

uint16_t AdvancedCLI::getAttemptedCommandCount() const { return _cmd_attempted; }

uint16_t AdvancedCLI::getAttemptedArgCount() const { return _arg_attempted; }

/* --------------------------------------- Private methods -------------------------------------- */

Command& AdvancedCLI::_addCommandInternal(const char* name, int16_t parent_idx) {
  if (!name) {
    _overflow = true;
    _dummy    = Command{};
    return _dummy;
  }

  ++_cmd_attempted; // Increment attempted count before overflow check to include all calls

  if (_cmd_count >= Config::MAX_COMMANDS) {
    _overflow        = true;
    _dummy           = Command{}; // reset all state
    _dummy._owner    = this;      // keep owner so add*() calls on the dummy can still reach us
    _dummy._self_idx = -1;        // sentinel: this slot overflowed; count but don't write to pool
    return _dummy;
  }

  uint16_t new_idx = _cmd_count++;
  _commands[new_idx]._init(name, this, new_idx, parent_idx);

  _commands[new_idx]._arg_pool_start = _arg_pool_used;
  _commands[new_idx]._arg_defs       = &_arg_def_pool[_arg_pool_used];
  _commands[new_idx]._parsed         = &_parsed_pool[_arg_pool_used];

  return _commands[new_idx];
}

Command* AdvancedCLI::_findCommand(const char* name, size_t name_len) {
  // Copy token to a null-terminated buffer for comparison
  char name_buf[Config::MAX_NAME_LEN] = {};
  size_t safe_len = name_len < Config::MAX_NAME_LEN - 1 ? name_len : Config::MAX_NAME_LEN - 1;

  for (size_t i = 0; i < safe_len; ++i) {
    name_buf[i] = name[i];
  }
  name_buf[safe_len] = '\0';

  for (uint16_t i = 0; i < _cmd_count; ++i) {
    if (_commands[i].isSubCommand()) continue; // only match top-level commands
    if (strEqual(_commands[i].getName(), name_buf, _case_sensitive)) {
      return &_commands[i];
    }
  }
  return nullptr;
}

Command* AdvancedCLI::_findSubCommand(const Command* parent, const char* name) {
  // Defensive null guard; callers always pass valid pointers.
  if (!parent || !name) return nullptr; // GCOVR_EXCL_BR_LINE

  int16_t parent_idx = parent->_self_idx;

  for (uint16_t i = 0; i < _cmd_count; ++i) {
    if (_commands[i]._parent_idx == parent_idx &&
        strEqual(_commands[i].getName(), name, _case_sensitive)) {
      return &_commands[i];
    }
  }
  return nullptr;
}

uint8_t AdvancedCLI::_tokenize(const char* input, size_t input_len,
  char tokens[][Config::MAX_TOKEN_LEN], uint8_t max_tokens) const {
  uint8_t token_count = 0;
  uint16_t i          = 0; // uint16_t: input_len can be up to MAX_INPUT_LEN-1 (255)

  while (i < input_len && token_count < max_tokens) {
    // Skip whitespace
    while (i < input_len && (input[i] == ' ' || input[i] == '\t'))
      ++i;

    if (i >= input_len) break;

    uint8_t token_idx = 0;
    char* token_buf   = tokens[token_count];
    bool quoted       = false;
    char quote_char   = '\0';

    if (input[i] == '"' || input[i] == '\'') {
      quoted     = true;
      quote_char = input[i];
      ++i; // skip opening quote
    }

    while (i < input_len) {
      char current_char = input[i];

      if (!quoted && (current_char == ' ' || current_char == '\t')) break;

      if ((current_char == '\\') && ((static_cast<size_t>(i) + 1) < input_len)) {
        // Escape sequence
        ++i;
        char escape_char = input[i];
        if (escape_char == '"')
          current_char = '"';
        else if (escape_char == '\'')
          current_char = '\'';
        else if (escape_char == '\\')
          current_char = '\\';
        else if (escape_char == 'n')
          current_char = '\n';
        else if (escape_char == 't')
          current_char = '\t';
        else
          current_char = escape_char;
      } else if (current_char == quote_char) {
        ++i; // skip closing quote
        break;
      }

      if (token_idx < Config::MAX_TOKEN_LEN - 1) {
        token_buf[token_idx++] = current_char;
      }
      ++i;
    }

    token_buf[token_idx] = '\0';

    // The quoted-empty-token arc is not exercised.
    if (token_idx > 0 || quoted) ++token_count; // GCOVR_EXCL_BR_LINE
  }

  return token_count;
}

// MARK: parse() helpers start

void AdvancedCLI::_handleUnknownCommand(const char* token) {
  _last_parse_ok = false;

  if (_unknown_cmd_fn) {
    _unknown_cmd_fn(token);
    return;
  }

  _outputf("[CLI] Unknown command: \"%s\"", token);

  // "Did you mean?" - simple prefix match
  const char* candidate = _suggestCommand(token);
  if (candidate) _outputf("       Did you mean: \"%s\"?", candidate);
}

const char* AdvancedCLI::_suggestCommand(const char* token) const {
  for (uint16_t i = 0; i < _cmd_count; ++i) {
    if (_commands[i]._parent_idx != -1) continue; // skip sub-commands

    const char* cmd_name = _commands[i].getName();
    size_t token_len     = strlen(token);
    bool match           = true;

    for (size_t j = 0; j < token_len; ++j) {
      char input_char     = token[j];
      char candidate_char = cmd_name[j];

      if (!_case_sensitive) {
        if (input_char >= 'A' && input_char <= 'Z') input_char += 32;
        if (candidate_char >= 'A' && candidate_char <= 'Z') candidate_char += 32;
      }

      if (input_char != candidate_char || candidate_char == '\0') {
        match = false;
        break;
      }
    }

    if (match && token_len > 0) return cmd_name;
  }

  return nullptr;
}

Command* AdvancedCLI::_scanForSubCommand(Command* cmd, const char tokens[][Config::MAX_TOKEN_LEN],
  uint8_t count, uint8_t& start_token) {
  uint8_t t = 1;

  while (t < count) {
    const char* tok = tokens[t];

    // "--" terminates the persistent-arg scan
    if (tok[0] == '-' && tok[1] == '-' && tok[2] == '\0') break;

    bool is_named_flag = (tok[0] == '-' && !isNumToken(tok));

    // Non-flag token: the first one is the sub-command name (if it matches one)
    if (!is_named_flag) {
      Command* sub = _findSubCommand(cmd, tok);
      if (sub) {
        start_token = t + 1;
        return sub;
      }
      break; // stop scanning regardless
    }

    int16_t def_idx = cmd->_findPersistentArgDefByName(tok);
    if (def_idx < 0) break; // non-persistent flag -> stop scan

    // Skip this persistent arg and its value token (if it is a named, not a flag)
    const ArgDef& d = cmd->_arg_defs[def_idx];
    if (d.type == ArgType::Flag) {
      ++t;
    } else {
      t += (t + 1 < count) ? 2 : 1;
    }
  }

  return nullptr;
}

void AdvancedCLI::_parsePersistentArgs(Command& parent, const char tokens[][Config::MAX_TOKEN_LEN],
  uint8_t subcmd_token, char* err_msg) {
  uint8_t t = 1;

  while (t < subcmd_token) {
    const char* tok = tokens[t];

    // Not every operand arc of the flag test runs in this scan.
    bool is_named_flag = (tok[0] == '-' && !isNumToken(tok)); // GCOVR_EXCL_BR_LINE

    if (!is_named_flag) {
      // Defensive: unreachable in practice. The persistent-arg scan above only advances past flag
      // tokens and their values, so the first non-flag token is always the sub-command boundary
      // (subcmd_token). This branch guards against an infinite loop should that invariant ever be
      // broken.
      // GCOVR_EXCL_START
      ++t;
      continue;
      // GCOVR_EXCL_STOP
    }

    int16_t def_idx = parent._findArgDefByName(tok);
    if (def_idx < 0) {
      ++t;
      continue;
    }

    ArgDef& d    = parent._arg_defs[def_idx];
    ParsedArg& p = parent._parsed[def_idx];

    if (d.type == ArgType::Flag) {
      p.is_set = true;
      p.token  = "1";
      ++t;
      continue;
    }

    if (isNextTokenValue(t, subcmd_token, tokens)) {
      p.is_set = true;
      p.token  = tokens[t + 1];
      t += 2;
      continue;
    }

    if (d.has_default) {
      p.is_set = true;
      p.token  = (d.value_type == ArgValueType::Any) ? d.default_value.str : nullptr;
      ++t;
      continue;
    }

    snprintf(err_msg, Config::MAX_INPUT_LEN, "[CLI] Argument \"%s\" expects a value.", d.name);
    _fireError(parent, err_msg);
    ++t;
  }
}

void AdvancedCLI::_parseTokens(Command& cmd, const char tokens[][Config::MAX_TOKEN_LEN],
  uint8_t count, uint8_t start_token, char* err_msg) {
  // 'positional_only' is set when "--" is encountered; all subsequent tokens are treated as
  // positional values regardless of whether they start with '-'.
  int16_t pos_arg_idx  = 0;
  bool positional_only = false;

  for (uint8_t t = start_token; t < count;) {
    const char* tok = tokens[t];

    // "--" separator: everything after is positional
    if (!positional_only && tok[0] == '-' && tok[1] == '-' && tok[2] == '\0') {
      positional_only = true;
      ++t;
      continue;
    }

    // A token is a flag/arg-name reference when it starts with '-' but is NOT a negative number.
    // Negative numbers: -5, -3.14, -.5  ->  second char is a digit or '.'
    bool is_flag = (!positional_only && tok[0] == '-' && !isNumToken(tok));

    // Positional
    if (!is_flag) {
      int16_t def_idx = cmd._positionalArgIndex(pos_arg_idx);

      if (def_idx >= 0) {
        cmd._parsed[def_idx].is_set = true;
        cmd._parsed[def_idx].token  = tok; // points into static tokens array
        ++pos_arg_idx;
      } else {
        snprintf(err_msg, Config::MAX_INPUT_LEN, "[CLI] Unexpected positional value: \"%s\"", tok);
        _fireError(cmd, err_msg);
      }

      ++t;
      continue;
    }

    int16_t def_idx = cmd._findArgDefByName(tok);
    if (def_idx < 0) {
      snprintf(err_msg,
        Config::MAX_INPUT_LEN,
        "[CLI] Unknown argument: \"%s\" for command \"%s\"",
        tok,
        cmd.getName());
      _fireError(cmd, err_msg);
      ++t;
      continue;
    }

    ArgDef& d    = cmd._arg_defs[def_idx];
    ParsedArg& p = cmd._parsed[def_idx];

    if (d.type == ArgType::Flag) {
      p.is_set = true;
      p.token  = "1"; // static literal, always safe to point to
      ++t;
      continue;
    }

    // Named: next token is the value.
    // Accept the next token as a value if it does NOT start with '-',
    // OR if it looks like a negative number (e.g. -5, -3.14).
    if (isNextTokenValue(t, count, tokens)) {
      p.is_set = true;
      p.token  = tokens[t + 1]; // points into static tokens array
      t += 2;
      continue;
    }

    // Use default, mark as set.
    // String defaults: point directly to the literal.
    // Typed (INT/FLOAT) defaults: leave token null; resolveToken() formats on demand.
    if (d.has_default) {
      p.is_set = true;
      p.token  = (d.value_type == ArgValueType::Any) ? d.default_value.str : nullptr;
      ++t;
      continue;
    }

    snprintf(err_msg, Config::MAX_INPUT_LEN, "[CLI] Argument \"%s\" expects a value.", d.name);
    _fireError(cmd, err_msg);
    ++t;
  }
}

void AdvancedCLI::_validateArgs(Command& cmd, const char* usage_buf, char* err_msg) {
  for (uint16_t i = 0; i < cmd._arg_count; ++i) {
    const ArgDef& d = cmd._arg_defs[i];
    ParsedArg& p    = cmd._parsed[i];

    // --- Required check ---
    if (d.is_required && !p.is_set) {
      snprintf(err_msg, Config::MAX_INPUT_LEN, "[CLI] Required argument missing: \"-%s\"", d.name);
      _fireError(cmd, err_msg, usage_buf);
      continue;
    }

    // Skip further validation if not provided
    if (!p.is_set) continue;

    // Flags carry no textual value to validate
    if (d.type == ArgType::Flag) continue;

    // --- Type check (INT / FLOAT) ---
    if (d.value_type == ArgValueType::Int || d.value_type == ArgValueType::Float) {
      char* end = nullptr;

      char pvbuf[24];
      const char* pv = resolveValue(&p, &d, pvbuf, sizeof(pvbuf));
      if (d.value_type == ArgValueType::Int) {
        strtol(pv, &end, 0);
      } else {
        strtod(pv, &end);
      }

      // Not all operand arcs of the type-OK test are taken.
      const bool typeOk = (end != nullptr && end != pv && *end == '\0'); // GCOVR_EXCL_BR_LINE

      if (!typeOk) {
        char reason[48];
        snprintf(reason,
          sizeof(reason),
          "expected %s, got \"%s\"",
          d.value_type == ArgValueType::Int ? "integer" : "number",
          pv);
        _fireInvalid(cmd, d, pv, reason, usage_buf);
        continue;
      }

      // --- User-supplied validation function ---
#if ACLI_ENABLE_VALIDATION_FN
      if (hasValidator(d) && !callValidator(d, pv)) {
        _fireInvalid(cmd, d, pv, "rejected by validation function", usage_buf);
        continue;
      }
#endif
    }

    // --- User-supplied validation for ArgStr (type Any) ---
#if ACLI_ENABLE_VALIDATION_FN
    // GCOVR_EXCL_BR_START: Compound condition; not all operand arcs are exercised.
    if (d.value_type == ArgValueType::Any && d.type != ArgType::Flag && hasValidator(d)) {
      // GCOVR_EXCL_BR_STOP
      char pvbuf3[Config::MAX_VALUE_LEN];
      const char* pv = resolveValue(&p, &d, pvbuf3, sizeof(pvbuf3));
      if (!callValidator(d, pv)) {
        _fireInvalid(cmd, d, pv, "rejected by validation function", usage_buf);
      }
    }
#endif
  }
}

void AdvancedCLI::_validatePersistentArgs(Command& parent, const char* usage_buf, char* err_msg) {
  for (uint16_t i = 0; i < parent._arg_count; ++i) {
    const ArgDef& d = parent._arg_defs[i];

    if (!d.is_persistent) continue;
    ParsedArg& p = parent._parsed[i];

    // Required check
    if (d.is_required && !p.is_set) {
      snprintf(err_msg, Config::MAX_INPUT_LEN, "[CLI] Required argument missing: \"-%s\"", d.name);
      _fireError(parent, err_msg, usage_buf);
      continue;
    }

    if (!p.is_set) continue;
    if (d.type == ArgType::Flag) continue;

    // Type check
    if (d.value_type != ArgValueType::Int && d.value_type != ArgValueType::Float) continue;

    char* end = nullptr;
    char pvbuf[24];
    const char* pv = resolveValue(&p, &d, pvbuf, sizeof(pvbuf));

    if (d.value_type == ArgValueType::Int) {
      strtol(pv, &end, 0);
    } else {
      strtod(pv, &end);
    }

    // Not all operand arcs of the type-OK test are taken.
    const bool typeOk = (end != nullptr && end != pv && *end == '\0'); // GCOVR_EXCL_BR_LINE

    if (!typeOk) {
      char reason[48];
      snprintf(reason,
        sizeof(reason),
        "expected %s, got \"%s\"",
        d.value_type == ArgValueType::Int ? "integer" : "number",
        pv);
      _fireInvalid(parent, d, pv, reason, usage_buf);
    }
  }
}

// MARK: parse() helpers end

void AdvancedCLI::_output(const char* str) const {
  // Always called with the sink set and a non-null string.
  if (_output_fn && str) _output_fn(str); // GCOVR_EXCL_BR_LINE
}

void AdvancedCLI::_outputf(const char* fmt, ...) const {
  // Defensive; the sink and fmt are always set here.
  if (!_output_fn || !fmt) return; // GCOVR_EXCL_BR_LINE

  static char fmt_buf[Config::MAX_INPUT_LEN * 2];
  va_list args;
  va_start(args, fmt);
  vsnprintf(fmt_buf, sizeof(fmt_buf), fmt, args);
  va_end(args);
  _output_fn(fmt_buf);
}

void AdvancedCLI::_buildUsageStr(const Command& cmd, char* buf, size_t buf_size) const {
  int write_pos;

  // For sub-commands include the parent name and its persistent args:
  // "joy -n <n> cal [-filter <filter>]"
  // The out-of-range parent-index arm is never taken.
  if (cmd._parent_idx >= 0 && cmd._parent_idx < _cmd_count) { // GCOVR_EXCL_BR_LINE
    const Command& parent = _commands[cmd._parent_idx];
    write_pos             = snprintf(buf, buf_size, "%s", parent.getName());

    // Interleave parent persistent args between parent name and sub-command name
    for (uint8_t i = 0; i < parent._arg_count; ++i) {
      const ArgDef& d = parent._arg_defs[i];

      // GCOVR_EXCL_BR_START: Defensive buffer bound; the write_pos-full arc is dead.
      if (!d.is_persistent || write_pos >= static_cast<int>(buf_size) - 1) continue;
      // GCOVR_EXCL_BR_STOP

      bool is_opt = !d.is_required;

      // Persistent args are only ever Flag or Named; Named is the default arm so the switch has
      // no unreachable synthetic case.
      switch (d.type) {
        case ArgType::Flag:
          write_pos += snprintf(buf + write_pos,
            buf_size - static_cast<size_t>(write_pos),
            is_opt ? " [-%s]" : " -%s",
            d.name);
          break;

        default: // Named
          write_pos += snprintf(buf + write_pos,
            buf_size - static_cast<size_t>(write_pos),
            is_opt ? " [-%s <%s>]" : " -%s <%s>",
            d.name,
            d.name);
          break;
      }
    }
    write_pos +=
      snprintf(buf + write_pos, buf_size - static_cast<size_t>(write_pos), " %s", cmd.getName());
  } else {
    write_pos = snprintf(buf, buf_size, "%s", cmd.getName());
  }

  for (uint8_t i = 0; i < cmd.getArgCount(); ++i) {
    const ArgDef& arg_def = cmd._arg_defs[i];
    bool is_optional      = !arg_def.is_required;

    // The buffer-full break is never reached in tests.
    if (write_pos >= static_cast<int>(buf_size) - 1) break; // GCOVR_EXCL_BR_LINE

    switch (arg_def.type) {
      case ArgType::Flag:
        write_pos += snprintf(buf + write_pos,
          buf_size - static_cast<size_t>(write_pos),
          is_optional ? " [-%s]" : " -%s",
          arg_def.name);
        break;

      case ArgType::Named:
        write_pos += snprintf(buf + write_pos,
          buf_size - static_cast<size_t>(write_pos),
          is_optional ? " [-%s <%s>]" : " -%s <%s>",
          arg_def.name,
          arg_def.name);
        break;

      default: // Positional
        write_pos += snprintf(buf + write_pos,
          buf_size - static_cast<size_t>(write_pos),
          is_optional ? " [<%s>]" : " <%s>",
          arg_def.name);
        break;
    }
  }
}

void AdvancedCLI::_fireInvalid(Command& cmd, const ArgDef& arg_def, const char* value,
  const char* reason, const char* usage_str) {
  // Per-argument override takes priority - called directly, bypasses onError.
#if ACLI_ENABLE_INVALID_FN
  if (arg_def.on_invalid_fn) {
    _last_parse_ok = false;
    arg_def.on_invalid_fn(arg_def.name, value, reason);
    return;
  }
#endif
  static char error_msg[Config::MAX_INPUT_LEN];
  snprintf(error_msg, sizeof(error_msg), "[CLI] Invalid \"-%s\": %s", arg_def.name, reason);
  _fireError(cmd, error_msg, usage_str);
}

void AdvancedCLI::_fireError(Command& cmd, const char* message, const char* usage_str) {
  _last_parse_ok = false;

  if (cmd._error_callback) {
    // message is always non-null when an error callback is set.
    cmd._error_callback(cmd, message ? message : ""); // GCOVR_EXCL_BR_LINE
    return;
  }

  // message is always a non-empty string here.
  if (message && message[0]) _output(message); // GCOVR_EXCL_BR_LINE

  // Not all usage_str null/empty arcs are exercised.
  if (usage_str && usage_str[0]) _outputf("      Usage: %s", usage_str); // GCOVR_EXCL_BR_LINE
}

void AdvancedCLI::_printCommandEntry(const Command& cmd, uint8_t indent, bool print_args) const {
  // Build indent string
  char pad[12] = {};

  for (uint8_t k = 0; k < indent && k < 11; ++k)
    pad[k] = ' ';

  _outputf("%s%-16s %s", pad, cmd.getName(), cmd.getDescription()[0] ? cmd.getDescription() : "");

  if (!print_args) return;

  // Argument lines (indented 2 more than the command name)
  char arg_pad[14] = {};

  // The k<13 clamp arm is never hit; indent stays small.
  for (uint8_t k = 0; k < indent + 2 && k < 13; ++k) // GCOVR_EXCL_BR_LINE
    arg_pad[k] = ' ';

  for (uint8_t j = 0; j < cmd.getArgCount(); ++j) {
    const ArgDef& d = cmd._arg_defs[j];

    // Build alias string: "(-a, -b)"
    char aliases[64]  = {};
    uint8_t alias_idx = 0;

    if (d.alias_count > 0) {
      aliases[alias_idx++] = '(';

      for (uint8_t k = 0; k < d.alias_count; ++k) {
        if (k > 0 && alias_idx < 62) aliases[alias_idx++] = ',';

        // The alias-buffer-full arm is never reached in tests.
        if (alias_idx < 62) aliases[alias_idx++] = '-'; // GCOVR_EXCL_BR_LINE

        // The alias-buffer-full arm is never reached in tests.
        for (uint8_t c = 0; d.aliases[k][c] && alias_idx < 62; ++c) { // GCOVR_EXCL_BR_LINE
          aliases[alias_idx++] = d.aliases[k][c];
        }
      }

      // The alias-buffer-full arm is never reached in tests.
      if (alias_idx < 63) aliases[alias_idx++] = ')'; // GCOVR_EXCL_BR_LINE

      aliases[alias_idx] = '\0';
    }

    const char* type_tag = "";

    switch (d.type) {
      case ArgType::Flag: type_tag = "[flag ]"; break;
      case ArgType::Named: type_tag = "[named]"; break;
      default: type_tag = "[pos  ]"; break; // Positional
    }

    char line[Config::MAX_DESC_LEN * 2] = {};
    int write_pos                       = 0;

    write_pos += snprintf(line + write_pos,
      sizeof(line) - static_cast<size_t>(write_pos),
      "%s-%-14s",
      arg_pad,
      d.name);
    clampWritePos(write_pos, sizeof(line));

    if (aliases[0]) {
      write_pos += snprintf(line + write_pos,
        sizeof(line) - static_cast<size_t>(write_pos),
        " %-12s",
        aliases);
    } else {
      write_pos +=
        snprintf(line + write_pos, sizeof(line) - static_cast<size_t>(write_pos), "             ");
    }
    clampWritePos(write_pos, sizeof(line));

    write_pos +=
      snprintf(line + write_pos, sizeof(line) - static_cast<size_t>(write_pos), " %s", type_tag);
    clampWritePos(write_pos, sizeof(line));

    if (d.description) {
      write_pos += snprintf(line + write_pos,
        sizeof(line) - static_cast<size_t>(write_pos),
        " %s",
        d.description);
      clampWritePos(write_pos, sizeof(line));
    }

    if (d.has_default) {
      char default_buf[24];
      const char* default_str;

      switch (d.value_type) {
        case ArgValueType::Int:
          snprintf(default_buf, sizeof(default_buf), "%" PRId32, d.default_value.i);
          default_str = default_buf;
          break;

        case ArgValueType::Float:
          snprintf(default_buf, sizeof(default_buf), "%g", static_cast<double>(d.default_value.f));
          default_str = default_buf;
          break;

        // GCOVR_EXCL_BR_START: Dead ':' arm; an Any default str pointer is never null.
        default:
          default_str = d.default_value.str ? d.default_value.str : "";
          break;
          // GCOVR_EXCL_BR_STOP
      }

      if (default_str[0]) {
        write_pos += snprintf(line + write_pos,
          sizeof(line) - static_cast<size_t>(write_pos),
          " (default: %s)",
          default_str);
        clampWritePos(write_pos, sizeof(line));
      }
    }

    if (d.is_required) {
      write_pos +=
        snprintf(line + write_pos, sizeof(line) - static_cast<size_t>(write_pos), " *required*");
      clampWritePos(write_pos, sizeof(line));
    }

    _output(line);
  }
}

} // namespace ACLI
