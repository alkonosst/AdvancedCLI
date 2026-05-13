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

  if (!cmd) {
    _last_parse_ok = false;
    if (_unknown_cmd_fn) {
      _unknown_cmd_fn(tokens[0]);
    } else {
      _outputf("[CLI] Unknown command: \"%s\"", tokens[0]);

      // "Did you mean?" - simple prefix match
      const char* candidate = nullptr;
      for (uint8_t i = 0; i < _cmd_count; ++i) {
        if (_commands[i]._parent_idx != -1) continue; // skip sub-commands
        const char* cmd_name = _commands[i].getName();
        size_t token_len     = strlen(tokens[0]);
        bool match           = true;
        for (size_t j = 0; j < token_len; ++j) {
          char input_char     = tokens[0][j];
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
        if (match && token_len > 0) {
          candidate = cmd_name;
          break;
        }
      }

      if (candidate) {
        _outputf("       Did you mean: \"%s\"?", candidate);
      }
    }
    return _last_parse_ok;
  }

  // Sub-command resolution: if the next token (before any flags) names a registered
  // sub-command, switch to it.  Falls through to normal positional parsing otherwise.
  uint8_t start_token = 1;
  if (count > 1 && tokens[1][0] != '-') {
    Command* sub = _findSubCommand(cmd, tokens[1]);
    if (sub) {
      cmd         = sub;
      start_token = 2;
    }
  }

  // Reset parsed state
  cmd->_resetParsed();

  // Walk remaining tokens and fill ParsedArguments.
  // 'positionalOnly' is set when "--" is encountered; all subsequent tokens
  // are treated as positional values regardless of whether they start with '-'.
  int8_t pos_arg_idx   = 0;
  bool positional_only = false;
  static char err_msg[Config::MAX_INPUT_LEN]; // scratch buffer for per-parse error messages

  for (uint8_t t = start_token; t < count;) {
    const char* tok = tokens[t];

    // "--" separator: everything after is positional
    if (!positional_only && tok[0] == '-' && tok[1] == '-' && tok[2] == '\0') {
      positional_only = true;
      ++t;
      continue;
    }

    // A token is a flag/arg-name reference when it starts with '-' but is NOT a negative number.
    // Negative numbers: -5, -3.14, -.5  →  second char is a digit or '.'
    bool is_flag = (!positional_only && tok[0] == '-' && !isNumToken(tok));

    if (is_flag) {
      int8_t def_idx = cmd->_findArgDefByName(tok);

      if (def_idx < 0) {
        snprintf(err_msg,
          sizeof(err_msg),
          "[CLI] Unknown argument: \"%s\" for command \"%s\"",
          tok,
          cmd->getName());
        _fireError(*cmd, err_msg);
        ++t;
        continue;
      }

      ArgDef& d    = cmd->_arg_defs[def_idx];
      ParsedArg& p = cmd->_parsed[def_idx];

      if (d.type == ArgType::Flag) {
        p.is_set = true;
        p.token  = "1"; // static literal, always safe to point to
        ++t;
      } else {
        // Named: next token is the value.
        // Accept the next token as a value if it does NOT start with '-',
        // OR if it looks like a negative number (e.g. -5, -3.14).
        if (isNextTokenValue(t, count, tokens)) {
          p.is_set = true;
          p.token  = tokens[t + 1]; // points into static tokens array
          t += 2;
        } else if (d.has_default) {
          // Use default, mark as set.
          // String defaults: point directly to the literal.
          // Typed (INT/FLOAT) defaults: leave token null; resolveToken() formats on demand.
          p.is_set = true;
          p.token  = (d.value_type == ArgValueType::Any) ? d.default_value.str : nullptr;
          ++t;
        } else {
          snprintf(err_msg, sizeof(err_msg), "[CLI] Argument \"%s\" expects a value.", d.name);
          _fireError(*cmd, err_msg);
          ++t;
        }
      }
    } else {
      // Positional
      int8_t def_idx = cmd->_positionalArgIndex(pos_arg_idx);
      if (def_idx >= 0) {
        cmd->_parsed[def_idx].is_set = true;
        cmd->_parsed[def_idx].token  = tok; // points into static tokens array
        ++pos_arg_idx;
      } else {
        snprintf(err_msg, sizeof(err_msg), "[CLI] Unexpected positional value: \"%s\"", tok);
        _fireError(*cmd, err_msg);
      }
      ++t;
    }
  }

  // Validate: required args, type check, range, oneOf
  bool valid = true;
  static char usage_buf[Config::MAX_INPUT_LEN];
  _buildUsageStr(*cmd, usage_buf, sizeof(usage_buf));

  for (uint8_t i = 0; i < cmd->_arg_count; ++i) {
    const ArgDef& d = cmd->_arg_defs[i];
    ParsedArg& p    = cmd->_parsed[i];

    // --- Required check ---
    if (d.is_required && !p.is_set) {
      snprintf(err_msg, sizeof(err_msg), "[CLI] Required argument missing: \"-%s\"", d.name);
      _fireError(*cmd, err_msg, usage_buf);
      valid = false;
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

      const bool typeOk = (end != nullptr && end != pv && *end == '\0');

      if (!typeOk) {
        char reason[48];
        snprintf(reason,
          sizeof(reason),
          "expected %s, got \"%s\"",
          d.value_type == ArgValueType::Int ? "integer" : "number",
          pv);
        _fireInvalid(*cmd, d, pv, reason, usage_buf);
        valid = false;
        continue;
      }

      // --- User-supplied validation function ---
#if ACLI_ENABLE_VALIDATION_FN
      if (hasValidator(d) && !callValidator(d, pv)) {
        _fireInvalid(*cmd, d, pv, "rejected by validation function", usage_buf);
        valid = false;
        continue;
      }
#endif
    }

    // --- User-supplied validation for ArgStr (type Any) ---
#if ACLI_ENABLE_VALIDATION_FN
    if (d.value_type == ArgValueType::Any && d.type != ArgType::Flag && hasValidator(d)) {
      char pvbuf3[Config::MAX_VALUE_LEN];
      const char* pv = resolveValue(&p, &d, pvbuf3, sizeof(pvbuf3));
      if (!callValidator(d, pv)) {
        _fireInvalid(*cmd, d, pv, "rejected by validation function", usage_buf);
        valid = false;
      }
    }
#endif
  }

  if (!valid) {
    _last_parse_ok = false;
    return _last_parse_ok;
  }

  // Execute callback
  cmd->_execute();
  return _last_parse_ok;
}

