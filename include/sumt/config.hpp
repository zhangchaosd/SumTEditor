#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace sumt {

struct FieldSpec {
  std::string name;
  std::size_t offset = 0;
  std::size_t length = 0;
  std::size_t display_width = 0;
};

struct Config {
  std::optional<std::string> file_path;
  std::size_t record_length = 0;
  std::vector<FieldSpec> fields;
};

Config parse_config_stream(std::istream& input, const std::string& source_name);
Config parse_config_file(const std::string& path);

}  // namespace sumt
