# 项目任务

打开项目文件夹后，在根目录创建 `.ne/task.json`，即可通过顶部“运行”菜单执行项目命令。“运行 → 配置任务…”会打开此文件；不存在时创建可运行的示例，不覆盖已有配置。修改后先保存，再运行任务。

配置采用 [VS Code Tasks](https://code.visualstudio.com/docs/debugtest/tasks) 中常见的 `label`、`type`、`command`、`args`、`options`、`group` 和 `dependsOn` 字段。本版实现命令执行功能，配置文件为 **`.ne/task.json`**，统一按顺序执行依赖。使用标准 JSON，不支持注释或尾逗号；不支持的字段会明确报错，不会静默忽略。

## 示例

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "构建",
      "type": "shell",
      "command": "cmake",
      "args": ["--build", "build", "--parallel"],
      "options": {
        "cwd": "${workspaceFolder}",
        "env": { "MY_BUILD_MODE": "debug" }
      },
      "group": { "kind": "build", "isDefault": true }
    },
    {
      "label": "测试",
      "type": "shell",
      "command": "ctest --test-dir build --output-on-failure",
      "dependsOn": "构建",
      "dependsOrder": "sequence",
      "group": "test"
    }
  ]
}
```

通过“运行 → 项目任务”直接选择，或使用“运行任务…”选择器。“运行构建任务”执行默认构建任务；未指定默认项时，使用第一个 `build` 任务。选择“测试”时先构建，构建成功后再测试。NyaEdit 自身的 [任务配置](../.ne/task.json) 提供配置构建、构建、测试和启动程序四个任务，使用 Ninja 与独立的 `build/native-release` 目录，避免复用其他平台的 CMake 缓存。

“运行 → 在终端中打开”打开系统终端并进入当前项目目录；没有打开项目时，使用当前已保存文件所在目录，再没有则使用用户主目录。任务运行期间也可使用。macOS 打开 Terminal，Windows 打开命令提示符，Linux 查找已安装的 GNOME Terminal、Konsole、Xfce Terminal 等终端。此终端可用于交互操作，编辑器内任务继续在底部面板显示输出。

| 操作 | Windows / Linux | macOS |
| --- | --- | --- |
| 运行任务… | Ctrl+Shift+R | ⌘⇧R |
| 运行构建任务 | Ctrl+Shift+B | ⌘⇧B |
| 重新运行上次任务 | Ctrl+Alt+R | ⌘⌥R |
| 停止任务 | Ctrl+Shift+F5 | ⌘⇧F5 |

## 字段

顶层必须有 `version: "2.0.0"` 和 `tasks` 数组。每个任务是一个对象：

| 字段 | 类型 | 作用 |
| --- | --- | --- |
| `label` | 字符串，必填 | 当前项目内唯一的任务名称，也是依赖引用的名称 |
| `detail` | 字符串 | 菜单提示中的说明 |
| `type` | `shell` / `process` | 默认 `shell`；`process` 直接启动可执行文件 |
| `command` | 字符串 | shell 命令或可执行文件；只有依赖的组合任务可省略 |
| `args` | 字符串数组 | 每项为一个参数；shell 任务会自动对各参数加引号 |
| `options.cwd` | 字符串 | 工作目录，默认 `${workspaceFolder}`；相对路径以项目根目录为基准 |
| `options.env` | 对象 | 覆盖任务基础环境变量；字符串设值，`null` 删除 |
| `group` | `build` / `test` 或对象 | 对象写法 `{ "kind": "build", "isDefault": true }`；只能有一个默认构建任务 |
| `dependsOn` | 字符串或字符串数组 | 在自身命令前执行的任务，按声明顺序执行 |
| `dependsOrder` | `sequence` | 可省略，默认顺序执行；当前不支持 `parallel` |
| `windows` / `osx` / `linux` | 对象 | 当前平台的 `type`、`command`、`args`、`options` 覆盖 |

平台覆盖中的 `options` 按字段合并，`env` 按变量名合并；`args` 替换原数组。

### 命令

`shell` 任务可写管道、重定向、`&&`，也可在 JSON 字符串中用 `\n` 写多行脚本。例如：

```json
{ "label": "检查并构建", "command": "echo Checking project... && cmake --build build --parallel" }
```

macOS / Linux 使用 `/bin/sh -c`；Windows 使用 PowerShell。`command` 按对应 shell 的语法编写，单独的 `args` 会作为独立参数保留空格等字符。shell 自身的变量可以写 `$HOME` 等语法，`${...}` 形式保留给 NyaEdit 配置变量。需要自定义 shell 时，可用 `process` 启动该 shell，并用 `args` 传入脚本。

`process` 任务将 `command` 作为可执行文件名或路径，参数放在 `args` 中，不解释管道、通配符和重定向。含目录分隔符的相对程序路径以工作目录为基准；普通程序名从任务环境的 `PATH` 中查找。

macOS / Linux 在每次运行前异步启动用户登录交互 shell（`$SHELL -ilc`），读取启动文件导出的环境，再统一解析变量和依赖。这使 Finder 等桌面入口启动的应用也能使用终端配置的 Homebrew、Node、Python 等工具路径；shell 和 process 任务都使用此环境。读取过程最多等待 8 秒，失败或超时后提示并回退到应用环境，期间可以停止任务。Windows 继承应用环境。`options.env` 最后应用，可覆盖或删除上述环境变量。

只导入已导出的环境变量，shell 别名和函数不属于可执行文件。如果工具未安装或仍未加入 `PATH`，可在 `command` 中填写工具绝对路径，或设置 `options.env.PATH`，例如 `"/path/to/tools:${env:PATH}"`（Windows 的目录分隔符使用 `;`）。

```json
{
  "label": "运行当前 Python 文件",
  "type": "process",
  "command": "python3",
  "args": ["${file}"],
  "options": { "cwd": "${fileDirname}" },
  "windows": { "command": "python" }
}
```

### 变量

变量可用于 `command`、`args`、`options.cwd` 和 `options.env` 的字符串值：

| 变量 | 内容 |
| --- | --- |
| `${workspaceFolder}` | 项目文件夹绝对路径 |
| `${workspaceFolderBasename}` | 项目文件夹名称 |
| `${file}` | 当前文档绝对路径 |
| `${fileDirname}` | 当前文档所在目录 |
| `${fileBasename}` | 当前文档文件名，含扩展名 |
| `${fileBasenameNoExtension}` | 去掉最后一个扩展名的文件名 |
| `${relativeFile}` | 当前文档相对于项目根目录的路径 |
| `${pathSeparator}` | 系统目录分隔符 |
| `${env:NAME}` | 任务基础环境中的变量 `NAME`（macOS / Linux 优先使用终端导出的环境） |

文档变量要求已保存的当前文档；欢迎页或未保存的新文档无法提供路径。未知变量或缺失的环境变量会直接报错，依赖命令也不会先行执行。变量在终端环境读取后统一取值；项目目录、当前文件和配置使用点击运行时的快照。`options.env` 中新增的值不参与 `${env:NAME}` 解析，后者读取任务基础环境。

shell 命令中的配置变量会根据引号上下文转义。路径也可直接作为 `args` 项，保留空格、引号、`$` 等字符。输出面板的命令标题保留配置原文，避免自动显示环境变量展开值；命令本身的标准输出正常展示。

### 依赖与组合任务

```json
{ "label": "完整检查", "dependsOn": ["构建", "测试"], "dependsOrder": "sequence" }
```

同时设置 `dependsOn` 和 `command` 时，在依赖成功后执行自己的命令。相同依赖在一次运行中只执行一次，循环和不存在的依赖会报错。所有步骤的配置和变量先统一解析；工作目录在对应步骤启动时检查，因此可由前序任务创建。

任何步骤启动失败、崩溃或以非零退出码结束时，停止后续步骤。每个窗口同时只运行一条任务链；服务类命令保持前台运行，用“停止任务”结束。

## 输出与生命周期

命令异步执行，编辑器可以继续使用。底部“任务输出”显示步骤、工作目录、标准输出、标准错误和退出码，按 UTF-8 增量解码，支持分段到达的中文和 emoji。ANSI 颜色及控制序列转换为纯文本，最多保留最近 10000 行和约一百万字符。每次开始新任务会清空旧输出，也可手动清空或复制。

隐藏输出面板后任务仍继续运行，通过“运行 → 任务输出”重新打开。“停止任务”结束当前步骤及其子进程、取消余下步骤；窗口退出时也会清理任务。任务保持启动时的项目、当前文件和配置快照，切换项目不会迁移运行中的命令。上次任务仅可在原项目内重跑，重跑会重新读取配置。

本版运行非交互命令，标准输入关闭；尚未提供 PTY、交互式 shell、密码输入、终端光标控制、并行依赖、自动运行、调试器或 `problemMatcher`。命令只在手动点击运行时启动，打开项目或配置文件不会自动执行。任务使用磁盘上的配置和项目文件，运行前请保存修改。