/* ------------------------------------------- Help ------------------------------------------- */

void AdvancedCLI::printHelp(uint8_t depth) const {
  _output("Available commands:");

  for (uint8_t i = 0; i < _cmd_count; ++i) {
    const Command& cmd = _commands[i];
    if (cmd._parent_idx != -1) continue; // sub-commands printed under their parent

    _printCommandEntry(cmd, 2, depth >= 3);

    if (depth >= 2) {
      // Print direct sub-commands indented below the parent
      for (uint8_t j = 0; j < _cmd_count; ++j) {
        if (_commands[j]._parent_idx == i) {
          _printCommandEntry(_commands[j], 4, depth >= 3);
        }
      }
    }
  }
}

void AdvancedCLI::printHelp(const char* cmd_name, uint8_t depth) const {
  if (!cmd_name) return;
  for (uint8_t i = 0; i < _cmd_count; ++i) {
    const Command& cmd = _commands[i];
    if (cmd._parent_idx != -1) continue; // only match top-level names
    if (!strEqual(cmd.getName(), cmd_name, _case_sensitive)) continue;

    _printCommandEntry(cmd, 2, depth >= 3);
    if (depth >= 2) {
      for (uint8_t j = 0; j < _cmd_count; ++j) {
        if (_commands[j]._parent_idx == i) {
          _printCommandEntry(_commands[j], 4, depth >= 3);
        }
      }
    }
    return;
  }
}

/* ----------------------------------- Inject (unit-testing) ---------------------------------- */

bool AdvancedCLI::inject(const char* input) {
  parse(input);
  return _last_parse_ok;
}

