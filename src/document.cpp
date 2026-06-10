#include "sumt/document.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace sumt {
namespace {

constexpr std::size_t kPageRecords = 128;

std::uintmax_t checked_file_size(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    throw std::runtime_error("failed to read file size for '" + path.string() +
                             "': " + error.message());
  }
  return size;
}

void write_all(std::ofstream& output, const std::vector<std::uint8_t>& bytes) {
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("failed to write output file");
  }
}

}  // namespace

Document Document::open(Config config, std::filesystem::path file_path) {
  if (config.record_length == 0) {
    throw std::runtime_error("record_length must be greater than 0");
  }

  const auto size = checked_file_size(file_path);
  if (size % config.record_length != 0) {
    throw std::runtime_error("file size is not a multiple of record_length");
  }

  const auto record_count = static_cast<std::size_t>(size / config.record_length);
  return Document(std::move(config), std::move(file_path), record_count);
}

Document::Document(Config config, std::filesystem::path file_path,
                   std::size_t original_record_count)
    : config_(std::move(config)),
      file_path_(std::move(file_path)),
      original_record_count_(original_record_count) {
  rows_.reserve(original_record_count_);
  for (std::size_t index = 0; index < original_record_count_; ++index) {
    rows_.push_back(make_original_entry(index, config_.fields.size()));
  }
}

bool Document::is_visible_byte(unsigned char byte) {
  return byte >= 0x20 && byte <= 0x7e;
}

Document::RowEntry Document::make_original_entry(std::size_t original_index,
                                                 std::size_t field_count) {
  RowEntry entry;
  entry.inserted = false;
  entry.original_index = original_index;
  entry.field_patches.resize(field_count);
  return entry;
}

void Document::ensure_row(std::size_t row_index) const {
  if (row_index >= rows_.size()) {
    throw std::out_of_range("row index out of range");
  }
}

void Document::ensure_field(std::size_t field_index) const {
  if (field_index >= config_.fields.size()) {
    throw std::out_of_range("field index out of range");
  }
}

RowView Document::row_view(std::size_t row_index) const {
  ensure_row(row_index);
  RowView view;
  view.index = row_index;
  view.deleted = rows_[row_index].deleted;
  view.inserted = rows_[row_index].inserted;
  view.fields.reserve(config_.fields.size());
  for (std::size_t field = 0; field < config_.fields.size(); ++field) {
    view.fields.push_back(field_text(row_index, field));
  }
  return view;
}

std::vector<RowView> Document::page(std::size_t first_row, std::size_t count) const {
  std::vector<RowView> rows;
  if (first_row >= rows_.size() || count == 0) {
    return rows;
  }

  const auto end = std::min(rows_.size(), first_row + count);
  rows.reserve(end - first_row);
  for (std::size_t row = first_row; row < end; ++row) {
    rows.push_back(row_view(row));
  }
  return rows;
}

void Document::load_page_for_original(std::size_t original_index) const {
  if (original_index >= original_record_count_) {
    throw std::out_of_range("original record index out of range");
  }
  if (page_cache_valid_ && original_index >= page_cache_start_ &&
      original_index < page_cache_start_ + page_cache_count_) {
    return;
  }

  const std::size_t start = (original_index / kPageRecords) * kPageRecords;
  const std::size_t count = std::min(kPageRecords, original_record_count_ - start);
  page_cache_.assign(count * config_.record_length, 0);

  std::ifstream input(file_path_, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open data file: " + file_path_.string());
  }

  const auto offset = static_cast<std::streamoff>(start * config_.record_length);
  input.seekg(offset);
  input.read(reinterpret_cast<char*>(page_cache_.data()),
             static_cast<std::streamsize>(page_cache_.size()));
  if (input.gcount() != static_cast<std::streamsize>(page_cache_.size())) {
    throw std::runtime_error("failed to read a full record page");
  }

  page_cache_start_ = start;
  page_cache_count_ = count;
  page_cache_valid_ = true;
}

void Document::invalidate_page_cache() const {
  page_cache_valid_ = false;
  page_cache_start_ = 0;
  page_cache_count_ = 0;
  page_cache_.clear();
}

std::vector<std::uint8_t> Document::read_original_record(std::size_t original_index) const {
  load_page_for_original(original_index);
  const std::size_t offset = (original_index - page_cache_start_) * config_.record_length;
  return std::vector<std::uint8_t>(page_cache_.begin() + static_cast<std::ptrdiff_t>(offset),
                                   page_cache_.begin() +
                                       static_cast<std::ptrdiff_t>(offset + config_.record_length));
}

