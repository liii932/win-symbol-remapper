# RestoreChineseSymbol

Windows 10/11 符号键重映射工具，C++14 / Win32。通过 `WH_KEYBOARD_LL` 拦截目标按键，以 `SendInput(KEYEVENTF_UNICODE)` 注入字符，无需修改输入法配置。内置方案输出 ASCII 符号，本地方案可输出中文等 BMP 字符。

## 映射

内置默认方案按美式键位匹配，共 19 个映射；中英文模式均生效。未列出的按键及含 Ctrl、Alt、Win 的组合键透传。

| 键位 | 无 Shift | Shift |
| --- | --- | --- |
| 1 | 透传 | `!` |
| 4 | 透传 | `$` |
| 6 | 透传 | `^` |
| 9 / 0 | 透传 | `(` / `)` |
| - | 透传 | `_` |
| , | `,` | `<` |
| . | `.` | `>` |
| [ / ] | `[` / `]` | 透传 |
| 反斜杠 | `\` | `\|` |
| ; | `;` | `:` |
| 单引号 | `'` | `"` |
| / | 透传 | `?` |

每次按下或长按重复输出一个字符；不自动配对括号或引号。内置规则位于 [`src/mapping_table.h`](src/mapping_table.h)，按键状态机位于 [`src/remapper.h`](src/remapper.h)。

映射表包含无 Shift、按住 Shift 两个分支，各有 256 个虚拟键码槽位；值为零表示透传。启动时生成全部只读映射表，运行期间只切换内存中的表。按键处理直接查表，不读配置、不分配内存。切换时已按住的键沿用原表直到松开，Shift 状态仍实时生效；新按下的键使用新表。已透传的按键不会在长按期间被接管。

## 本地方案

配置文件为 **可执行文件同目录的 `schemes.ini`**，与启动时的工作目录无关。构建时若该文件不存在，会复制 [`examples/schemes.ini`](examples/schemes.ini)，提供“代码符号”和“中文标点”两个方案；已有配置不会被覆盖。启动方案默认仍为 `builtin`（内置默认），且始终可以切回它。

程序启动时一次性加载全部有效方案；修改文件后需退出并重新启动。运行期间不监控、重读或写回配置，也不保存上次选中的方案。

```ini
[app]
version=1
startup=builtin
next_scheme_hotkey=Ctrl+Alt+F10

[scheme.coding]
name=代码符号
base=builtin
Shift+LeftBracket=U+007B
Shift+RightBracket=U+007D
Quote=pass

[scheme.chinese]
name=中文标点
base=empty
Comma=U+FF0C
Period=U+3002
Shift+Slash=U+FF1F
```

- 编码为 UTF-8，可带 BOM；支持 LF、CRLF。空行及以 `;`、`#` 开头的整行注释会被忽略，不支持行尾注释。字段和键名区分大小写。
- 非空配置必须包含 `[app]` 的 `version=1`。`startup` 默认为 `builtin`，切换键默认为 `Ctrl+Alt+F10`。
- 方案 ID 为 1–48 个 ASCII 字母、数字、下划线或连字符，`builtin` 为保留值。`name` 默认为 ID，最长 128 个 UTF-8 字节，不含控制字符。
- `base` 只允许 `builtin` 或 `empty`，省略时使用 `builtin`。所有覆盖在启动时展开，与 `base` 字段位置无关。省略的键继承基础表；`pass` 显式透传。
- 输出格式为 `U+` 加四位十六进制数字，例如 `U+0022` 为双引号、`U+005C` 为反斜杠。支持一个 BMP 字符；不支持控制字符、代理项、Unicode 非字符、行/段分隔符、字符串及 emoji。
- 支持 `Key0`–`Key9`、`Space`、`Minus`、`Equal`、`Comma`、`Period`、`LeftBracket`、`RightBracket`、`Backslash`、`Semicolon`、`Quote`、`Slash`、`Backtick`；加 `Shift+` 前缀表示按住 Shift。按当前美式虚拟键位解释。
- 切换键格式为修饰键加一个主键，例如 `Ctrl+Alt+F10`、`Ctrl+Shift+F9`。修饰键支持 `Ctrl`、`Alt`、`Shift`、`Win`，至少包含 Ctrl/Alt/Win 之一；主键支持 A–Z、0–9、F1–F24（不含系统保留的 F12）。不能与暂停键 `Ctrl+Alt+F9` 相同，不能重复修饰键。系统或其他程序占用时可用托盘切换。
- 文件最大 64 KiB，最多 32 个自定义方案。循环顺序为内置默认，然后按文件中的方案顺序。