#if ACLI_USE_STD_FUNCTION
bool AdvancedCLI::inject(const char* input, char* output_buf, size_t buf_size) {
  if (!output_buf || buf_size == 0) return inject(input);
  output_buf[0]     = '\0';
  size_t captured   = 0;
  OutputFn saved_fn = _output_fn;
  _output_fn        = [output_buf, buf_size, &captured](const char* str) {
    if (!str) return;
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

uint8_t AdvancedCLI::commandCount() const { return _cmd_count; }

uint8_t AdvancedCLI::argCount() const { return _arg_pool_used; }

bool AdvancedCLI::isValid() const { return !_overflow; }

/* --------------------------------------- Private methods -------------------------------------- */

Command& AdvancedCLI::_addCommandInternal(const char* name, int8_t parent_idx) {
  if (!name || _cmd_count >= Config::MAX_COMMANDS) {
    _overflow = true;
    _dummy    = Command{};
    return _dummy;
  }
  uint8_t new_idx = _cmd_count++;
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

  for (uint8_t i = 0; i < _cmd_count; ++i) {
    if (strEqual(_commands[i].getName(), name_buf, _case_sensitive)) {
      return &_commands[i];
    }
  }
  return nullptr;
}

Command* AdvancedCLI::_findSubCommand(const Command* parent, const char* name) {
  if (!parent || !name) return nullptr;
  int8_t parent_idx = parent->_self_idx;
  for (uint8_t i = 0; i < _cmd_count; ++i) {
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

    if (input[i] == '"') {
      quoted = true;
      ++i; // skip opening quote
    }

    while (i < input_len) {
      char current_char = input[i];

      if (quoted) {
        if (current_char == '\\' && i + 1 < input_len) {
          // Escape sequence
          ++i;
          char escape_char = input[i];
          if (escape_char == '"')
            current_char = '"';
          else if (escape_char == '\\')
            current_char = '\\';
          else if (escape_char == 'n')
            current_char = '\n';
          else if (escape_char == 't')
            current_char = '\t';
          else
            current_char = escape_char;
        } else if (current_char == '"') {
          ++i; // skip closing quote
          break;
        }
      } else {
        if (current_char == ' ' || current_char == '\t') break;
      }

      if (token_idx < Config::MAX_TOKEN_LEN - 1) {
        token_buf[token_idx++] = current_char;
      }
      ++i;
    }

    token_buf[token_idx] = '\0';
    if (token_idx > 0 || quoted) ++token_count;
  }
  return token_count;
}

void AdvancedCLI::_output(const char* str) const {
  if (_output_fn && str) _output_fn(str);
}

void AdvancedCLI::_outputf(const char* fmt, ...) const {
  if (!_output_fn || !fmt) return;
  static char fmt_buf[Config::MAX_INPUT_LEN * 2];
  va_list args;
  va_start(args, fmt);
  vsnprintf(fmt_buf, sizeof(fmt_buf), fmt, args);
  va_end(args);
  _output_fn(fmt_buf);
}

void AdvancedCLI::_buildUsageStr(const Command& cmd, char* buf, size_t buf_size) const {
  int write_pos;
  // For sub-commands include the parent name: "wifi connect [-ssid <ssid>]"
  if (cmd._parent_idx >= 0 && cmd._parent_idx < _cmd_count) {
    write_pos =
      snprintf(buf, buf_size, "%s %s", _commands[cmd._parent_idx].getName(), cmd.getName());
  } else {
    write_pos = snprintf(buf, buf_size, "%s", cmd.getName());
  }

  for (uint8_t i = 0; i < cmd.getArgCount(); ++i) {
    const ArgDef& arg_def = cmd._arg_defs[i];
    bool is_optional      = !arg_def.is_required;

    if (write_pos >= (int)buf_size - 1) break;

    switch (arg_def.type) {
      case ArgType::Flag:
        write_pos += snprintf(buf + write_pos,
          buf_size - (size_t)write_pos,
          is_optional ? " [-%s]" : " -%s",
          arg_def.name);
        break;

      case ArgType::Named:
        write_pos += snprintf(buf + write_pos,
          buf_size - (size_t)write_pos,
          is_optional ? " [-%s <%s>]" : " -%s <%s>",
          arg_def.name,
          arg_def.name);
        break;

      case ArgType::Positional:
        write_pos += snprintf(buf + write_pos,
          buf_size - (size_t)write_pos,
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
    cmd._error_callback(cmd, message ? message : "");
  } else {
    if (message && message[0]) _output(message);
    if (usage_str && usage_str[0]) _outputf("      Usage: %s", usage_str);
  }
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
  for (uint8_t k = 0; k < indent + 2 && k < 13; ++k)
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
        if (alias_idx < 62) aliases[alias_idx++] = '-';
        for (uint8_t c = 0; d.aliases[k][c] && alias_idx < 62; ++c) {
          aliases[alias_idx++] = d.aliases[k][c];
        }
      }
      if (alias_idx < 63) aliases[alias_idx++] = ')';
      aliases[alias_idx] = '\0';
    }

    const char* type_tag = "";
    switch (d.type) {
      case ArgType::Flag: type_tag = "[flag ]"; break;
      case ArgType::Named: type_tag = "[named]"; break;
      case ArgType::Positional: type_tag = "[pos  ]"; break;
    }

    char line[Config::MAX_DESC_LEN * 2] = {};
    int write_pos                       = 0;

    write_pos +=
      snprintf(line + write_pos, sizeof(line) - (size_t)write_pos, "%s-%-14s", arg_pad, d.name);
    clampWritePos(write_pos, sizeof(line));

    if (aliases[0]) {
      write_pos += snprintf(line + write_pos, sizeof(line) - (size_t)write_pos, " %-12s", aliases);
    } else {
      write_pos += snprintf(line + write_pos, sizeof(line) - (size_t)write_pos, "             ");
    }
    clampWritePos(write_pos, sizeof(line));

    write_pos += snprintf(line + write_pos, sizeof(line) - (size_t)write_pos, " %s", type_tag);
    clampWritePos(write_pos, sizeof(line));

    if (d.description) {
      write_pos +=
        snprintf(line + write_pos, sizeof(line) - (size_t)write_pos, " %s", d.description);
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

        default: default_str = d.default_value.str ? d.default_value.str : ""; break;
      }

      if (default_str[0]) {
        write_pos += snprintf(line + write_pos,
          sizeof(line) - (size_t)write_pos,
          " (default: %s)",
          default_str);
        clampWritePos(write_pos, sizeof(line));
      }
    }

    if (d.is_required) {
      write_pos += snprintf(line + write_pos, sizeof(line) - (size_t)write_pos, " *required*");
      clampWritePos(write_pos, sizeof(line));
    }

    _output(line);
  }
}

} // namespace ACLI
