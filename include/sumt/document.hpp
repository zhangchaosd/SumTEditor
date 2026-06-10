#pragma once

#include "sumt/config.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sumt {

struct RowView {
  std::size_t index = 0;
  bool deleted = false;
  bool inserted = false;
  std::vector<std::string> fields;
};

class Document {
 public:
  static Document open(Config config, std::filesystem::path file_path);

  const Config& config() const { return config_; }
  const std::filesystem::path& file_path() const { return file_path_; }

  std::size_t row_count() const { return rows_.size(); }
  bool empty() const { return rows_.empty(); }
  bool dirty() const { return dirty_; }
  bool can_undo() const { return !undo_stack_.empty(); }
  bool can_redo() const { return !redo_stack_.empty(); }
  bool has_clipboard() const { return clipboard_.has_value(); }

  RowView row_view(std::size_t row_index) const;
  std::vector<RowView> page(std::size_t first_row, std::size_t count) const;
  std::vector<std::uint8_t> record_bytes(std::size_t row_index) const;
  std::string raw_field_text(std::size_t row_index, std::size_t field_index) const;
  std::string field_text(std::size_t row_index, std::size_t field_index) const;

  void edit_field(std::size_t row_index, std::size_t field_index, const std::string& value);
  void copy_row(std::size_t row_index);
  void paste_before(std::size_t row_index);
  void paste_after(std::size_t row_index);
  std::size_t mark_deleted(std::size_t row_index);
  std::size_t mark_deleted_range(std::size_t first_row, std::size_t last_row);
  bool undo();
  bool redo();
  void save();

 private:
  struct RowEntry {
    bool inserted = false;
    bool deleted = false;
    std::size_t original_index = 0;
    std::vector<std::uint8_t> record;
    std::vector<std::optional<std::string>> field_patches;
  };

  enum class ActionType {
    Edit,
    Insert,
    Delete,
  };

  struct Action {
    ActionType type = ActionType::Edit;
    std::size_t row = 0;
    std::size_t field = 0;
    std::string previous_text;
    std::string new_text;
    std::optional<std::string> previous_patch;
    RowEntry inserted_entry;
    std::vector<std::pair<std::size_t, bool>> previous_deleted;
  };

  Document(Config config, std::filesystem::path file_path, std::size_t original_record_count);

  static bool is_visible_byte(unsigned char byte);
  static RowEntry make_original_entry(std::size_t original_index, std::size_t field_count);

  void ensure_row(std::size_t row_index) const;
  void ensure_field(std::size_t field_index) const;
  std::vector<std::uint8_t> read_original_record(std::size_t original_index) const;
  void load_page_for_original(std::size_t original_index) const;
  void invalidate_page_cache() const;
  std::string padded_field_value(std::size_t field_index, const std::string& value) const;
  void set_field_text(RowEntry& row, std::size_t field_index, const std::string& text);
  void restore_original_patch(RowEntry& row, std::size_t field_index,
                              const std::optional<std::string>& patch);
  void apply_action(const Action& action);
  void revert_action(const Action& action);
  void push_action(Action action);

  Config config_;
  std::filesystem::path file_path_;
  std::size_t original_record_count_ = 0;
  std::vector<RowEntry> rows_;
  std::optional<std::vector<std::uint8_t>> clipboard_;
  std::vector<Action> undo_stack_;
  std::vector<Action> redo_stack_;
  bool dirty_ = false;

  mutable bool page_cache_valid_ = false;
  mutable std::size_t page_cache_start_ = 0;
  mutable std::size_t page_cache_count_ = 0;
  mutable std::vector<std::uint8_t> page_cache_;
};

}  // namespace sumt
