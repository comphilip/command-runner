# Command Runner Win32 迁移方案

## 1. 背景与目标

当前项目使用 Python、Tkinter、Pillow、pystray 和 PyInstaller。业务代码本身体积很小，但 PyInstaller 的独立发行包必须包含 Python 解释器、标准库、Tcl/Tk 和扩展模块，因此单文件 EXE 约为 20 MB。

迁移目标：

- 只支持 Windows 11 x64。
- 保留当前命令管理、实时日志、系统托盘和进程树管理功能。
- 保留当前 GUI 的对齐方式、可调整大小布局和键盘可访问性。
- 降低发行体积，优先控制在 1–2 MB，至少低于 5 MB。
- 避免长期直接维护大量裸 Win32 消息循环和资源释放代码。
- 最终交付单个原生 EXE，不依赖 Python、.NET 或第三方 GUI DLL。

迁移的行为基线是 Python 版 v0.1.5。重写过程中不能只迁移最初的功能列表，还必须保留后续版本补充的命令行解析、无控制台启动、单工作线程异步 I/O、对话框可用性、键盘操作和托盘资源生命周期等修复。第 14 节给出逐项回归清单。

PyInstaller 会将 Python 解释器及应用依赖包含在发行包中，因此在保留 Python 和 Tkinter 的前提下，很难达到 5 MB，更无法达到 1 MB。[PyInstaller 打包原理](https://pyinstaller.org/en/stable/operating-mode.html)

## 2. 最终选型

采用以下技术栈：

- C++20
- MSVC 2022
- CMake
- Win32++：窗口、对话框、原生控件、Splitter 和消息分发
- Microsoft WIL：HANDLE、COM 和 Win32 错误的 RAII 管理
- Win32 API：CreateProcessW、异步管道、Job Object 和 Shell_NotifyIconW
- 轻量 JSON 库：保持现有配置文件兼容

Win32++ 是 header-only 的 Win32 C++ 封装，支持原生窗口、对话框、SDI/MDI、Splitter、Per-Monitor DPI V2 和高分辨率缩放，适合构建不携带额外 GUI DLL 的小型原生程序。[Win32++ 项目页](https://sourceforge.net/projects/win32-framework/)

WIL 不是 GUI 框架，主要用于自动释放 Windows 资源和统一错误处理，例如用 `wil::unique_handle` 管理进程、线程、管道和 Job Object 句柄。[Microsoft WIL](https://github.com/microsoft/wil)

## 3. 备选方案结论

| 方案 | 预计发布体积 | 维护难度 | 结论 |
|---|---:|---|---|
| 优化现有 PyInstaller | 约 12–18 MB | 低 | 无法满足目标 |
| Nuitka/cx_Freeze | 约 10–25 MB | 低到中 | 仍需携带 Python/Tk |
| C# WinForms + .NET Framework 4.8 | 约 0.1–1 MB | 低到中 | EXE 很小，但依赖系统共享运行时 |
| Win32++ + WIL | 约 0.5–2 MB | 中 | 最终采用 |
| WTL + WIL | 约 0.3–1.5 MB | 中 | 更偏 ATL/宏风格，学习和维护成本较高 |
| 静态 MFC | 约 2–6 MB | 低 | GUI 开发方便，但难以稳定低于 1 MB |
| FLTK | 约 1–4 MB | 低 | 部分控件自绘，原生外观和辅助功能需额外验证 |
| wxWidgets | 约 4–10 MB | 低 | 跨平台优势在本项目中没有价值 |
| C++ 裸 Win32 | 约 0.3–1.5 MB | 高 | 体积小，但后期维护成本过高 |

体积是基于同类 Release 构建的估计值，最终结果取决于 CRT 链接方式、JSON 库、资源文件和编译优化，应以 Windows 11 x64 实际构建为准。

## 4. 代码架构

保留当前项目中 UI、状态管理和进程服务分离的设计，建议目录如下：

```text
src/
  main.cpp
  core/
    models.h
    config_store.h
    config_store.cpp
    process_manager.h
    process_manager.cpp
  platform/
    child_process.h
    child_process.cpp
    windows_job.h
    windows_job.cpp
    tray_icon.h
    tray_icon.cpp
  ui/
    main_window.h
    main_window.cpp
    command_dialog.h
    command_dialog.cpp
    close_dialog.h
    close_dialog.cpp
resources/
  CommandRunner.rc
  resource.h
  CommandRunner.ico
tests/
CMakeLists.txt
```

职责边界：

- `core` 不直接依赖 Win32++ 控件，便于单元测试。
- `platform` 封装 Windows 进程、Job Object、管道和托盘 API。
- `ui` 只负责展示、输入验证和将用户操作转发给核心服务。
- 进程启动、停止、等待和 stdout/stderr 读取集中在一个共享 I/O 工作线程中，不能为每个命令或每根管道长期创建一个线程。
- 工作线程不得直接更新窗口，通过 `PostMessage` 将状态和日志事件投递给 UI 线程。
- 应用状态独立于可销毁、可重建的主窗口和托盘对象；最小化到托盘不能丢失命令状态或日志。

建议定义应用私有消息：

```cpp
constexpr UINT WM_APP_PROCESS_EVENT = WM_APP + 1;
constexpr UINT WM_APP_LOG_EVENT = WM_APP + 2;
```

## 5. GUI 布局方案

现有 GUI 布局可以完整保留，但 Win32++ 不提供类似 Tkinter `pack/grid` 或 CSS 的通用自动布局。主窗口使用 `CSplitter` 和集中式 `OnSize` 布局；对话框使用资源编辑器和 `CResizer`。

### 5.1 主窗口结构

```text
MainFrame
├── Toolbar/ActionBar，固定高度
└── Vertical Splitter，填满剩余区域
    ├── CommandListPane
    │   └── CListView，填满上窗格
    └── LogPane
        ├── OptionsBar，固定高度
        └── CRichEdit，填满剩余区域
```

布局对应关系：

| 当前 Tkinter 控件 | Win32++/Win32 控件 |
|---|---|
| 顶部按钮栏 | Button 容器或 Toolbar |
| `ttk.PanedWindow` | `CSplitter` |
| `ttk.Treeview` | `CListView`，Report 模式 |
| `tk.Text` | `CRichEdit` |
| 单选按钮 | 原生 Radio Button |
| 复选框 | 原生 Check Box |
| 信息和确认框 | MessageBox 或 Task Dialog |
| 文件夹选择 | `CFolderDialogEx` |

主窗口布局规则：

- ActionBar 固定高度并横向填满，按钮左对齐、紧凑相邻，不插入无用途的分隔条。
- Splitter 填满 ActionBar 以下区域。
- 上下窗格初始比例为 2:3，即分隔位置约为可用高度的 40%。
- 用户拖动分隔条后保留用户选择的位置或比例。
- ListView 和 RichEdit 始终填满各自窗格。
- 设置合理的最小窗口尺寸，防止控件重叠。

ListView 保留原版的鼠标和键盘行为：

- 单击选择；Ctrl+单击切换单项；Shift+单击扩展连续选择。
- Ctrl+A 全选；Shift+Up/Down 扩展选择。
- Ctrl+Up/Down 只移动焦点；Home/End 选择首项或末项；Ctrl+Home/End 只移动焦点到边界。
- Space 切换焦点项的选择状态；Delete 删除所选项。
- Enter 在仅有一个可编辑项时打开编辑对话框，否则启动所选命令。
- 双击一行时先选中该行，并仅在命令处于可编辑状态时打开编辑对话框。

### 5.2 表格列宽

Name、Status、PID、Exit Code 和 Auto Start 使用固定的 DPI 逻辑宽度，Working Directory 使用剩余宽度，并设置最小值。窗口变宽时主要扩展 Working Directory 列。

### 5.3 日志选项栏

左侧从左向右排列：

```text
Combined | stdout | Standard error
```

右侧从右向左排列：

```text
Auto-scroll | Word wrap | Jump to Latest | Clear
```

可以实现一个只负责水平排列、间距和 DPI 缩放的轻量布局辅助类，避免坐标计算散落在各个消息处理器中。

### 5.4 添加和编辑对话框

使用模态对话框和 Visual Studio 资源编辑器定义基础布局，再通过 `CResizer` 设置锚点。对话框相对主窗口居中，界面文本保持英文，并为所有标签、输入框、复选框和按钮设置不冲突的 Mnemonic/Access key。

对话框包含 Name、Working Directory、Browse、Command Line、Output Encoding、Shell、Auto Start、Save 和 Cancel。Shell 默认关闭，表示直接执行；Auto Start 默认关闭。

Command Line 使用支持多行、自动换行和垂直滚动条的原生 Edit/RichEdit 控件，初始高度约四行。Enter 插入换行，Ctrl+Enter 保存，Escape 取消。配置中保留用户输入的多行文本，但真正启动命令时必须将 `CRLF`、单独的 `CR` 和单独的 `LF` 分别替换为一个空格，不能把换行直接交给 shell 或进程解析器。

对话框允许横向调整大小并保持高度固定，内容区宽度必须始终与客户区同步；窗口变宽时 Name、Working Directory 和 Command Line 一起变宽，不能只放大外框而留下固定宽度的内容。基础锚点如下：

- Name 输入框：左、上、右。
- Working Directory 输入框：左、上、右。
- Browse 按钮：上、右。
- Command Line 输入框及其滚动条：左、上、右。
- Save 和 Cancel：上、右。

Win32++ 发行包包含 `DialogResizing` 示例，可作为实现参考。

## 6. DPI、窗口缩放和辅助功能

应用 Manifest 声明 Per-Monitor DPI Awareness V2。所有尺寸以 96 DPI 下的逻辑值定义，并集中转换：

```cpp
int ScaleForWindow(HWND window, int value)
{
    return MulDiv(value, GetDpiForWindow(window), 96);
}
```

必须处理以下情况：

- 响应 `WM_DPICHANGED`，采用 Windows 建议的新窗口矩形。
- 在 DPI 改变后重新计算控件位置、字体和表格列宽。
- 不长期缓存启动时计算出的像素尺寸。
- 图标资源包含 16、20、24、32、48、64 和 256 像素版本。
- 在 100%、125%、150%、175% 和 200% 缩放下测试。
- 测试窗口在不同 DPI 的多个显示器之间移动。

Win32++ 的 `CResizer` 历史上在跨不同 DPI 显示器移动对话框时需要额外处理。主窗口应采用 Frame 加程序化布局；对话框在 `WM_DPICHANGED` 后重新初始化 Resizer，必要时重建子对话框。[相关讨论](https://sourceforge.net/p/win32-framework/support-requests/9/)

继续使用 Windows 原生 Button、ListView、RichEdit、Radio Button 和 Check Box，以保留键盘导航、焦点、系统主题和 UI Automation/屏幕阅读器兼容性。不要使用 Dear ImGui 或自绘控件替代主要交互控件。

所有可操作控件都必须有键盘访问路径和可见焦点。界面及错误消息使用英文；新增字符串时同时检查 Access key 冲突。

## 7. 进程管理迁移

现有 Python 实现中的进程行为应保持不变：

- 支持直接执行和 Shell 执行；直接执行是默认模式。
- 指定工作目录。
- stdout 和 stderr 分别使用独立管道捕获。
- 不提供交互式 stdin，子进程的标准输入连接到空设备。
- 一个共享 I/O 工作线程异步读取所有命令的输出并按行增量解码。
- 每个日志视图最多保留最近 1000 行。
- 使用 Job Object 管理完整进程树。
- 停止时先发送 `CTRL_BREAK_EVENT`。
- 超时后使用 Job Object 或 `taskkill /T /F` 强制终止。
- 支持停止后重新启动。
- 应用退出前停止所有仍在运行的命令。

### 7.1 命令行语义

启动前先按第 5.4 节的规则把换行归一化为空格。Windows 命令行不是 POSIX shell 语法，原生版本不得使用等价于 `shlex.split()` 的空白拆词逻辑：

- 直接模式等价于 `shell=False`。将归一化后的完整命令行放入可写缓冲区并交给 `CreateProcessW`，保留 Windows 的引号和反斜杠语义。若为安全或诊断需要单独取得可执行文件名，使用 `CommandLineToArgvW` 只解析首个参数，不能重新拼接整个命令行。
- Shell 模式用于 `.bat`、`.cmd`、管道、重定向和 `&&` 等需要 shell 启动行为的命令。通过 `%COMSPEC% /D /S /C` 执行，并按 `cmd.exe` 的规则正确引用原始命令行。
- 两种模式都必须传入配置的 Working Directory，并分别覆盖带空格路径、嵌套引号、末尾反斜杠、Unicode 参数和空参数的测试。

### 7.2 创建参数和前台窗口

创建普通命令时使用 `CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW`，并设置 `STARTF_USESHOWWINDOW`、`SW_HIDE`。这既避免弹出控制台窗口，也保留 v0.1.5 对 GnuPG `gpg-agent`/`pinentry-basic.exe` 抢占前台的回归修复。Shell 和直接模式必须使用相同的无窗口策略。

实现和测试时需确认 `CREATE_NO_WINDOW`、进程组及 `CTRL_BREAK_EVENT` 的组合在无控制台 GUI 进程中仍能完成预期的优雅停止；如果 Windows 限制导致无法投递 Ctrl 事件，应在平台层创建和附加临时隐藏控制台，而不能通过移除无窗口标志重新引入闪窗或抢焦点问题。

### 7.3 异步 I/O 和状态机

使用 IOCP/重叠 I/O 实现单个共享工作线程。若匿名管道不能满足可靠的重叠读取，可在内部改用具有匿名语义的唯一命名管道，但不能改变外部行为。stdout 和 stderr 各自维护增量解码器，按块读取，按 `LF` 分行，并在 EOF 时提交没有换行的最后一段；无效字节使用替换字符，不能导致读取任务退出。

UI 线程只消费不可变事件。每次启动递增 generation，旧进程迟到的日志或退出事件不得污染新一代运行。状态至少包含 `STOPPED`、`STARTING`、`RUNNING`、`STOPPING`、`EXITED` 和 `FAILED`。停止失败时必须记录错误并从 `STOPPING` 恢复为 `RUNNING`，不能永久卡在 `STOPPING`；重启只能在旧进程确认结束后开始新一代。

WIL 用于管理以下资源：

```cpp
struct ChildProcess
{
    wil::unique_handle process;
    wil::unique_handle thread;
    wil::unique_handle job;
    wil::unique_handle stdoutRead;
    wil::unique_handle stderrRead;
};
```

创建子进程时使用 `STARTUPINFOEXW` 和 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`，只继承 stdout/stderr 的管道写端和作为 stdin 的空设备句柄，避免将管道读端或其他无关句柄泄漏给子进程。

为避免子进程在加入 Job Object 前创建逃逸的后代，优先使用 `CREATE_SUSPENDED` 创建，完成 Job Object 分配后再恢复主线程。若 Job Object 创建或分配失败，写入 Standard error 日志，并保留 `taskkill /PID <pid> /T /F` 作为强制停止后备路径。

## 8. 配置兼容性

继续使用：

```text
%LOCALAPPDATA%\CommandRunner\commands.json
```

保持当前 JSON schema 和字段名称兼容：

- `version`
- `commands`
- `preferences`
- `id`
- `name`
- `working_directory`
- `command_line`
- `encoding`
- `auto_start`
- `shell`
- `wrap_lines`
- `auto_scroll`

兼容性细节：

- 缺失的 `auto_start` 和 `shell` 按 `false` 处理。
- 缺失的 `wrap_lines` 按 `false` 处理，缺失的 `auto_scroll` 按 `true` 处理。
- 读取旧配置时忽略已经废弃的 `execution_mode` 字段及未知字段，不能因此拒绝整个文件。
- 应用初始化完成后自动启动所有 `auto_start=true` 的命令，每个命令仍须经过正常状态机和错误报告路径。
- 配置读取失败时显示英文 Configuration Error，并以安全的空命令列表继续运行，不能在启动阶段崩溃或覆盖损坏的原文件。

保存时继续采用临时文件加原子替换，避免程序异常退出导致配置损坏。迁移后的首个版本必须能直接读取 Python 版本创建的配置文件。

## 9. 托盘功能

使用 `Shell_NotifyIconW` 实现，不再依赖 pystray 或 Pillow。封装独立的 `TrayIcon` 类，负责：

- 添加、更新和删除托盘图标。
- 双击或默认菜单项恢复主窗口。
- Start All、Stop All 和 Exit 菜单。
- Explorer 重启后响应 `TaskbarCreated`，重新注册图标。
- 在析构和正常退出路径中删除图标。

保持 v0.1.2 的资源生命周期：成功最小化到托盘后销毁主窗口及其 GUI 控件资源，只保留核心状态和进程服务；恢复时先删除并释放托盘图标/菜单，再按当前状态重建主窗口。托盘创建失败时不得销毁或隐藏主窗口。重复最小化和恢复不能累积 HWND、图标、菜单或线程资源。

关闭主窗口时，若没有运行中的命令则直接退出；若仍有命令，显示居中的模态对话框并提供 Stop Commands and Exit、Cancel 和 Minimize to Tray 三个选择。Cancel 是默认安全行为；Stop Commands and Exit 必须等待停止流程完成后再释放进程服务。

## 10. 构建与体积优化

Release 构建建议：

```text
/O1
/GL
/Gw
/Gy
/LTCG
/OPT:REF
/OPT:ICF
```

其他原则：

- 初期优先保证行为正确，不在迁移阶段过早进行极端体积优化。
- 比较 `/MD` 和 `/MT` 的最终体积及部署要求。
- 若要求真正单 EXE，优先 `/MT`，接受静态 CRT 增加的体积。
- 不发布 PDB，但 CI 应保存 PDB 作为崩溃分析制品。
- 不默认使用 UPX；它可能增加杀毒软件误报，并影响签名和诊断。
- 使用 `dumpbin /imports` 和 `dumpbin /headers` 检查依赖与节区体积。
- 对每个 Release 构建记录未签名和签名后的 EXE 大小。
- 在 `.rc` 中维护 `VERSIONINFO`，保证文件版本、产品版本、描述、产品名、版权和语言可在 Windows 文件属性中查看；发布时与 Git tag/Changelog 版本同步更新。

第一阶段的合理目标是低于 2 MB；低于 1 MB 作为优化目标，而不是功能验收的硬性前提。

## 11. 依赖管理

- 固定 Win32++ 和 WIL 的具体版本，不直接跟随最新分支。
- 使用 CMake Presets 固定 x64 Release/Debug 构建参数。
- Win32++ 只引入项目需要的头文件。
- WIL 可通过 NuGet、vcpkg 或固定版本源码使用。
- 记录每个依赖的版本、许可证和更新步骤。
- 升级 Win32++ 时重点测试 RichEdit、CResizer、高 DPI 和托盘行为。

## 12. 迁移步骤

### 阶段一：核心模型和配置

- 建立 CMake、资源文件和空主窗口。
- 迁移数据模型和 JSON 配置。
- 添加配置兼容性测试，包括缺失新字段、遗留 `execution_mode`、未知字段、损坏文件和原子保存。

### 阶段二：进程管理

- 实现 CreateProcessW、管道读取和状态机。
- 实现 Job Object、停止、强制终止和重启。
- 测试 stdout/stderr、不同编码、EOF 尾行、代际隔离和进程树清理。
- 测试多行归一化、Windows 引号规则、Shell/直接模式、无控制台窗口和不抢前台。
- 压力测试同时运行多个命令时始终只使用一个共享 I/O 工作线程。

### 阶段三：主窗口

- 实现 ActionBar、ListView、Splitter、OptionsBar 和 RichEdit。
- 实现选择、多选、第 5.1 节列出的完整键盘操作、日志过滤和自动滚动。
- 实现窗口最小尺寸、列宽和自适应布局。

### 阶段四：对话框和托盘

- 实现添加、编辑、删除和关闭确认对话框。
- 实现托盘图标及菜单。
- 实现关闭时停止、取消或最小化到托盘，并验证主窗口/托盘资源交替创建和释放。
- 验证命令编辑器的多行输入、滚动条、Ctrl+Enter、横向缩放和居中行为。

### 阶段五：兼容性和优化

- 在 Windows 11 x64 上执行完整回归测试。
- 测试高 DPI、多显示器、键盘和屏幕阅读器。
- 测试 Explorer 重启后的托盘恢复。
- 开启 Release 优化并测量体积。
- 最后进行代码签名和杀毒软件误报检查。

## 13. 验收标准

- 单个 EXE 可在干净的 Windows 11 x64 环境启动。
- 不需要安装 Python、.NET 或第三方 GUI DLL。
- 能直接读取现有 `commands.json`。
- Shell 与直接模式的命令行语义与 v0.1.5 一致，多行命令启动前正确归一化，路径、引号和 Unicode 参数不被错误拆分。
- 启动命令时不出现控制台窗口，GnuPG `pinentry-basic.exe` 不抢占前台。
- 所有命令共用一个 I/O 工作线程，stdout/stderr 和最后一个无换行片段均不丢失。
- 所有命令操作、状态转换和日志功能与当前版本一致，停止失败不会卡在 `STOPPING`。
- 窗口缩放时控件不重叠、不截断，Splitter 可拖动。
- 命令对话框横向缩放时内容宽度同步，多行编辑、垂直滚动和 Ctrl+Enter 可用。
- 在不同 DPI 显示器间移动时布局和字体正确。
- 第 5.1 节的键盘快捷键、多选、焦点、双击编辑和所有 Access key 均可用。
- 托盘菜单和 Explorer 重启恢复正常，反复最小化/恢复无 GUI 或托盘资源泄漏。
- 停止命令能够清理完整进程树。
- Release EXE 首要目标低于 2 MB，必须低于 5 MB。

## 14. Changelog 行为回归清单

本节把 `CHANGELOG.md` 中会影响原生重写的条目集中成清单。实现、代码审查和发布验收时逐项勾选，防止后期修复在重构中被遗漏。

### v0.1.5

- [ ] Shell 选项已进入模型、JSON、对话框、启动实现和测试；默认仍为直接执行。
- [ ] 命令对话框内容宽度跟随窗口宽度。
- [ ] Command Line 是带垂直滚动条的多行编辑器；保存快捷键是 Ctrl+Enter。
- [ ] 启动时把 CRLF、CR、LF 替换为空格，但配置中保留原始文本。
- [ ] 所有进程的异步读写和等待共用一个工作线程。
- [ ] 无控制台启动，不出现 `pinentry-basic.exe` 抢前台回归。
- [ ] 创建参数包含 `CREATE_NO_WINDOW`。
- [ ] Windows 直接模式不使用 POSIX `shlex.split()` 语义。

### v0.1.4 和 v0.1.3

- [ ] ListView 完整支持 Ctrl/Shift 多选、焦点移动、Home/End、Enter、Delete、Space 和 Ctrl+A。
- [ ] 直接模式等价于 `shell=False`，不会隐式交给命令解释器。
- [ ] `auto_start` 可配置、可持久化、显示在表格中，并在应用启动时生效。

### v0.1.2 和 v0.1.1

- [ ] 最小化到托盘后释放主窗口 GUI 资源；恢复主窗口时释放托盘资源，核心状态不受影响。
- [ ] UI、状态和进程平台层保持分离；对话框为居中的模态原生对话框。
- [ ] 工具栏为无多余分隔条的紧凑按钮组，界面文本为英文。
- [ ] 配置启动错误会显示给用户且不会导致程序崩溃或覆盖原文件。
- [ ] 所有 UI 控件都有 Mnemonic/Access key；双击仅编辑可编辑的命令。
- [ ] Stop 能清理进程树，失败路径不会停留在 `STOPPING`；Restart 等待 Stop 完成。
- [ ] Word wrap 默认关闭；Clear 位于 Jump to Latest 左侧；日志可切换 Combined/stdout/Standard error。

### v0.1.0 和发布项

- [ ] 多命令配置、批量 Start/Stop/Restart、工作目录、实时日志及每视图 1000 行上限均保留。
- [ ] 关闭窗口时的 Stop Commands and Exit、Cancel、Minimize to Tray 三种选择均保留。
- [ ] Job Object 和 `taskkill /T /F` 后备路径都能清理进程树。
- [ ] 交付单个 Windows 11 x64 EXE，并包含正确的 Windows `VERSIONINFO`。

`requirements*.txt`、PyInstaller 安装位置、Python 类型检查和 pytest 修复属于旧实现的构建维护项，不直接移植到 C++。它们分别由固定的 CMake/依赖清单、MSVC 警告与静态分析、原生单元测试和 CI 构建检查替代。
