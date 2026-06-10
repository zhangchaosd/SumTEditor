#include "sumt/document.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace sumt {
namespace {

using ftxui::CatchEvent;
using ftxui::Component;
using ftxui::Element;
using ftxui::Event;
using ftxui::Renderer;
using ftxui::ScreenInteractive;
using ftxui::border;
using ftxui::bold;
using ftxui::dim;
using ftxui::filler;
using ftxui::hbox;
using ftxui::inverted;
using ftxui::separator;
using ftxui::size;
using ftxui::text;
using ftxui::vbox;

enum class Mode {
  Normal,
  Edit,
  Command,
  Visual,
};

std::string truncate_for_width(std::string value, std::size_t width) {
  if (value.size() > width) {
    value.resize(width);
  }
  return value;
}

class TuiApp {
 public:
  explicit TuiApp(Document& document) : document_(document) {}

  int run() {
    auto screen = ScreenInteractive::TerminalOutput();
    auto quit = screen.ExitLoopClosure();

    Component component = Renderer([&] { return render(); });
    component = CatchEvent(component, [&](Event event) { return handle_event(event, quit); });

    screen.Loop(component);
    return 0;
  }

 private:
  static constexpr std::size_t kPageSize = 20;

  Element render() const {
    std::vector<Element> lines;
    lines.push_back(render_header());
    lines.push_back(separator());

    const auto rows = document_.page(first_row_, kPageSize);
    if (rows.empty()) {
      lines.push_back(text("No records") | dim);
    } else {
      for (const auto& row : rows) {
        lines.push_back(render_row(row));
      }
    }

    lines.push_back(filler());
    lines.push_back(separator());
    lines.push_back(render_status());
    return vbox(std::move(lines)) | border;
  }

  Element render_header() const {
    std::vector<Element> cells;
    cells.push_back(text("  #") | size(ftxui::WIDTH, ftxui::EQUAL, 8));
    for (const auto& field : document_.config().fields) {
      const auto width = static_cast<int>(std::min<std::size_t>(field.display_width, 40));
      cells.push_back(separator());
      cells.push_back(text(truncate_for_width(field.name, field.display_width)) | bold |
                      size(ftxui::WIDTH, ftxui::EQUAL, width));
    }
    return hbox(std::move(cells));
  }

  Element render_row(const RowView& row) const {
    std::vector<Element> cells;
    std::ostringstream prefix;
    prefix << (row.index == cursor_row_ ? ">" : " ");
    prefix << (row.deleted ? "D" : row.inserted ? "+" : " ");
    prefix << " ";
    prefix << row.index;
    cells.push_back(text(prefix.str()) | size(ftxui::WIDTH, ftxui::EQUAL, 8));

    for (std::size_t field = 0; field < row.fields.size(); ++field) {
      const auto& spec = document_.config().fields[field];
      const auto width = static_cast<int>(std::min<std::size_t>(spec.display_width, 40));
      auto cell = text(truncate_for_width(row.fields[field], spec.display_width)) |
                  size(ftxui::WIDTH, ftxui::EQUAL, width);
      if (row.index == cursor_row_ && field == cursor_field_) {
        cell = cell | inverted;
      }
      cells.push_back(separator());
      cells.push_back(cell);
    }

    auto element = hbox(std::move(cells));
    if (row.deleted) {
      element = element | dim;
    }
    if (row.index == cursor_row_) {
      element = element | bold;
    }
    return element;
  }

  Element render_status() const {
    std::string mode_name;
    switch (mode_) {
      case Mode::Normal:
        mode_name = "NORMAL";
        break;
      case Mode::Edit:
        mode_name = "EDIT";
        break;
      case Mode::Command:
        mode_name = "COMMAND";
        break;
      case Mode::Visual:
        mode_name = "VISUAL";
        break;
    }

    std::ostringstream left;
    left << mode_name << "  "
         << "row " << (document_.empty() ? 0 : cursor_row_ + 1) << "/" << document_.row_count()
         << "  field " << (cursor_field_ + 1) << "/" << document_.config().fields.size();
    if (document_.dirty()) {
      left << "  modified";
    }
    if (mode_ == Mode::Visual) {
      left << "  selecting " << selected_row_count() << " row";
      if (selected_row_count() != 1) {
        left << "s";
      }
    }
    if (mode_ == Mode::Edit) {
      left << "  value: " << edit_buffer_;
    } else if (mode_ == Mode::Command) {
      left << "  :" << command_;
    } else if (!status_.empty()) {
      left << "  " << status_;
    }

    return hbox({text(left.str()), filler(),
                 text("j/k h/l e y p/P dd v u Ctrl-r :w :q :wq :q!") | dim});
  }

