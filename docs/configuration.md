# 配置与历史存储

`nya_core` 中的 `AppStorage` 统一管理持久化数据，不依赖 Widgets。正常启动时创建三个文件；自动测试、界面预览和 `--smoke-test` 使用临时目录，不修改用户设置。

| 文件 | 内容 | 位置接口 |
| --- | --- | --- |
| `editor.ini` | 主题、侧栏、字体及字号、动画、语言支持、自动换行、文件搜索模式、窗口位置及分栏尺寸 | `QStandardPaths::AppConfigLocation` |
| `system.ini` | 预留应用系统选项，目前只有格式版本 | `QStandardPaths::AppConfigLocation` |
| `history.json` | 最近文件与文件夹、打开时间、文件选择器上次目录 | `QStandardPaths::AppLocalDataLocation` |

系统配置指当前用户的应用系统选项，后续可扩展更新、平台集成等配置。文件和读写接口已就绪，目前未添加对应设置页面。

当前组织名与应用名均为 `NyaEdit`，常见默认位置如下；运行时始终通过 Qt 查询，遵循系统目录重定向及 Linux 的 XDG 环境变量。

| 平台 | 配置目录 | 历史数据目录 |
| --- | --- | --- |
| macOS | `~/Library/Preferences/NyaEdit/NyaEdit/` | `~/Library/Application Support/NyaEdit/NyaEdit/` |
| Windows | `%LOCALAPPDATA%/NyaEdit/NyaEdit/` | `%LOCALAPPDATA%/NyaEdit/NyaEdit/` |
| Linux | `~/.config/NyaEdit/NyaEdit/` | `~/.local/share/NyaEdit/NyaEdit/` |

macOS 路径已在当前构建环境实际查询验证。各平台路径规则参考 [Qt QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html)。可在应用中通过“设置 → 打开配置目录”定位实际配置文件。

## 编辑器与预留系统配置

`editor.ini` 首次创建的内容如下，窗口状态在正常关闭时保存。INI 文件使用 UTF-8，保留扩展键。

窗口状态包含 `window/geometry`、`window/splitter` 和 `window/sidebarWidth`；侧栏宽度单独保存，确保收起后退出也能在下次展开时恢复原尺寸。

```ini
[meta]
schemaVersion=1

[appearance]
dark=true
sidebar=true

[animation]
interface=true
cursor=true
selection=true
speed=1

[editor]
fontFamily=
fontSize=13
wrap=false

[search]
fuzzy=true

[language]
treeSitterEnabled=false
treeSitterExecutable=tree-sitter
treeSitterConfigPath=
```

`editor/fontFamily` 保存编辑区字体名称，初始为空时使用平台默认等宽字体（macOS 为 Menlo，Windows 为 Consolas，其他平台使用 Qt 系统等宽字体）。`editor/fontSize` 使用 pt，默认 13，支持 6–96。在“设置 → 字体与字号…”中确认后同时更新所有文档并持久化；取消只丢弃对话框中的选择。已有配置缺少字体名称时仍保留原字号。界面控件字体不受编辑器字体设置影响。

`search/fuzzy` 控制文件夹搜索：默认 `true`，字符按顺序非连续匹配；关闭后使用连续片段匹配。在搜索框右键菜单中切换后立即保存并重新搜索，清除历史不会重置此设置。

`language/treeSitterEnabled` 控制可选的 Tree-sitter 语法着色，默认关闭；`language/treeSitterExecutable` 为 CLI 名称或可执行文件完整路径，默认 `tree-sitter`，通过 PATH 查找，macOS 还会检查常见的 Homebrew / MacPorts 路径。`language/treeSitterConfigPath` 可指定 Tree-sitter 的 `config.json`，留空使用 CLI 默认配置；建议填写绝对路径。在“设置 → 语言支持…”中确认后应用到所有已打开文档，并供新文档和重启后使用；取消不会修改设置，清除历史不会重置语言支持。语言解析器及其高亮规则由用户现有的 Tree-sitter 配置提供，NyaEdit 不自动安装。此设置提供语法着色，不包含 LSP 补全、诊断或定义跳转。