文件不存在、为空或仅有注释时直接使用内置方案。单个方案的键名、字符、继承、重复字段等错误会使整个方案被跳过；重复 ID 的所有定义均跳过。结构错误、版本错误、未知/重复全局字段、非法编码或超出文件/方案数量限制时整份配置回退内置默认。无效启动方案回退 `builtin`；无效切换键回退 `Ctrl+Alt+F10`，有效方案仍保留。

启动时会集中提示加载问题；右键托盘的“配置加载详情”显示配置路径、快捷键状态和带行号的问题（最多显示前 20 项）。修正后重启检查。

## 构建与测试

要求 CMake ≥ 3.20、C++14 编译器；Windows 应用使用 MSVC + Windows SDK 或 MinGW-w64，无第三方库依赖。

```sh
cmake -S . -B out -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build out --config Release
ctest --test-dir out -C Release --output-on-failure
```

Windows 生成 `RestoreChineseSymbol.exe` 和初始 `schemes.ini`，位于 `out/` 或多配置生成器的 `out/Release/`。Linux 构建、运行按键状态机及配置/方案管理测试。CI 配置覆盖 Windows/MSVC 与 Linux/GCC。

也可直接使用 MinGW-w64 编译；脚本输出至 `build/`，静态链接 GCC 运行库并执行测试：

```powershell
.\build.ps1 -Compiler 'C:\mingw64\bin\g++.exe'
```

更新可执行文件前需退出正在运行的实例。

## 使用与边界

- 启动后默认启用。`Ctrl+Alt+F9` 或单击托盘图标切换启用/暂停状态。
- `Ctrl+Alt+F10`（可配置）切换到下一方案，按住不会连续切换；只有一个方案时不改变状态。暂停期间可切换方案，恢复后使用所选方案。
- 右键菜单可直接选择方案、查看当前实际映射、查看配置加载详情或退出。两个快捷键独立注册；某个快捷键不可用不影响托盘操作。
- 作用于当前桌面，无应用白名单。非美式布局需调整键位。
- 只处理按键，不改写粘贴或已提交文本；不主动提交候选词。命中按键直接插入符号，单引号不再交给输入法作拼音分隔符。组合输入行为依赖输入法及应用。
- 受 UIPI 限制，不能向更高完整性级别的进程注入输入。注入返回零时放行原按键。自定义输入控件、游戏及远程桌面需单独验证。

`--smoke-test` 同样加载程序目录的配置，在暂停状态下安装钩子，运行消息循环约 250 ms 后退出；不显示托盘或注册快捷键。退出码 0 表示成功，1 表示初始化失败，2 表示已有实例，3 表示配置存在加载问题。此检查需交互式 Windows 桌面，不替代实际输入法及快捷键冲突测试。

Windows 启动回归可运行 `./tests/windows_smoke.ps1 -Executable ./build/RestoreChineseSymbol.exe`。脚本在独立的临时目录验证缺失配置、不同工作目录、中文路径、错误版本、无效方案、超大文件、非法 UTF-8 和快捷键冲突，结束后清理临时文件，不修改实际配置。运行前应退出已有实例。

修改规则或状态机时，同步更新 [`tests/remapper_test.cpp`](tests/remapper_test.cpp)。回归重点为全部默认键位、按下/松开配对、修饰键切换、长按切换方案、暂停、注入失败和注入事件防循环。[`tests/scheme_config_test.cpp`](tests/scheme_config_test.cpp) 验证配置边界、错误隔离、完整表生成、方案循环及加载后实际按键输出。问题报告需包含 Windows、输入法、应用版本、配置及最小按键序列。

## 源码管理

源码仓库包含 `src/`、`tests/`、`cmake/`、构建脚本及 `examples/schemes.ini`。`.gitignore` 排除构建目录、可执行文件、调试文件、IDE 本地状态和个人 `schemes.ini`；示例配置仍纳入版本管理。修改个人方案时编辑程序同目录的配置，共享示例的修改放在 `examples/schemes.ini`。

文本文件使用 UTF-8、LF 换行，由 `.editorconfig` 和 `.gitattributes` 统一。GitHub Actions 已配置 Windows/MSVC 与 Linux/GCC 构建和测试。编译后的 Windows 程序可作为 GitHub Release 附件发布。

## 许可证

[MIT](LICENSE) · Copyright (c) 2026 liii932
