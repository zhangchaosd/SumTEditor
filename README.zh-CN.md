# SumTEditor

[![CI](https://github.com/zhangchaosd/SumTEditor/actions/workflows/ci.yml/badge.svg)](https://github.com/zhangchaosd/SumTEditor/actions/workflows/ci.yml)

[English](README.md)

SumTEditor 是一个使用 C++17 编写的终端二进制文件编辑器，面向“定长记录”文件。
它适合处理每条记录长度相同、并且记录中的某些字节区间是可见文本的二进制文件。

它不是通用十六进制编辑器，而是根据配置把指定字节区间展示成表格列。你可以浏览记录、
编辑可见单字节字段、复制粘贴整条记录、标记记录删除，并通过临时文件重写的方式保存修改。

## 项目状态

项目目前处于早期可用原型阶段。核心编辑模型、配置解析、保存路径、撤销/重做栈，以及基于
FTXUI 的终端界面都已经实现，并有轻量测试覆盖。界面刻意保持小而清晰，采用类似 Vim 的操作
方式，方便后续继续扩展。

## 功能特性

- 查看和编辑由定长记录组成的二进制文件。
- 根据字节偏移和长度配置表格展示字段。
- 支持文本型字段的可见单字节字符编辑。
- Vim 风格终端操作，包含 normal、edit、command、visual-row 模式。
- 支持整条记录复制和粘贴。
- 支持单行删除标记和可视范围删除标记。
- 支持编辑、插入、删除标记的撤销和重做。
- 保存时先写临时文件，再替换原文件。
- C++17 核心库不依赖第三方库。
- 终端界面使用 FTXUI，可从本地压缩包、GitHub 或系统包获取。

## 非目标

SumTEditor 首版不是完整的十六进制编辑器、二进制 schema 语言或记录转换工具。
当前版本不提供任意字节编辑、UTF-8 感知字段编辑、CSV 导出、记录重排格式化或软删除字段改写。

## 构建

要求：

- CMake 3.20 或更新版本
- 支持 C++17 的编译器
- macOS 或 Linux 终端环境
- FTXUI，用于交互式 TUI

默认构建：

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

默认情况下，如果仓库根目录存在 `FTXUI-main.zip`，CMake 会优先使用这个本地压缩包。
如果压缩包不存在，则回退到从 GitHub 获取 FTXUI。也可以手动指定本地压缩包路径：

```sh
cmake -S . -B build -DSUMT_FTXUI_ARCHIVE=/path/to/FTXUI-main.zip
```

如果你已经安装了 FTXUI 系统包：

```sh
cmake -S . -B build -DSUMT_USE_SYSTEM_FTXUI=ON
```

只构建核心库和测试：

```sh
cmake -S . -B build-core -DSUMT_BUILD_TUI=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## 使用

```sh
sumteditor CONFIG [FILE]
```

- `CONFIG` 必填。
- `FILE` 可选，用来覆盖配置文件里的 `file` 项。
- 配置里的相对 `file` 路径会按配置文件所在目录解析。

配置示例：

```ini
file = data.bin
record_length = 128
field = id,0,8
field = name,8,24,20
field = status,32,1
```

字段语法：

```ini
field = name,offset,length[,display_width]
```

规则：

- `record_length` 必须大于 0。
- 每个字段必须完整落在一条记录内部。
- 字段名不能重复。
- 字段长度和偏移都按字节计算。
- 目标文件大小必须是 `record_length` 的整数倍。
- 可编辑文本只能包含可见单字节字符，即 `0x20` 到 `0x7E`。
- 字段编辑内容较短时会用空格补齐。
- 字段编辑内容过长时会拒绝提交。

## 快捷键

| 按键 | 操作 |
| --- | --- |
| `j`、`k`、方向键 | 在记录之间移动 |
| `h`、`l` | 在字段之间移动 |
| Tab | 移动到下一个字段，到最右侧后回到第一个字段 |
| `e`、Enter | 编辑当前字段 |
| `y` | 复制当前记录 |
| `p` | 在当前记录之后粘贴复制的记录 |
| `P` | 在当前记录之前粘贴复制的记录 |
| `dd` | 将当前记录标记为删除 |
| `v` | 开始可视行选择 |
| visual 模式下 `d` | 将选中记录标记为删除 |
| `u` | 撤销 |
| `Ctrl-r` | 重做 |
| `:w` | 保存 |
| `:q` | 无未保存修改时退出 |
| `:wq` | 保存并退出 |
| `:q!` | 不保存并退出 |

## 编辑模型

SumTEditor 会保留原文件在磁盘上的内容，并把修改作为内存中的操作保存：
字段补丁、插入记录、删除标记、剪贴板内容以及撤销/重做历史。原始记录按页读取，不会把整个
文件一次性加载进内存。

删除操作在保存前只是逻辑标记。被标记删除的记录仍然会显示在表格中，但执行 `:w` 或 `:wq`
保存时会从输出文件中省略。

保存时，程序会把所有保留的记录写入临时文件，应用字段补丁和插入记录，然后替换原文件。
保存成功后，撤销历史会被清空，文档状态会变为未修改。

## 开发

代码库拆分为低依赖核心库和终端界面：

- `include/sumt` 与 `src/config.cpp`、`src/document.cpp`：配置解析和编辑模型。
- `src/main.cpp`、`src/tui_app.cpp`：命令行入口和 FTXUI 界面。
- `tests/test_core.cpp`：基于 CTest 和普通 `assert` 的核心行为测试。
- `examples/sample.conf`：最小配置示例。

修改后运行测试：

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

## GitHub Actions

仓库包含 CI 和 Release 两个 workflow：

- CI 会在推送到 `main` 和创建 pull request 时构建并测试项目。
- Release 会在推送 `v0.1.0` 这类版本 tag 时构建 Linux 和 macOS 压缩包。

学习说明见 [docs/github-actions.md](docs/github-actions.md)。

## 路线图

- 为已修改字段和插入记录提供更清晰的视觉反馈。
- 为大文件增加搜索和跳转命令。
- 可选导出配置字段到 CSV 或文本。
- 为按键处理和命令行为增加更多 TUI 测试。
- 为常见平台增加打包脚本。

## 贡献

欢迎提交 issue 和 pull request。请尽量保持核心库不依赖 TUI 专用依赖，并为配置解析、记录编辑、
保存行为或撤销/重做相关变更增加聚焦测试。

## 许可证

SumTEditor 使用 MIT License 发布。详见 [LICENSE](LICENSE)。

FTXUI 同样使用 MIT License。如果你在自己的构建或发行包里重新分发 FTXUI 源码或压缩包，
请保留它原始的版权和许可证声明。