std::vector<std::uint8_t> Document::record_bytes(std::size_t row_index) const {
  ensure_row(row_index);
  const auto& row = rows_[row_index];
  if (row.inserted) {
    return row.record;
  }

  auto record = read_original_record(row.original_index);
  for (std::size_t field_index = 0; field_index < row.field_patches.size(); ++field_index) {
    if (!row.field_patches[field_index].has_value()) {
      continue;
    }
    const auto& field = config_.fields[field_index];
    const auto& patch = row.field_patches[field_index].value();
    std::copy(patch.begin(), patch.end(), record.begin() + static_cast<std::ptrdiff_t>(field.offset));
  }
  return record;
}

std::string Document::raw_field_text(std::size_t row_index, std::size_t field_index) const {
  ensure_field(field_index);
  const auto record = record_bytes(row_index);
  const auto& field = config_.fields[field_index];
  return std::string(record.begin() + static_cast<std::ptrdiff_t>(field.offset),
                     record.begin() + static_cast<std::ptrdiff_t>(field.offset + field.length));
}

std::string Document::field_text(std::size_t row_index, std::size_t field_index) const {
  std::string text = raw_field_text(row_index, field_index);
  for (char& ch : text) {
    if (!is_visible_byte(static_cast<unsigned char>(ch))) {
      ch = '.';
    }
  }
  return text;
}

std::string Document::padded_field_value(std::size_t field_index, const std::string& value) const {
  ensure_field(field_index);
  const auto& field = config_.fields[field_index];
  if (value.size() > field.length) {
    throw std::runtime_error("field value is longer than configured field length");
  }
  for (const char ch : value) {
    if (!is_visible_byte(static_cast<unsigned char>(ch))) {
      throw std::runtime_error("field value contains a non-visible byte");
    }
  }

  std::string padded = value;
  padded.resize(field.length, ' ');
  return padded;
}

void Document::set_field_text(RowEntry& row, std::size_t field_index, const std::string& text) {
  ensure_field(field_index);
  const auto& field = config_.fields[field_index];
  if (text.size() != field.length) {
    throw std::runtime_error("internal error: field patch has an unexpected length");
  }

  if (row.inserted) {
    std::copy(text.begin(), text.end(),
              row.record.begin() + static_cast<std::ptrdiff_t>(field.offset));
  } else {
    row.field_patches[field_index] = text;
  }
}

void Document::restore_original_patch(RowEntry& row, std::size_t field_index,
                                      const std::optional<std::string>& patch) {
  if (row.inserted) {
    throw std::runtime_error("internal error: cannot restore a patch on an inserted row");
  }
  ensure_field(field_index);
  row.field_patches[field_index] = patch;
}

void Document::edit_field(std::size_t row_index, std::size_t field_index,
                          const std::string& value) {
  ensure_row(row_index);
  ensure_field(field_index);
  if (rows_[row_index].deleted) {
    throw std::runtime_error("cannot edit a row marked for deletion");
  }

  const std::string padded = padded_field_value(field_index, value);
  const std::string previous_text = raw_field_text(row_index, field_index);
  if (previous_text == padded) {
    return;
  }

  Action action;
  action.type = ActionType::Edit;
  action.row = row_index;
  action.field = field_index;
  action.previous_text = previous_text;
  action.new_text = padded;
  if (!rows_[row_index].inserted) {
    action.previous_patch = rows_[row_index].field_patches[field_index];
  }

  apply_action(action);
  push_action(std::move(action));
}

void Document::copy_row(std::size_t row_index) {
  ensure_row(row_index);
  clipboard_ = record_bytes(row_index);
}

void Document::paste_before(std::size_t row_index) {
  ensure_row(row_index);
  if (!clipboard_.has_value()) {
    throw std::runtime_error("clipboard is empty");
  }

  Action action;
  action.type = ActionType::Insert;
  action.row = row_index;
  action.inserted_entry.inserted = true;
  action.inserted_entry.record = clipboard_.value();
  action.inserted_entry.deleted = false;

  apply_action(action);
  push_action(std::move(action));
}

void Document::paste_after(std::size_t row_index) {
  ensure_row(row_index);
  if (!clipboard_.has_value()) {
    throw std::runtime_error("clipboard is empty");
  }

  Action action;
  action.type = ActionType::Insert;
  action.row = row_index + 1;
  action.inserted_entry.inserted = true;
  action.inserted_entry.record = clipboard_.value();
  action.inserted_entry.deleted = false;

  apply_action(action);
  push_action(std::move(action));
}

