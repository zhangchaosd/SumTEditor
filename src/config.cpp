#include "sumt/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace sumt {
namespace {

std::string trim(const std::string& value) {
  auto begin = value.begin();
  while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }

  auto end = value.end();
  while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }

  return std::string(begin, end);
}

std::vector<std::string> split_csv(const std::string& value) {
  std::vector<std::string> parts;
  std::stringstream stream(value);
  std::string part;
  while (std::getline(stream, part, ',')) {
    parts.push_back(trim(part));
  }
  return parts;
}

std::size_t parse_size(const std::string& value, const std::string& label, int line_number) {
  const std::string trimmed = trim(value);
  if (trimmed.empty()) {
    throw std::runtime_error("line " + std::to_string(line_number) + ": " + label +
                             " must not be empty");
  }
  if (!std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    throw std::runtime_error("line " + std::to_string(line_number) + ": " + label +
                             " must be a non-negative integer");
  }

  unsigned long long parsed = 0;
  try {
    parsed = std::stoull(trimmed);
  } catch (const std::exception&) {
    throw std::runtime_error("line " + std::to_string(line_number) + ": " + label +
                             " is out of range");
  }

  if (parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("line " + std::to_string(line_number) + ": " + label +
                             " is out of range");
  }
  return static_cast<std::size_t>(parsed);
}

FieldSpec parse_field_spec(const std::string& value, int line_number) {
  const auto parts = split_csv(value);
  if (parts.size() != 3 && parts.size() != 4) {
    throw std::runtime_error("line " + std::to_string(line_number) +
                             ": field must be name,offset,length[,display_width]");
  }

  FieldSpec field;
  field.name = parts[0];
  if (field.name.empty()) {
    throw std::runtime_error("line " + std::to_string(line_number) +
                             ": field name must not be empty");
  }
  field.offset = parse_size(parts[1], "field offset", line_number);
  field.length = parse_size(parts[2], "field length", line_number);
  field.display_width = parts.size() == 4 ? parse_size(parts[3], "field display_width", line_number)
                                          : field.length;
  return field;
}

void validate_config(const Config& config, const std::string& source_name) {
  if (config.record_length == 0) {
    throw std::runtime_error(source_name + ": record_length must be configured and greater than 0");
  }
  if (config.fields.empty()) {
    throw std::runtime_error(source_name + ": at least one field must be configured");
  }

  std::set<std::string> names;
  for (const auto& field : config.fields) {
    if (!names.insert(field.name).second) {
      throw std::runtime_error(source_name + ": duplicate field name '" + field.name + "'");
    }
    if (field.length == 0) {
      throw std::runtime_error(source_name + ": field '" + field.name + "' length must be > 0");
    }
    if (field.display_width == 0) {
      throw std::runtime_error(source_name + ": field '" + field.name +
                               "' display_width must be > 0");
    }
    if (field.offset > config.record_length ||
        field.length > config.record_length - field.offset) {
      throw std::runtime_error(source_name + ": field '" + field.name +
                               "' extends past record_length");
    }
  }
}

}  // namespace

Config parse_config_stream(std::istream& input, const std::string& source_name) {
  Config config;
  bool saw_record_length = false;
  bool saw_file = false;
  std::string line;
  int line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
      continue;
    }

    const auto equals = trimmed.find('=');
    if (equals == std::string::npos) {
      throw std::runtime_error("line " + std::to_string(line_number) +
                               ": expected key = value");
    }

    const std::string key = trim(trimmed.substr(0, equals));
    const std::string value = trim(trimmed.substr(equals + 1));

    if (key == "file") {
      if (saw_file) {
        throw std::runtime_error("line " + std::to_string(line_number) +
                                 ": file configured more than once");
      }
      if (value.empty()) {
        throw std::runtime_error("line " + std::to_string(line_number) +
                                 ": file must not be empty");
      }
      config.file_path = value;
      saw_file = true;
    } else if (key == "record_length") {
      if (saw_record_length) {
        throw std::runtime_error("line " + std::to_string(line_number) +
                                 ": record_length configured more than once");
      }
      config.record_length = parse_size(value, "record_length", line_number);
      saw_record_length = true;
    } else if (key == "field") {
      config.fields.push_back(parse_field_spec(value, line_number));
    } else {
      throw std::runtime_error("line " + std::to_string(line_number) + ": unknown key '" + key +
                               "'");
    }
  }

  validate_config(config, source_name);
  return config;
}

Config parse_config_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open config file: " + path);
  }
  return parse_config_stream(input, path);
}

}  // namespace sumt
