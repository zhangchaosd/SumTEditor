#include "sumt/config.hpp"
#include "sumt/document.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

sumt::Config config_from_text(const std::string& text) {
  std::istringstream input(text);
  return sumt::parse_config_stream(input, "test.conf");
}

void expect_throw(const std::function<void()>& fn) {
  bool threw = false;
  try {
    fn();
  } catch (const std::exception&) {
    threw = true;
  }
  assert(threw);
}

fs::path make_temp_dir() {
  const auto base = fs::temp_directory_path();
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto candidate =
        base / ("sumt_tests_" + std::to_string(
                                  std::chrono::steady_clock::now().time_since_epoch().count()) +
                "_" + std::to_string(attempt));
    std::error_code error;
    if (fs::create_directories(candidate, error)) {
      return candidate;
    }
  }
  throw std::runtime_error("failed to create temporary test directory");
}

void write_binary(const fs::path& path, const std::vector<std::string>& records) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output);
  for (const auto& record : records) {
    output.write(record.data(), static_cast<std::streamsize>(record.size()));
  }
}

std::string read_all(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

sumt::Config sample_config() {
  return config_from_text(R"(
record_length = 8
field = id,0,3
field = name,3,5
)");
}

void test_config_parser() {
  const auto config = config_from_text(R"(
# demo
file = data.bin
record_length = 128
field = id,0,8
field = name,8,24,20
field = status,32,1
)");

  assert(config.file_path.has_value());
  assert(config.file_path.value() == "data.bin");
  assert(config.record_length == 128);
  assert(config.fields.size() == 3);
  assert(config.fields[1].name == "name");
  assert(config.fields[1].offset == 8);
  assert(config.fields[1].length == 24);
  assert(config.fields[1].display_width == 20);

  expect_throw([] { config_from_text("field = id,0,1\n"); });
  expect_throw([] { config_from_text("record_length = abc\nfield = id,0,1\n"); });
  expect_throw([] { config_from_text("record_length = 2\nfield = id,1,2\n"); });
  expect_throw([] { config_from_text("record_length = 4\nfield = id,0,1\nfield = id,1,1\n"); });
}

void test_file_validation_and_display(const fs::path& dir) {
  auto config = sample_config();
  const auto bad_file = dir / "bad.bin";
  write_binary(bad_file, {"abc"});
  expect_throw([&] { (void)sumt::Document::open(config, bad_file); });

  auto display_config = config_from_text(R"(
record_length = 4
field = raw,0,4
)");
  const auto display_file = dir / "display.bin";
  write_binary(display_file, {std::string("AB\001D", 4)});
  auto document = sumt::Document::open(display_config, display_file);
  assert(document.row_count() == 1);
  assert(document.field_text(0, 0) == "AB.D");
}

void test_editing_undo_redo_and_save(const fs::path& dir) {
  auto config = sample_config();
  const auto data_file = dir / "records.bin";
  write_binary(data_file, {"001ALPHA", "002BRAVO", "003CHARL"});

  auto document = sumt::Document::open(config, data_file);
  assert(document.row_count() == 3);
  assert(document.raw_field_text(0, 1) == "ALPHA");

  document.edit_field(0, 1, "AX");
  assert(document.raw_field_text(0, 1) == "AX   ");
  assert(document.dirty());

  document.undo();
  assert(document.raw_field_text(0, 1) == "ALPHA");
  document.redo();
  assert(document.raw_field_text(0, 1) == "AX   ");

  expect_throw([&] { document.edit_field(0, 1, "TOOLONG"); });
  expect_throw([&] { document.edit_field(0, 1, std::string("A\n")); });

  document.copy_row(0);
  assert(document.has_clipboard());
  document.paste_after(0);
  assert(document.row_count() == 4);
  assert(document.raw_field_text(1, 0) == "001");
  assert(document.raw_field_text(1, 1) == "AX   ");

  document.mark_deleted(2);
  assert(document.row_view(2).deleted);
  assert(document.undo());
  assert(!document.row_view(2).deleted);
  assert(document.redo());
  assert(document.row_view(2).deleted);

  document.save();
  assert(!document.dirty());
  assert(document.row_count() == 3);
  assert(read_all(data_file) == "001AX   001AX   003CHARL");
}

void test_range_delete_undo_redo(const fs::path& dir) {
  auto config = sample_config();
  const auto data_file = dir / "range.bin";
  write_binary(data_file, {"001ALPHA", "002BRAVO", "003CHARL", "004DELTA"});

  auto document = sumt::Document::open(config, data_file);
  assert(document.mark_deleted(1) == 1);
  assert(document.mark_deleted_range(0, 2) == 2);
  assert(document.row_view(0).deleted);
  assert(document.row_view(1).deleted);
  assert(document.row_view(2).deleted);

  assert(document.undo());
  assert(!document.row_view(0).deleted);
  assert(document.row_view(1).deleted);
  assert(!document.row_view(2).deleted);

  assert(document.redo());
  assert(document.row_view(0).deleted);
  assert(document.row_view(1).deleted);
  assert(document.row_view(2).deleted);

  assert(document.mark_deleted_range(0, 2) == 0);
  assert(document.undo());
  assert(!document.row_view(0).deleted);
  assert(document.row_view(1).deleted);
  assert(!document.row_view(2).deleted);
  assert(document.undo());
  assert(!document.dirty());
  assert(!document.row_view(1).deleted);
  assert(!document.undo());
}

}  // namespace

int main() {
  const auto dir = make_temp_dir();
  try {
    test_config_parser();
    test_file_validation_and_display(dir);
    test_editing_undo_redo_and_save(dir);
    test_range_delete_undo_redo(dir);
    fs::remove_all(dir);
  } catch (...) {
    fs::remove_all(dir);
    throw;
  }
  return 0;
}
