# Command Runner

Windows 11 桌面命令运行器。它可以保存多条命令，批量启动、停止和重启，并实时查看
stdout、stderr 或合并日志。每个日志视图最多保留最近 1000 行。

## 功能

- 添加、编辑、删除多条命令配置
- 多选启动、停止、重启
- Shell 与直接执行两种模式
- stdout、stderr、综合日志视图
- 自动换行、智能自动滚动、跳到最新
- 配置原子保存到 `%LOCALAPPDATA%\CommandRunner\commands.json`
- 最小化到系统托盘
- 有任务运行时，关闭窗口提供“关闭命令并退出 / 取消 / 最小化到托盘”
- Windows Job Object 管理进程树；停止时先尝试优雅退出，超时后强制终止

命令不支持 stdin 交互。需要实时查看 Python 子程序输出时，请使用 `python -u`。

## Windows 开发运行

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe app.py
```

如果是 Microsoft Store Python，也可以把 `py -3` 换成 `python`。

## 在 Windows 构建

把整个目录同步到 Windows 后，双击 `build-windows.bat`，或运行：

```powershell
.\build-windows.ps1
```

输出位于：

```text
dist\CommandRunner.exe
```

当前 spec 生成单文件程序。若 Windows 构建机安装了 UPX，PyInstaller 会进一步压缩支持
的二进制组件。PyInstaller 不是交叉编译器，因此正式 Windows EXE 应在 Windows（或
Wine 中的 Windows Python）构建。本项目涉及托盘和 Job Object，最终版本务必在真实
Windows 11 上测试。

Windows 文件属性中的说明、文件版本、产品名称、产品版本、版权和语言由
`version_info.txt` 定义。发布新版本时，需要同步修改其中的 `filevers`、`prodvers`、
`FileVersion` 和 `ProductVersion`。

## GitHub 自动发布

推送名称符合 `v*` 的 tag 时，GitHub Actions 会在 Windows VM 中安装依赖、使用
PyInstaller 构建 `CommandRunner.exe`、创建同名 GitHub Release 并上传 EXE。

发布前必须在 `CHANGELOG.md` 中添加与 tag 完全同名的二级标题：

```markdown
## v1.2.0

### Added

- xxx

### Fixed

- yyy
```

工作流只提取该版本标题下、下一个 `##` 标题之前的内容作为 Release notes。如果找不到
对应章节或章节为空，工作流会停止，不会创建 Release。

发布示例：

```bash
git tag v1.2.0
git push origin v1.2.0
```

## 操作说明

“通过 Shell”适合 `.bat`、`.cmd`、管道、重定向和 `&&`；“直接执行”适合普通可执行
程序及参数。命令以当前用户权限运行。不要导入并执行不可信配置。

应用最小化时需要 `pystray` 和 Pillow；它们已列在 `requirements.txt`。

## Linux 侧验证

Linux 可验证配置、日志和 POSIX 进程组清理，但不能验证 Windows 托盘与 Job Object：

```bash
python3 -m pytest
python3 -m compileall app.py command_runner
```
