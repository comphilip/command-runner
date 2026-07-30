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
dist\CommandRunner\CommandRunner.exe
```

开发阶段使用 `onedir`，更容易定位依赖或 DLL 问题。PyInstaller 不是交叉编译器，因此
正式 Windows EXE 应在 Windows（或 Wine 中的 Windows Python）构建。本项目涉及托盘和
Job Object，最终版本务必在真实 Windows 11 上测试。

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
