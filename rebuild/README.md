# mhtabx

一个轻量的多进程 Tab 容器。可将多个独立子进程的主窗口嵌入到同一个 Tab 控件中，在视觉上形成类似浏览器标签的多开体验。

本项目是 `mhtab.exe`（梦幻西游多开管理器）的**开源功能等价复刻**，基于纯 Win32 API + C++17，不包含任何原二进制代码。

## 功能

- ✅ 单实例互斥锁 + 命令行转发（避免重复启动）
- 🚧 主窗口 + Tab 控件（W2）
- 🚧 子进程启动 + `SetParent` 嵌入 Tab 页（W2）
- 🚧 子进程生命周期监控（`WaitForMultipleObjects`）（W2）
- 🚧 13 种自定义消息 IPC 协议（W3）
- 🚧 子进程 crash 自动 reload（W3）
- 🚧 Tab 拖拽重排 + 跨主框架拖拽（W4）
- 🚧 自绘皮肤（W4）

## 构建

### 前置要求

- **Windows 10 RS1 (1607)** 或更高
- **Visual Studio 2022**（MSVC v143）
- **CMake 3.20+**

### 快速编译

```powershell
cd E:\jinyiNew\mhtabx
cmake -B build -A x64
cmake --build build --config Release
.\build\bin\Release\mhtabx.exe
```

### 使用其他 IDE

- **CLion**：直接 Open Project，选择顶层 `CMakeLists.txt`
- **VSCode**：安装 CMake Tools 扩展后打开目录

## 使用

```powershell
# 首启：打开主窗口
mhtabx.exe "child_program.exe --arg1 --arg2"

# 再次启动：自动转发到已有实例的新 Tab
mhtabx.exe "another_child.exe"

# 测试：启动 demo_child.exe 验证嵌入流程
mhtabx.exe "demo_child.exe"
```

## 与原 mhtab.exe 的差异

| 项 | 原 mhtab.exe | mhtabx |
|---|---|---|
| 子进程名 | 固定 `my.exe` | 命令行指定任意程序 |
| 并发上限 | 8（命名互斥锁 `mhxy0~mhxy7`）| 可配置（默认 8） |
| 皮肤 | 硬编码梦幻西游风格 | 可换皮肤资源 |
| 依赖 | OpenSSL / DirectX | 仅 Win32 API |

## 目录结构

```
mhtabx/
├── include/          公共头
├── src/              实现
│   └── resource/     资源文件（rc + 图标）
├── child/            示例可嵌入子进程
└── docs/mhtabx/      设计文档（架构、任务、进度）
```

## License

MIT License - 详见 LICENSE 文件
