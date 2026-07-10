#include "core/config_document.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

namespace fmcw {
namespace {

std::string trim(std::string_view text) {
  std::size_t first = 0;
  while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
    ++first;
  }
  std::size_t last = text.size();
  while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
    --last;
  }
  return std::string(text.substr(first, last - first));
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

bool validKey(std::string_view key) {
  if (key.empty()) {
    return false;
  }
  return std::all_of(key.begin(), key.end(), [](unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
  });
}

std::size_t findUnquoted(std::string_view text, char needle) {
  char quote = 0;
  bool escaped = false;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const char ch = text[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (quote == '"' && ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '\'' || ch == '"') {
      if (quote == 0) {
        quote = ch;
      } else if (quote == ch) {
        quote = 0;
      }
      continue;
    }
    if (quote == 0 && ch == needle) {
      return index;
    }
  }
  return std::string_view::npos;
}

std::string stripComment(std::string_view line) {
  const auto position = findUnquoted(line, '#');
  return std::string(position == std::string_view::npos ? line : line.substr(0, position));
}

bool parseQuoted(std::string_view text, std::string& output, std::string& message) {
  const char quote = text.front();
  if (text.size() < 2 || text.back() != quote) {
    message = "Unterminated quoted scalar";
    return false;
  }

  output.clear();
  for (std::size_t index = 1; index + 1 < text.size(); ++index) {
    const char ch = text[index];
    if (quote == '\'' && ch == '\'' && index + 2 < text.size() && text[index + 1] == '\'') {
      output.push_back('\'');
      ++index;
      continue;
    }
    if (quote == '"' && ch == '\\') {
      if (index + 2 >= text.size()) {
        message = "Invalid escape sequence";
        return false;
      }
      const char escaped = text[++index];
      switch (escaped) {
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case '\\': output.push_back('\\'); break;
        case '"': output.push_back('"'); break;
        default:
          message = "Unsupported escape sequence";
          return false;
      }
      continue;
    }
    output.push_back(ch);
  }
  return true;
}

bool isInteger(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  std::size_t index = (text.front() == '+' || text.front() == '-') ? 1 : 0;
  if (index == text.size()) {
    return false;
  }
  return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(index), text.end(), [](unsigned char ch) {
    return std::isdigit(ch) != 0;
  });
}

bool isNumber(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  std::string value(text);
  char* end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  return end == value.c_str() + value.size() && std::isfinite(parsed);
}

bool parseScalar(std::string_view text, ConfigScalar& scalar, std::string& message) {
  const std::string value = trim(text);
  if (value.empty()) {
    message = "Scalar value is empty";
    return false;
  }

  if (value.front() == '\'' || value.front() == '"') {
    scalar.kind = ConfigScalarKind::String;
    return parseQuoted(value, scalar.text, message);
  }

  if (findUnquoted(value, ':') != std::string_view::npos) {
    message = "Plain string containing ':' must be quoted";
    return false;
  }

  const auto normalized = lower(value);
  if (normalized == "true" || normalized == "false") {
    scalar = {normalized, ConfigScalarKind::Boolean};
    return true;
  }
  if (normalized == "null" || normalized == "~") {
    message = "Null values are not supported in configuration profiles";
    return false;
  }
  if (isInteger(value)) {
    scalar = {value, ConfigScalarKind::Integer};
    return true;
  }
  if (isNumber(value)) {
    scalar = {value, ConfigScalarKind::Number};
    return true;
  }
  scalar = {value, ConfigScalarKind::String};
  return true;
}

std::vector<std::string> splitPath(std::string_view path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto end = path.find('.', start);
    parts.emplace_back(path.substr(start, end == std::string_view::npos ? path.size() - start : end - start));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

std::string quote(std::string_view value) {
  std::string output = "\"";
  for (const char ch : value) {
    switch (ch) {
      case '\\': output += "\\\\"; break;
      case '"': output += "\\\""; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default: output.push_back(ch); break;
    }
  }
  output.push_back('"');
  return output;
}

std::string renderScalar(const ConfigScalar& scalar) {
  return scalar.kind == ConfigScalarKind::String ? quote(scalar.text) : scalar.text;
}

struct TreeNode {
  std::map<std::string, TreeNode> children;
  std::optional<ConfigScalar> scalar;
};

void renderYamlNode(const TreeNode& node, std::size_t depth, std::ostringstream& output) {
  for (const auto& entry : node.children) {
    output << std::string(depth * 2, ' ') << entry.first << ':';
    if (entry.second.scalar.has_value()) {
      output << ' ' << renderScalar(*entry.second.scalar) << '\n';
    } else {
      output << '\n';
      renderYamlNode(entry.second, depth + 1, output);
    }
  }
}

}  // namespace

