# mhtab.exe 还原源代码（伪代码）

## 重要声明

**这不是可直接编译的源代码**，而是从 IDA Pro Hex-Rays 反编译器导出的 **C/C++ 伪代码**，经过：

- 函数命名（基于字符串证据 + 调用模式）
- 全局变量重命名
- 添加中文业务注释
- 按业务模块重新组织文件
- 修正最显著的命名（如 `sub_140014E50` → `TabCtrlApp_Init`）

仍保留 IDA 风格的伪类型：

| IDA 类型 | 实际含义 |
|---|---|
| `__int64` | `int64_t` 或 `intptr_t` |
| `_QWORD` | 64 位无符号整数（通常是指针） |
| `_DWORD` / `_WORD` / `_BYTE` | 32/16/8 位 |
| `__int128` / `_OWORD` | 128 位（SSE 寄存器内容） |
| `__fastcall` | x64 默认 ABI |

## 阅读顺序建议

| 顺序 | 文件 | 内容 |
|---|---|---|
| 1 | `include/types.h` | 数据结构定义（View slot、ViewContainer 偏移说明） |
| 2 | `include/globals.h` | 全局变量与单例声明 |
| 3 | `src/main.c` | `WinMain` 主入口 |
| 4 | `src/TabCtrlApp.cpp` | `TabCtrlApp::init` + 单例 + 事件总线 |
| 5 | `src/MainFrame.cpp` | 主窗口注册、WndProc、消息分发 |
| 6 | `src/Dispatch.cpp` | 13 个 IPC 消息处理器（msg 1~26 / 4102~4104） |
| 7 | `src/View.cpp` | View 激活/停用/关闭/拖拽 |
| 8 | `src/TabsList.cpp` | View 槽位分配与查找 |
| 9 | `src/RookieTip.cpp` | 新手引导 tooltip |
| 10 | `src/Util.c` | INI、Hash、sprintf、sscanf |
| 11 | `src/Skin.cpp` | Tab 头位图渲染 |

## 文件来源说明

所有函数体都来自 `mhtab.exe.i64` 中的 Hex-Rays 反编译结果，地址注释在每个函数定义之上（`/* @0x... */`）。

## 未完成

- `MainFrame_WndProc` 的 4041 字节内部 switch 仅以伪代码片段呈现，216 个 case 未拆分（详见 TODO）
- 大量 STL/CRT 内部辅助函数仍以 `sub_xxx` 形式出现
- `my.exe` 子进程未参与还原

## 与 docs/ 的关系

- `docs/mhtab_reverse/DESIGN_*.md` —— **架构层** 设计文档（mermaid 图、数据流）
- `src/` —— **代码层** 还原伪代码（本目录）

二者配合阅读效果最佳。
