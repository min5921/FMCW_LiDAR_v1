#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace fmcw {

enum class ConfigScalarKind {
  String,
  Integer,
  Number,
  Boolean,
};

struct ConfigScalar {
  std::string text;
  ConfigScalarKind kind = ConfigScalarKind::String;
};

struct ConfigParseError {
  std::size_t line = 0;
  std::size_t column = 0;
  std::string message;
};

class ConfigDocument {
 public:
  using Values = std::map<std::string, ConfigScalar>;

  static bool parseYaml(std::string_view yaml, ConfigDocument& output, ConfigParseError& error);

  const Values& values() const;
  const ConfigScalar* find(std::string_view path) const;
  bool contains(std::string_view path) const;

  void setString(std::string path, std::string value);
  void setInteger(std::string path, std::int64_t value);
  void setNumber(std::string path, double value);
  void setBoolean(std::string path, bool value);
  void setScalar(std::string path, ConfigScalar value);
  void merge(const ConfigDocument& overrides);

  std::string toYaml() const;
  std::string toFlatJson() const;

 private:
  Values values_;
};

}  // namespace fmcw