  bool handle_event(const Event& event, const std::function<void()>& quit) {
    try {
      switch (mode_) {
        case Mode::Normal:
          return handle_normal(event, quit);
        case Mode::Edit:
          return handle_edit(event);
        case Mode::Command:
          return handle_command(event, quit);
        case Mode::Visual:
          return handle_visual(event);
      }
    } catch (const std::exception& error) {
      status_ = error.what();
      mode_ = Mode::Normal;
      pending_delete_ = false;
      return true;
    }
    return false;
  }

  bool handle_normal(const Event& event, const std::function<void()>& quit) {
    if (event == Event::ArrowDown || event == Event::Character("j")) {
      pending_delete_ = false;
      move_row(1);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character("k")) {
      pending_delete_ = false;
      move_row(-1);
      return true;
    }
    if (event == Event::ArrowLeft || event == Event::Character("h")) {
      pending_delete_ = false;
      move_field(-1);
      return true;
    }
    if (event == Event::Tab) {
      pending_delete_ = false;
      move_field_wrapped(1);
      return true;
    }
    if (event == Event::ArrowRight || event == Event::Character("l")) {
      pending_delete_ = false;
      move_field(1);
      return true;
    }
    if (event == Event::Return || event == Event::Character("e")) {
      pending_delete_ = false;
      start_edit();
      return true;
    }
    if (event == Event::Character("y")) {
      pending_delete_ = false;
      document_.copy_row(cursor_row_);
      status_ = "copied row";
      return true;
    }
    if (event == Event::Character("p")) {
      pending_delete_ = false;
      document_.paste_after(cursor_row_);
      move_row(1);
      status_ = "pasted row";
      return true;
    }
    if (event == Event::Character("P")) {
      pending_delete_ = false;
      document_.paste_before(cursor_row_);
      status_ = "pasted row";
      return true;
    }
    if (event == Event::Character("d")) {
      if (pending_delete_) {
        const auto changed = document_.mark_deleted(cursor_row_);
        status_ = changed == 0 ? "row was already marked for deletion"
                               : "marked 1 row for deletion";
        pending_delete_ = false;
      } else {
        status_ = "press d again to mark the row for deletion";
        pending_delete_ = true;
      }
      return true;
    }
    if (event == Event::Character("v")) {
      pending_delete_ = false;
      visual_anchor_ = cursor_row_;
      mode_ = Mode::Visual;
      status_ = "visual row selection";
      return true;
    }
    if (event == Event::Character("u")) {
      pending_delete_ = false;
      const bool changed = document_.undo();
      clamp_cursor();
      status_ = changed ? "undo" : "nothing to undo";
      return true;
    }
    if (event.input() == "\x12") {
      pending_delete_ = false;
      const bool changed = document_.redo();
      clamp_cursor();
      status_ = changed ? "redo" : "nothing to redo";
      return true;
    }
    if (event == Event::Character(":")) {
      pending_delete_ = false;
      command_.clear();
      mode_ = Mode::Command;
      return true;
    }
    if (event == Event::Escape) {
      pending_delete_ = false;
      status_.clear();
      return true;
    }
    if (event.input() == "\x03") {
      quit();
      return true;
    }
    return false;
  }

  bool handle_edit(const Event& event) {
    if (event == Event::Escape) {
      mode_ = Mode::Normal;
      status_ = "edit cancelled";
      return true;
    }
    if (event == Event::Return) {
      document_.edit_field(cursor_row_, cursor_field_, edit_buffer_);
      mode_ = Mode::Normal;
      status_ = "field updated";
      return true;
    }
    if (event == Event::Backspace || event == Event::Delete) {
      if (!edit_buffer_.empty()) {
        edit_buffer_.pop_back();
      }
      return true;
    }
    if (event.is_character()) {
      const auto value = event.character();
      if (value.size() == 1) {
        edit_buffer_ += value;
      }
      return true;
    }
    return false;
  }

  bool handle_command(const Event& event, const std::function<void()>& quit) {
    if (event == Event::Escape) {
      mode_ = Mode::Normal;
      status_ = "command cancelled";
      return true;
    }
    if (event == Event::Backspace || event == Event::Delete) {
      if (!command_.empty()) {
        command_.pop_back();
      }
      return true;
    }
    if (event == Event::Return) {
      execute_command(quit);
      return true;
    }
    if (event.is_character()) {
      command_ += event.character();
      return true;
    }
    return false;
  }