bool ConfigDocument::parseYaml(std::string_view yaml, ConfigDocument& output, ConfigParseError& error) {
  output.values_.clear();
  error = {};

  struct Parent {
    std::size_t indent = 0;
    std::string key;
  };
  std::vector<Parent> parents;

  std::istringstream stream{std::string(yaml)};
  std::string raw_line;
  std::size_t line_number = 0;
  while (std::getline(stream, raw_line)) {
    ++line_number;
    if (!raw_line.empty() && raw_line.back() == '\r') {
      raw_line.pop_back();
    }
    if (raw_line.find('\t') != std::string::npos) {
      error = {line_number, raw_line.find('\t') + 1, "Tabs are not allowed; use two-space indentation"};
      return false;
    }

    const std::string uncommented = stripComment(raw_line);
    if (trim(uncommented).empty()) {
      continue;
    }

    const auto first = uncommented.find_first_not_of(' ');
    const std::size_t indent = first == std::string::npos ? 0 : first;
    if (indent % 2 != 0) {
      error = {line_number, indent + 1, "Indentation must use multiples of two spaces"};
      return false;
    }

    const std::string content = trim(std::string_view(uncommented).substr(indent));
    if (!content.empty() && content.front() == '-') {
      error = {line_number, indent + 1, "YAML sequences are not supported by the profile schema"};
      return false;
    }

    while (!parents.empty() && parents.back().indent >= indent) {
      parents.pop_back();
    }
    if (indent != parents.size() * 2) {
      error = {line_number, indent + 1, "Nested keys must increase indentation by exactly two spaces"};
      return false;
    }

    const auto colon = findUnquoted(content, ':');
    if (colon == std::string_view::npos) {
      error = {line_number, indent + 1, "Expected 'key: value' mapping"};
      return false;
    }
    const std::string key = trim(std::string_view(content).substr(0, colon));
    if (!validKey(key)) {
      error = {line_number, indent + 1, "Keys may contain only letters, digits, '_' and '-'"};
      return false;
    }

    std::string path;
    for (const auto& parent : parents) {
      if (!path.empty()) {
        path.push_back('.');
      }
      path += parent.key;
    }
    if (!path.empty()) {
      path.push_back('.');
    }
    path += key;

    const std::string value = trim(std::string_view(content).substr(colon + 1));
    if (value.empty()) {
      parents.push_back({indent, key});
      continue;
    }

    ConfigScalar scalar;
    std::string scalar_error;
    if (!parseScalar(value, scalar, scalar_error)) {
      error = {line_number, indent + colon + 2, scalar_error};
      return false;
    }
    if (output.values_.find(path) != output.values_.end()) {
      error = {line_number, indent + 1, "Duplicate configuration key: " + path};
      return false;
    }
    output.values_.emplace(std::move(path), std::move(scalar));
  }

  if (output.values_.empty()) {
    error = {0, 0, "Configuration document contains no scalar values"};
    return false;
  }
  return true;
}

const ConfigDocument::Values& ConfigDocument::values() const { return values_; }

const ConfigScalar* ConfigDocument::find(std::string_view path) const {
  const auto found = values_.find(std::string(path));
  return found == values_.end() ? nullptr : &found->second;
}

bool ConfigDocument::contains(std::string_view path) const { return find(path) != nullptr; }

void ConfigDocument::setString(std::string path, std::string value) {
  setScalar(std::move(path), {std::move(value), ConfigScalarKind::String});
}

void ConfigDocument::setInteger(std::string path, std::int64_t value) {
  setScalar(std::move(path), {std::to_string(value), ConfigScalarKind::Integer});
}

void ConfigDocument::setNumber(std::string path, double value) {
  std::ostringstream text;
  text << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  setScalar(std::move(path), {text.str(), ConfigScalarKind::Number});
}

void ConfigDocument::setBoolean(std::string path, bool value) {
  setScalar(std::move(path), {value ? "true" : "false", ConfigScalarKind::Boolean});
}

void ConfigDocument::setScalar(std::string path, ConfigScalar value) {
  values_[std::move(path)] = std::move(value);
}

void ConfigDocument::merge(const ConfigDocument& overrides) {
  for (const auto& entry : overrides.values_) {
    values_[entry.first] = entry.second;
  }
}

std::string ConfigDocument::toYaml() const {
  TreeNode root;
  for (const auto& entry : values_) {
    TreeNode* node = &root;
    for (const auto& part : splitPath(entry.first)) {
      node = &node->children[part];
    }
    node->scalar = entry.second;
  }
  std::ostringstream output;
  renderYamlNode(root, 0, output);
  return output.str();
}

std::string ConfigDocument::toFlatJson() const {
  std::ostringstream output;
  output << "{";
  bool first = true;
  for (const auto& entry : values_) {
    if (!first) {
      output << ',';
    }
    first = false;
    output << '\n' << "  " << quote(entry.first) << ": ";
    if (entry.second.kind == ConfigScalarKind::String) {
      output << quote(entry.second.text);
    } else {
      output << entry.second.text;
    }
  }
  if (!values_.empty()) {
    output << '\n';
  }
  output << "}";
  return output.str();
}

}  // namespace fmcw
