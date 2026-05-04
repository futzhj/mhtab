# 还原笔记

## 写作风格说明

由于 Hex-Rays 反编译器的产物是"伪 C"而非真实源码，本目录下所有 .c/.cpp 文件都应当视为**可读性优化后的伪代码**，它们有以下特征：

### ✅ 可信部分

- 控制流（if/while/for/switch）
- 函数调用图（谁调用了谁）
- 字符串常量与格式化参数
- 结构体字段偏移（我基于使用模式反推）
- Win32 API 调用及其参数
- 自定义消息号（msg 1/3/4/6/7/8/16/17/25/26/4102/4103/4104）
- 线程安全单例模式（C++11 magic statics）

### ⚠️ 推测部分

- 变量名（除非函数名通过字符串证据确认）
- 结构体字段含义（仅部分字段可从业务上下文推断）
- 部分业务分支的意图

### ❌ 不可信部分

- IDA 变量 `v1, v2, ..., v42` 仅是反编译器自动命名
- 强制类型转换可能不对应原 C++ 类型
- C++ 成员函数被展平为 `__fastcall` 函数，`this` 是第一个参数
- 异常处理（throw/catch）在伪代码中经常缺失或扭曲

## 文件映射表

| src/ 文件 | IDA 地址范围 | 大小 | 说明 |
|---|---|---|---|
| `main.c` | `0x140017340` | 1719 B | WinMain |
| `TabCtrlApp.cpp` | `0x1400087F0..0x1400128B0` | ~3KB | 单例 + 事件订阅 |
| `TabCtrlApp_methods.cpp` | `0x140013060..0x140016BE0` | ~5KB | viewNew / waitforCmd / 3 OnEvent |
| `MainFrame.cpp` | `0x14000AD90`（thunk）+ `0x14000AFF0`（WndProc） | 骨架 | 完整 WndProc 4041B 仅标注 case |
| `Dispatch.cpp` | 13 个处理器，详见每函数头注释 | ~12KB | 最核心业务 |
| `View.cpp` | `0x140007140` / `0x140007670` | ~2KB | Activate / Deactivate |
| `TabsList.cpp` | `0x1400181F0 / 0x1400184D0 / 0x140018640` | ~3KB | 槽位池 |
| `RookieTip.cpp` | `0x14000DD20 / 0x14000DF20` | ~1KB | Tooltip 子类化 |
| `Util.c` | INI / Hash / sprintf 等 | ~3KB | 基础设施 |

## 关键发现

### 1. IPC 协议双层设计

- **子进程层**：`my.exe` 子进程通过自定义 msg 1/3/4/6/7/8 等与主进程 `mhtab.exe` 通信。
- **主进程层**：不同实例之间用 `WM_COPYDATA (0x4A)` + 三个自定义 msg `0x8101/0x8102/0x8029` 通信。

### 2. 单实例互斥锁命名方案

- **互斥锁名**：`"MHXYWinMgr" + MD5(workdir)hex` → 每个安装目录一个
- **窗口类名**：`"MHXYMainFrame" + MD5(workdir)hex` → 可 FindWindow
- **额外限制**：`"mhxy0"~"mhxy7"` 8 个全局命名互斥锁，限制系统内最多 8 个梦幻西游子进程

### 3. 子进程命令行格式

```
my.exe <prefix> <fmt><slot> <div> <mod> <pid> <workdir_id> <pad08d>
```

### 4. 拖拽状态机

- `drag_state` @MainFrame+1120
  - 0: 空闲
  - 1: 正在拖拽
  - 2: 正在跨主框架转移

### 5. 子进程心跳

主进程用 `SendMessage(view_hwnd, 0x1304, 0, 0)` 检查子进程存活状态，返回值：
- 0/1: 正常
- 其他: 视为异常 → `TryReloadView` 复活

## 验证方法

如需验证还原的准确性，可：

1. 用 `dumpbin /exports mhtab.exe` 对照导入表
2. 在 IDA 中 CTRL+X 查看 xref，对照本目录的调用图
3. 动态调试时，把这些文件作为"源码映射"参考阅读
4. 用 `windbg -c ".srcpath+ e:\jinyiNew\mhtab\src" mhtab.exe` 配合 Microsoft Debugger

## 已识别但未完整呈现的函数

完整 4041 字节 WndProc 太大，可通过 IDA 的 Tools → Produce File → Create LST file 导出全函数反编译结果到 `e:\jinyiNew\mhtab\full_wndproc.c`。
