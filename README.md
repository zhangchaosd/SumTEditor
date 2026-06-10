# SumTEditor

[![CI](https://github.com/zhangchaosd/SumTEditor/actions/workflows/ci.yml/badge.svg)](https://github.com/zhangchaosd/SumTEditor/actions/workflows/ci.yml)

[简体中文](README.zh-CN.md)

SumTEditor is a C++17 terminal editor for binary files made of fixed-length records.
It is designed for files where every record has the same byte length and a few byte
ranges contain visible text that humans can inspect or edit.

Instead of acting as a general-purpose hex editor, SumTEditor presents configured byte
ranges as table columns. You can move through records, edit visible single-byte fields,
copy and paste whole records, mark records for deletion, and save all changes through a
temporary-file rewrite.

## Project Status

This project is in an early, usable prototype stage. The core editing model, config
parser, save path, undo/redo stack, and FTXUI-based terminal interface are implemented
and covered by lightweight tests. The UI is intentionally small and Vim-inspired, so
the first public version can stay understandable and easy to extend.

## Features

- Fixed-length binary record viewing and editing.
- Configurable table columns backed by byte offsets and lengths.
- Single-byte visible character editing for text-like fields.
- Vim-style terminal workflow with normal, edit, command, and visual-row modes.
- Whole-record copy and paste.
- Single-row and visual-range delete marking.
- Undo and redo for edits, inserts, and delete marks.
- Safe save path that writes a temporary file before replacing the original.
- C++17 core library with no third-party dependency.
- FTXUI terminal interface, pulled from a local archive, GitHub, or a system package.

## Non-Goals

SumTEditor is not a full hex editor, binary schema language, or record transformer.
The first version does not provide arbitrary byte editing, UTF-8-aware field editing,
CSV export, record reformatting, or soft-delete field rewriting.

## Build

Requirements:

- CMake 3.20 or newer
- A C++17 compiler
- macOS or Linux terminal environment
- FTXUI for the interactive TUI

Default build:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

By default, CMake uses `FTXUI-main.zip` from the repository root when the file is
present. If the archive is not present, it falls back to fetching FTXUI from GitHub.
You can point at another local archive with:

```sh
cmake -S . -B build -DSUMT_FTXUI_ARCHIVE=/path/to/FTXUI-main.zip
```

To use an installed FTXUI package:

```sh
cmake -S . -B build -DSUMT_USE_SYSTEM_FTXUI=ON
```

To build only the core library and tests:

```sh
cmake -S . -B build-core -DSUMT_BUILD_TUI=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Usage

```sh
sumteditor CONFIG [FILE]
```

- `CONFIG` is required.
- `FILE` is optional and overrides the `file` entry in the config.
- Relative `file` paths inside the config are resolved relative to the config file.

Example config:

```ini
file = data.bin
record_length = 128
field = id,0,8
field = name,8,24,20
field = status,32,1
```

Field syntax:

```ini
field = name,offset,length[,display_width]
```

Rules:

- `record_length` must be greater than zero.
- Every configured field must fit inside one record.
- Field names must be unique.
- Field lengths and offsets are byte-based.
- The target file size must be an exact multiple of `record_length`.
- Editable text must contain only visible single-byte characters, `0x20` through `0x7E`.
- Short field edits are padded with spaces.
- Overlong field edits are rejected.

## Key Bindings

| Key | Action |
| --- | --- |
| `j`, `k`, arrow keys | Move between records |
| `h`, `l` | Move between fields |
| Tab | Move to the next field, wrapping to the first field |
| `e`, Enter | Edit the current field |
| `y` | Copy the current record |
| `p` | Paste copied record after the current record |
| `P` | Paste copied record before the current record |
| `dd` | Mark the current record for deletion |
| `v` | Start visual row selection |
| `d` in visual mode | Mark selected rows for deletion |
| `u` | Undo |
| `Ctrl-r` | Redo |
| `:w` | Save |
| `:q` | Quit when there are no unsaved changes |
| `:wq` | Save and quit |
| `:q!` | Quit without saving |

## Editing Model

SumTEditor keeps the original file on disk and stores changes as in-memory operations:
field patches, inserted records, delete marks, clipboard content, and undo/redo history.
Original records are read by page instead of loading the entire file into memory.

Delete operations are logical until save. A record marked for deletion remains visible
in the table, but it is omitted when `:w` or `:wq` rewrites the file.

Saving writes all surviving records to a temporary file, applies field patches and
inserted records, then replaces the original file. After a successful save, the undo
history is cleared and the document becomes clean.

## Development

The codebase is split into a dependency-light core and a terminal UI:

- `include/sumt` and `src/config.cpp`, `src/document.cpp`: config parsing and editing model.
- `src/main.cpp`, `src/tui_app.cpp`: CLI entry point and FTXUI interface.
- `tests/test_core.cpp`: CTest-based core behavior coverage using plain `assert`.
- `examples/sample.conf`: minimal example configuration.

Run tests after changes:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

## Roadmap

- Better visual feedback for modified fields and inserted records.
- Search and jump commands for large files.
- Optional CSV/text export of configured fields.
- More TUI tests around key handling and command behavior.
- Packaging scripts for common platforms.

## Contributing

Issues and pull requests are welcome. Please keep the core library independent from
TUI-specific dependencies where possible, and add focused tests for changes to config
parsing, record editing, save behavior, or undo/redo.

## License

SumTEditor is released under the MIT License. See [LICENSE](LICENSE).

FTXUI is also licensed under the MIT License. If you redistribute FTXUI source or
archives with your own builds, keep its original copyright and license notice.