  bool handle_visual(const Event& event) {
    if (event == Event::Escape) {
      mode_ = Mode::Normal;
      status_ = "visual cancelled";
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character("j")) {
      move_row(1);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character("k")) {
      move_row(-1);
      return true;
    }
    if (event == Event::Character("d")) {
      const auto first = std::min(visual_anchor_, cursor_row_);
      const auto last = std::max(visual_anchor_, cursor_row_);
      const auto changed = document_.mark_deleted_range(first, last);
      mode_ = Mode::Normal;
      status_ = changed == 0 ? "selected rows were already marked for deletion"
                             : "marked " + std::to_string(changed) + " selected row" +
                                   (changed == 1 ? "" : "s") + " for deletion";
      return true;
    }
    return false;
  }

  void start_edit() {
    if (document_.empty()) {
      status_ = "no record to edit";
      return;
    }
    edit_buffer_ = document_.field_text(cursor_row_, cursor_field_);
    while (!edit_buffer_.empty() && edit_buffer_.back() == ' ') {
      edit_buffer_.pop_back();
    }
    mode_ = Mode::Edit;
    status_.clear();
  }

  void execute_command(const std::function<void()>& quit) {
    if (command_ == "w") {
      document_.save();
      status_ = "saved";
      mode_ = Mode::Normal;
      clamp_cursor();
      return;
    }
    if (command_ == "wq") {
      document_.save();
      quit();
      return;
    }
    if (command_ == "q") {
      if (document_.dirty()) {
        status_ = "unsaved changes; use :q! to quit";
        mode_ = Mode::Normal;
        return;
      }
      quit();
      return;
    }
    if (command_ == "q!") {
      quit();
      return;
    }

    status_ = "unknown command: " + command_;
    mode_ = Mode::Normal;
  }

  void move_row(int delta) {
    if (document_.empty()) {
      cursor_row_ = 0;
      first_row_ = 0;
      return;
    }

    if (delta < 0) {
      const auto amount = static_cast<std::size_t>(-delta);
      cursor_row_ = amount > cursor_row_ ? 0 : cursor_row_ - amount;
    } else {
      cursor_row_ = std::min(document_.row_count() - 1,
                             cursor_row_ + static_cast<std::size_t>(delta));
    }

    if (cursor_row_ < first_row_) {
      first_row_ = cursor_row_;
    } else if (cursor_row_ >= first_row_ + kPageSize) {
      first_row_ = cursor_row_ - kPageSize + 1;
    }
  }

  void move_field(int delta) {
    if (document_.config().fields.empty()) {
      cursor_field_ = 0;
      return;
    }

    if (delta < 0) {
      const auto amount = static_cast<std::size_t>(-delta);
      cursor_field_ = amount > cursor_field_ ? 0 : cursor_field_ - amount;
    } else {
      cursor_field_ = std::min(document_.config().fields.size() - 1,
                               cursor_field_ + static_cast<std::size_t>(delta));
    }
  }

  void move_field_wrapped(int delta) {
    const auto field_count = document_.config().fields.size();
    if (field_count == 0) {
      cursor_field_ = 0;
      return;
    }

    const auto signed_count = static_cast<int>(field_count);
    const auto next = (static_cast<int>(cursor_field_) + delta + signed_count) % signed_count;
    cursor_field_ = static_cast<std::size_t>(next);
  }

  std::size_t selected_row_count() const {
    if (document_.empty()) {
      return 0;
    }
    const auto first = std::min(visual_anchor_, cursor_row_);
    const auto last = std::max(visual_anchor_, cursor_row_);
    return last - first + 1;
  }

  void clamp_cursor() {
    if (document_.empty()) {
      cursor_row_ = 0;
      first_row_ = 0;
    } else if (cursor_row_ >= document_.row_count()) {
      cursor_row_ = document_.row_count() - 1;
    }
    if (!document_.config().fields.empty() && cursor_field_ >= document_.config().fields.size()) {
      cursor_field_ = document_.config().fields.size() - 1;
    }
    if (cursor_row_ < first_row_) {
      first_row_ = cursor_row_;
    } else if (cursor_row_ >= first_row_ + kPageSize) {
      first_row_ = cursor_row_ >= kPageSize ? cursor_row_ - kPageSize + 1 : 0;
    }
  }

  Document& document_;
  Mode mode_ = Mode::Normal;
  std::size_t cursor_row_ = 0;
  std::size_t cursor_field_ = 0;
  std::size_t first_row_ = 0;
  std::size_t visual_anchor_ = 0;
  bool pending_delete_ = false;
  std::string edit_buffer_;
  std::string command_;
  std::string status_ = "ready";
};

}  // namespace

int run_tui(Document& document) {
  TuiApp app(document);
  return app.run();
}

}  // namespace sumt
