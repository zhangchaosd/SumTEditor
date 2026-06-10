#include "sumt/config.hpp"
#include "sumt/document.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace sumt {
int run_tui(Document& document);
}

namespace {

void print_usage(std::ostream& output) {
  output << "Usage: sumteditor CONFIG [FILE]\n"
         << "\n"
         << "Open a fixed-length-record binary file using CONFIG.\n"
         << "FILE overrides the optional file entry in CONFIG.\n";
}

std::filesystem::path resolve_file_path(const std::filesystem::path& config_path,
                                        const sumt::Config& config, int argc, char** argv) {
  if (argc == 3) {
    return std::filesystem::path(argv[2]);
  }

  if (!config.file_path.has_value()) {
    throw std::runtime_error("no data file was provided and CONFIG has no file entry");
  }

  std::filesystem::path data_path(config.file_path.value());
  if (data_path.is_relative()) {
    data_path = config_path.parent_path() / data_path;
  }
  return data_path;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--help") {
      print_usage(std::cout);
      return 0;
    }
    if (argc < 2 || argc > 3) {
      print_usage(std::cerr);
      return 2;
    }

    const std::filesystem::path config_path(argv[1]);
    auto config = sumt::parse_config_file(config_path.string());
    const auto file_path = resolve_file_path(config_path, config, argc, argv);
    auto document = sumt::Document::open(std::move(config), file_path);
    return sumt::run_tui(document);
  } catch (const std::exception& error) {
    std::cerr << "sumteditor: " << error.what() << '\n';
    return 1;
  }
}