std::size_t Document::mark_deleted(std::size_t row_index) {
  return mark_deleted_range(row_index, row_index);
}

std::size_t Document::mark_deleted_range(std::size_t first_row, std::size_t last_row) {
  ensure_row(first_row);
  ensure_row(last_row);
  if (first_row > last_row) {
    std::swap(first_row, last_row);
  }

  Action action;
  action.type = ActionType::Delete;
  for (std::size_t row = first_row; row <= last_row; ++row) {
    action.previous_deleted.emplace_back(row, rows_[row].deleted);
  }

  std::size_t changed = 0;
  for (const auto& item : action.previous_deleted) {
    if (!item.second) {
      ++changed;
    }
  }
  if (changed == 0) {
    return 0;
  }

  apply_action(action);
  push_action(std::move(action));
  return changed;
}

void Document::apply_action(const Action& action) {
  switch (action.type) {
    case ActionType::Edit: {
      ensure_row(action.row);
      if (rows_[action.row].inserted) {
        set_field_text(rows_[action.row], action.field, action.new_text);
      } else {
        set_field_text(rows_[action.row], action.field, action.new_text);
      }
      break;
    }
    case ActionType::Insert:
      if (action.row > rows_.size()) {
        throw std::out_of_range("insert row index out of range");
      }
      rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(action.row),
                   action.inserted_entry);
      break;
    case ActionType::Delete:
      for (const auto& item : action.previous_deleted) {
        ensure_row(item.first);
        rows_[item.first].deleted = true;
      }
      break;
  }
}

void Document::revert_action(const Action& action) {
  switch (action.type) {
    case ActionType::Edit: {
      ensure_row(action.row);
      if (rows_[action.row].inserted) {
        set_field_text(rows_[action.row], action.field, action.previous_text);
      } else {
        restore_original_patch(rows_[action.row], action.field, action.previous_patch);
      }
      break;
    }
    case ActionType::Insert:
      ensure_row(action.row);
      rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(action.row));
      break;
    case ActionType::Delete:
      for (const auto& item : action.previous_deleted) {
        ensure_row(item.first);
        rows_[item.first].deleted = item.second;
      }
      break;
  }
}

void Document::push_action(Action action) {
  undo_stack_.push_back(std::move(action));
  redo_stack_.clear();
  dirty_ = true;
}

bool Document::undo() {
  if (undo_stack_.empty()) {
    return false;
  }

  Action action = undo_stack_.back();
  undo_stack_.pop_back();
  revert_action(action);
  redo_stack_.push_back(std::move(action));
  dirty_ = !undo_stack_.empty();
  return true;
}

bool Document::redo() {
  if (redo_stack_.empty()) {
    return false;
  }

  Action action = redo_stack_.back();
  redo_stack_.pop_back();
  apply_action(action);
  undo_stack_.push_back(std::move(action));
  dirty_ = true;
  return true;
}

void Document::save() {
  const std::filesystem::path temp_path = file_path_.string() + ".tmp";
  std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("failed to open temporary output file: " + temp_path.string());
  }

  std::size_t written_records = 0;
  try {
    for (const auto& row : rows_) {
      if (row.deleted) {
        continue;
      }

      std::vector<std::uint8_t> record;
      if (row.inserted) {
        record = row.record;
      } else {
        record = read_original_record(row.original_index);
        for (std::size_t field_index = 0; field_index < row.field_patches.size(); ++field_index) {
          if (!row.field_patches[field_index].has_value()) {
            continue;
          }
          const auto& field = config_.fields[field_index];
          const auto& patch = row.field_patches[field_index].value();
          std::copy(patch.begin(), patch.end(),
                    record.begin() + static_cast<std::ptrdiff_t>(field.offset));
        }
      }

      write_all(output, record);
      ++written_records;
    }
    output.close();
    if (!output) {
      throw std::runtime_error("failed to close temporary output file");
    }

    std::error_code error;
    std::filesystem::rename(temp_path, file_path_, error);
    if (error) {
      throw std::runtime_error("failed to replace original file: " + error.message());
    }
  } catch (...) {
    output.close();
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    throw;
  }

  rows_.clear();
  rows_.reserve(written_records);
  for (std::size_t index = 0; index < written_records; ++index) {
    rows_.push_back(make_original_entry(index, config_.fields.size()));
  }
  original_record_count_ = written_records;
  undo_stack_.clear();
  redo_stack_.clear();
  dirty_ = false;
  invalidate_page_cache();
}

}  // namespace sumt