`animation/interface` 控制侧栏收放、文件树节点展开 / 收起、搜索面板展开 / 收起和页面切换；`animation/cursor` 控制光标平滑移动与渐变闪烁；`animation/selection` 控制选择高亮渐变。三个开关默认均为 `true`，相互独立。`animation/speed` 为速度倍率，默认 `1`，支持 `0.25–3`，数值越大越快。在“设置 → 动画设置…”中确认后立即保存并应用到所有文档；取消不会修改设置，清除历史也不会重置动画。旧配置缺少这些键时自动补齐默认值。选区在动画关闭或编辑器失去焦点时仍使用高对比颜色。

`system.ini` 当前只包含 `[meta]` 下的 `schemaVersion=1`。核心读写接口：

```cpp
nya::AppStorage storage;
if (!storage.initialize()) {
    // 使用 storage.lastError() 报告错误；库本身不显示对话框。
}

const int size = storage.value("editor/fontSize", 13).toInt();
storage.setValue("editor/fontSize", 16);

// 预留系统配置命名空间；以下键仅为扩展示例，尚无对应业务逻辑。
const bool enabled = storage.systemValue("updates/enabled", false).toBool();
storage.setSystemValue("updates/enabled", true);
```

`setValue` / `setSystemValue` / `record` / `clearHistory` 返回成功与否。成功写入后分别发出 `configurationChanged`、`systemConfigurationChanged`、`historyChanged` 信号；存储错误通过 `errorOccurred` 和 `lastError()` 提供。UI 负责决定如何展示错误。配置通过 QSettings 原子同步，历史通过 QSaveFile 提交。

`StoragePaths::standard()` 返回运行平台路径；`StoragePaths::underDirectory(root)` 返回 `root/config` 与 `root/data`，可注入 `AppStorage` 或 `MainWindow`，便于独立测试和嵌入其他前端。

## 历史记录

```json
{
  "schemaVersion": 1,
  "lastDirectory": "/absolute/workspace",
  "entries": [
    {
      "path": "/absolute/workspace/notes.txt",
      "kind": "file",
      "lastOpened": "2026-10-29T08:00:00.000Z"
    },
    {
      "path": "/absolute/workspace",
      "kind": "folder",
      "lastOpened": "2026-10-29T07:00:00.000Z"
    }
  ]
}
```

历史按最近打开顺序保存，最多 30 条。记录路径规范化并去重，文件与文件夹保留明确类型，时间采用 UTC。单独打开的文件与打开的文件夹均会保存，文件保存到新路径时也会记录；普通保存不会重新登记已有路径。移动或删除的历史项目仍展示，重新打开时提示不可用。

已打开文件夹内的文件（包含子目录）不单独新增或提升文件历史，重新打开已在标签页中的文件、保存为该文件夹内的新路径也遵循此规则；文件夹本身仍记录，文件夹外的文件正常记录。异步加载以发起打开时的文件夹为准。历史中曾经单独打开的文件继续保留；仅存在于历史、当前未打开的文件夹不影响文件记录。

“设置 → 清除历史记录”直接清空历史及上次目录，同步刷新欢迎页和最近菜单，保留编辑器与系统配置。正在加载的文件不会在清空操作完成后把旧记录重新写回；之后主动打开项目会产生新记录。清空不会关闭当前文档或文件夹。

首次采用新文件结构时，会从旧版默认 QSettings 导入已有偏好和最近路径；后续以新文件为准。清空时同时删除旧版的历史路径键，防止再次导入。历史 JSON 损坏或版本不支持时，应用保留原文件并提示错误；显式清空后恢复记录。写入失败不会把内存历史误清空。

历史只存路径与打开时间，不存正文；当前不提供未保存文档恢复。
