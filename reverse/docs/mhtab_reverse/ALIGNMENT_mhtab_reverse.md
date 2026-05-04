# ALIGNMENT — mhtab.exe 代码还原任务

## 1. 原始需求
通过 IDA Pro MCP 服务对 `E:\jinyiNew\mhtab\mhtab.exe` 进行**代码还原（逆向工程）**。

## 2. 目标识别（Ground Truth）

| 项 | 值 |
|---|---|
| 文件 | `E:\jinyiNew\mhtab\mhtab.exe` |
| 大小 | 4,361,720 字节 (~4.16 MB) |
| 架构 | x86_64 PE |
| ImageBase | `0x140000000` |
| MD5 | `cbcab699e1bc8e7eaad488cc5dfeb794` |
| SHA256 | `9f1bae435fbb6f53a4883e53165b45c22a731f6c5f1c11de64646cb640eb40b5` |
| 入口点 | `0x14001a370` (CRT `start`) |
| 函数总数 | 1140（已命名 26、库函数 584、未命名 530） |
| 字符串总数 | 573 |

## 3. 业务推断（基于强字符串证据）

- 程序名 `mhtab` = "梦幻西游 Tab" 即 **梦幻西游多窗口标签栏管理工具**
- 关键 C++ 类（出现在 RegisterClass 调用与字符串中）：
  - `MHXYMainFrame` —— 主框架窗口类
  - `MHXYWinMgr` —— 子窗口/游戏窗口管理器（管理 Tab）
  - `RookieTipWindow` —— 新手提示窗口
- UI 控件：`SysTabControl32`（标签）、`tooltips_class32`
- 时间显示格式：`[%02d:%02d.%02d] `（计时显示）
- 日志/格式：`%s %d %s`、`%s %d`（推测是 Tab 标题模板）

## 4. 关键 API 类别

| 类别 | API | 推断用途 |
|---|---|---|
| 进程操作 | `OpenProcess`、`CreateProcessA`、`AttachThreadInput`、`GetWindowThreadProcessId` | 附加/控制游戏进程窗口 |
| 窗口操作 | `FindWindowExA`、`SetWindowLongPtrA`、`SetParent` (经 `SetWindowLongA`)、`SetForegroundWindow`、`BringWindowToTop` | 把游戏子窗口嵌入到 Tab |
| 消息发送 | `SendMessageA`、`PostMessageA`、`SendMessageTimeoutA` | 转发输入到游戏 |
| 加密哈希 | `CryptCreateHash`、`CryptHashData`、`CryptGetHashParam` | 配置/校验文件哈希 |
| 配置 | `GetPrivateProfileStringA`、`WritePrivateProfileStringA` | INI 配置读写 |
| 同步 | `OpenMutexA`、`CreateMutexA` | 单例运行 |
| 资源/UI | LoadBitmapA、CreateCompatibleBitmap、AlphaBlend、TransparentBlt | Tab 皮肤绘制 |

## 5. 还原边界

### 范围内
- WinMain → 主窗口注册 → 主消息循环
- `MHXYMainFrame` 主窗口 WndProc + 子窗口管理逻辑
- `MHXYWinMgr` Tab 管理（添加/删除/切换/附加外部窗口）
- `RookieTipWindow::init` 等关键成员
- 哈希、INI、定时器辅助函数
- 高频被引用函数（xref ≥ 10 的全部命名）

### 范围外
- C/C++ 标准运行时（CRT、STL 容器）已被 IDA 部分识别为 lib，仅在阻挡主流程时局部还原
- 完整 1140 个函数全量还原（无业务价值，避免冗余）

## 6. 验收标准

1. 主入口、主窗口注册与消息循环已被命名并附中文注释
2. `MHXYMainFrame` 与 `MHXYWinMgr` 相关方法（≥10 个）函数名可读
3. 关键全局变量（主窗口句柄、Tab 数组、配置等）被命名
4. 输出 `FINAL_mhtab_reverse.md`：含模块图、关键函数清单、调用关系
5. IDB 已保存（`mhtab.exe.i64`）

## 7. 关键假设（待验证）

- [ ] 程序使用 Win32 SDK + ATL/自封装 C++（非 MFC、非 Qt）
- [ ] Tab 控件使用标准 `SysTabControl32` 由主窗口宿主管理
- [ ] 配置 INI 文件路径由 `GetCurrentDirectoryA + GetPrivateProfileStringA` 组合定位
- [ ] 单实例机制使用命名互斥锁

## 8. 决策原则

- 命名规范：成员函数采用 `MHXYWinMgr::OnXxx`/`Cls_method` 风格；非 OO 全局函数用 `snake_case` 描述用途
- 注释语言：中文
- 类型还原：通过 `set_type` 仅在能完全确定签名时使用，避免错误传播
- 顺序：从入口向下广度优先 → 高 xref 函数 → 字符串引用函数
