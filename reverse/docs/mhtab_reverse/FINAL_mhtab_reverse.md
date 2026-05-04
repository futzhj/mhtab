# FINAL — mhtab.exe 代码还原报告

## 1. 任务摘要

| 项 | 内容 |
|---|---|
| 目标 | `E:\jinyiNew\mhtab\mhtab.exe` 代码还原 |
| 工具 | IDA Pro + MCP 服务（127.0.0.1:13337） |
| 成果 | 命名 60+ 核心函数、添加 22 处中文注释、产出 3 份文档 |
| IDB | `E:\jinyiNew\mhtab\mhtab.exe.i64`（已保存） |
| 还原范围 | 入口 → 主框架 → Tab 管理 → 子进程协议 → INI/Hash/UI 等业务核心 |

## 2. 程序定性

`mhtab.exe` 是一款 **梦幻西游(MHXY) 多开 Tab 管理器**。其核心架构：

> 主进程 `mhtab.exe` 通过 `CreateProcessA` 启动多份 `my.exe` 子进程，每个子进程承载一个独立的游戏窗口；主进程把所有子窗口"收编"到一个 Tab 控件下，类似浏览器多标签页。最多支持 **8 个并行实例**（命名互斥锁 `mhxy0~mhxy7`）。

证据来自字符串：

- `MHXYMainFrame` / `MHXYWinMgr` —— 主窗口与子窗口类名前缀
- `TabCtrlApp::viewNew : start my.exe, ARG_LEN = %d; arg = %s\n`
- `TabCtrlApp::waitforCmd START and receive an event`
- `Dispacth::newClient : first receive the message ...`（注：原作者拼写为 `Dispacth`）
- `RookieTipWindow::init`、`RookieGuide::loadState`、`TabBarPlus::doDragging`

## 3. 核心模块清单

### 3.1 入口与初始化

| 地址 | 函数名 | 作用 |
|---|---|---|
| `0x14001A370` | `start` | CRT 入口（`__scrt_common_main_seh`） |
| `0x140017340` | `WinMain` | 主入口：单例检查 + 主消息循环 |
| `0x140014E50` | `TabCtrlApp_Init` | TabCtrlApp::init 应用初始化 |
| `0x14000DD20` | `RookieTipWindow_Init` | 新手提示窗口初始化 |
| `0x14000DF20` | `RookieGuide_loadState` | INI 加载新手引导状态 |

### 3.2 单例对象

| 地址 | 函数名 | 全局变量 |
|---|---|---|
| `0x1400087F0` | `TabCtrlApp_GetInstance` | `g_TabCtrlApp_instance` (`0x14004FE38`) |
| `0x1400088C0` | `MainFrameSingleton_GetInstance` | `g_MainFrame_instance` (`0x14004FEB0`) |
| `0x140018460` | `TabsList_GetInstance` | `g_TabsList` (`0x1400519B0`) |
| `0x14000ACF0` | `RookieTipWindow_GetInstance` | — |

### 3.3 主窗口过程与 Tab 控制

| 地址 | 函数名 | 作用 |
|---|---|---|
| `0x14000AD90` | `MainFrame_WndProc_thunk` | C++ this 绑定 thunk（WM_NCCREATE） |
| `0x14000AFF0` | `MainFrame_WndProc` | **主 WndProc**（4041 字节，216 case） |
| `0x140018A30` | `MainFrame_RegisterMsgHandler` | 注册自定义消息处理器 |
| `0x140016970` | `MainFrame_PostMsg` | 主框架内消息发送 |
| `0x140015D20` | `MainFrame_Cleanup` | 主框架清理 |
| `0x140015910` | `MainFrame_AcquireViewContainer` | 获取/分配 ViewContainer 槽 |

### 3.4 13 个 Dispatch 消息处理器

| msg | 地址 | 函数名 | 作用 |
|---|---|---|---|
| 1 | `0x140015480` | `Dispatch_NewClient_msg1` | 子进程注册 |
| 3 | `0x140013C80` | `Dispatch_OnReadyConfirm_msg3` | 客户端就绪确认 |
| 4 | `0x1400133F0` | `MainFrame_OnMsg4_ForwardInput` | 转发键鼠输入 |
| 6 | `0x1400162D0` | `Dispatch_UpdatePos_msg6` | 位置更新 |
| 7 | `0x140013150` | `Dispatch_ActivateView_msg7` | 激活（不抢前台） |
| 8 | `0x140016140` | `Dispatch_HideView_msg8` | 隐藏视图 |
| 16 | `0x140015C90` | `MainFrame_OnMsg16_NewView` | 新建 view |
| 17 | `0x140015A30` | `Dispatch_GetByParentHwnd_msg17` | 通过父 HWND 查 view |
| 25 | `0x1400152E0` | `Dispatch_CleanupView_msg25` | 视图清理 |
| 26 | `0x140015F00` | `Dispatch_ShowWindow_msg26` | 改 ShowCmd |
| 4102 | `0x1400163F0` | `Dispatch_Activate_msg4102` | 激活 + focus |
| 4103 | `0x140012EE0` | `Dispatch_BringToFront_msg4103` | **强制置顶（AttachThreadInput 技巧）** |
| 4104 | `0x140015B70` | `Dispatch_FindByHwnd_msg4104` | 通过 HWND 查找 view |

### 3.5 应用事件总线

| event | 地址 | 函数名 | 作用 |
|---|---|---|---|
| 1 | `0x140013060` | `TabCtrlApp_OnEvent1_InitTabList` | 初始化双向链表 |
| 2 | `0x140015390` | `TabCtrlApp_OnEvent2_TimerPoll` | 定时轮询 active view |
| 80 | `0x140013980` | `TabCtrlApp_OnEvent80_OnReady` | App 就绪后健康检查 |
| —  | `0x140008B90` | `TabCtrlApp_AddListener` | 订阅 |
| —  | `0x140008AE0` | `TabCtrlApp_RemoveListener` | 取消订阅 |

### 3.6 子进程管理

| 地址 | 函数名 | 作用 |
|---|---|---|
| `0x1400165F0` | `TabCtrlApp_ViewNew` | **CreateProcessA 启动 my.exe** |
| `0x140014000` | `TabCtrlApp_CloseAndRecycle` | 关闭旧 view + 启动新 view |
| `0x140016BE0` | `TabCtrlApp_WaitForCmd` | **WaitForMultipleObjects 监控子进程退出** |
| `0x140018390` | `GetActiveTabCount` | 检查互斥锁 mhxy0~mhxy7（最多 8） |

### 3.7 View 操作

| 地址 | 函数名 | 作用 |
|---|---|---|
| `0x140007140` | `View_Activate` | 激活 view（SetWindowPos+SetFocus） |
| `0x140007670` | `View_Deactivate` | 取消置顶 |
| `0x140007BA0` | `View_ForceClose` | 强制关闭子进程 |
| `0x1400078A0` | `View_RemoveTab` | 从 Tab 控件移除 |
| `0x140018780` | `TabView_PollTick` | 计时轮询 |
| `0x1400185E0` | `View_TimerExpired` | 定时器到期 |
| `0x140007C50` | `View_HoldDragging` | 拖拽悬停 |
| `0x14000EF80` | `TabCtrl_SwitchTab` | Tab 切换 |
| `0x140013200` | `TryReloadView` | 重载失效 view |

### 3.8 TabsList & ViewContainer

| 地址 | 函数名 | 作用 |
|---|---|---|
| `0x1400181F0` | `TabsList_AllocSlot` | 分配 0x278 字节 view 槽 |
| `0x1400184D0` | `TabsList_FreeSlot` | 释放槽 |
| `0x140018640` | `TabsList_FindByProcHandle` | 通过 hProcess 查 view |
| `0x140008200` | `View_Container_Init` | 0xB50 字节 ViewContainer 初始化 |
| `0x140006A20` | `View_Container_Setup` | ViewContainer 二次设置 |

### 3.9 拖拽（Tab 重排序）

| 地址 | 函数名 | 作用 |
|---|---|---|
| `0x14000F050` | `TabBarPlus_doDragging` | 拖拽过程 |
| `0x1400142A0` | `TabCtrlApp_endDragging` | 结束拖拽（含跨主框架） |
| `0x140015D90` | `TabCtrlApp_BeginDragging` | 开始拖拽 |

### 3.10 加密 / Hash / 配置

| 地址 | 函数名 | 作用 |
|---|---|---|
| `0x1400146C0` | `GetUniqueIdSuffix` | **MD5(workdir) 作为类名后缀** |
| `0x14000EAA0` | `Hash_StringToU32` | 自定义 32 位混淆 hash |
| `0x14000EE30` | `GetWorkdirInstanceID` | 缓存工作目录 hash |
| `0x140014940` | `MakeMHXYWinMgrClassName` | "MHXYWinMgr"+UID |
| `0x140008E90` | `MainFrame_SaveClassName` | 主类名存全局 |
| `0x140014B40` | `get_ini` | INI 字符串读取 |
| `0x14000EA40` | `my_sscanf` | sscanf 包装 |
| `0x14000BFC0` | `my_sprintf` | sprintf 包装 |
| `0x140014890` | `Calc_30s_Bucket` | 30 秒粒度时间桶 |

### 3.11 关键全局变量

| 地址 | 名称 | 类型 | 作用 |
|---|---|---|---|
| `0x14004FE38` | `g_TabCtrlApp_instance` | dword | App 单例 |
| `0x14004FEB0` | `g_MainFrame_instance` | dword | MainFrame 单例 |
| `0x1400519B0` | `g_TabsList` | xmmword | Tabs vector 头 |
| `0x14004FE8C` | `g_workdir_instance_id` | dword | 当前实例 ID |
| `0x140050BF8` | `g_workdir_hash_cache` | dword | hash 缓存 |
| `0x140051110` | `g_main_title_buf` | byte[] | Tab 标题缓冲区 |
| `0x14004E120` | `g_mainframe_class_name` | char[] | 完整主窗口类名 |
| `0x1400515A0` | `g_workdir_buf` | char[] | 当前工作目录字符串 |

## 4. 关键技术点

### 4.1 多目录共存（防类名冲突）

```
窗口类名 = "MHXYMainFrame" + MD5_HEX(GetCurrentDirectoryA())
```

不同安装目录的 mhtab.exe 类名不同，互不干扰。

### 4.2 8 实例上限（防超额多开）

```c
for (int i = 0; i < 8; ++i) {
    sprintf(name, "mhxy%d", i);
    if (OpenMutexA(name)) ++count;
}
```

### 4.3 强制窗口置顶（绕过 SetForegroundWindow 限制）

`Dispatch_BringToFront_msg4103` 中：

```c
ForeWnd = GetForegroundWindow();
ForeTid = GetWindowThreadProcessId(ForeWnd, NULL);
MyTid   = GetCurrentThreadId();
AttachThreadInput(ForeTid, MyTid, TRUE);    // ← 关键
SetWindowPos(hwnd, HWND_TOPMOST, ...);
SetForegroundWindow(hwnd);                   // 现在可以成功
SetFocus(hwnd); SetActiveWindow(hwnd);
AttachThreadInput(ForeTid, MyTid, FALSE);
```

### 4.4 子进程退出 → 自动连锁退出

`TabCtrlApp_WaitForCmd` 中所有 view 都退出后：

```c
if (--mainframe[265] <= 0) {
    MainFrame_Cleanup(mainframe);
    PostQuitMessage(0);
    exit(0);
}
```

### 4.5 LOG stub

`Log_Stub`（`0x14000AFE0`）实际是空函数 `return 0`，所有日志在 release 编译已被 NOP 化。但通过其残留的 format string 字符串仍能反推函数命名。

## 5. 验收清单

- [x] 入口、主窗口注册、消息循环已命名 + 中文注释
- [x] `MHXYMainFrame` 主框架核心函数 ≥ 10 个已可读
- [x] `MHXYWinMgr` 子窗口管理（13 个 Dispatch_*）已全部命名
- [x] 关键全局变量（实例 ID、视图列表、配置缓冲）已命名
- [x] IDB 已保存
- [x] 输出 `ALIGNMENT.md` / `DESIGN.md` / `FINAL.md`

## 6. 输出文件

| 路径 | 用途 |
|---|---|
| `docs/mhtab_reverse/ALIGNMENT_mhtab_reverse.md` | 任务对齐与边界 |
| `docs/mhtab_reverse/DESIGN_mhtab_reverse.md` | 架构设计与流程图（mermaid） |
| `docs/mhtab_reverse/FINAL_mhtab_reverse.md` | 本文件——最终交付报告 |
| `docs/mhtab_reverse/TODO_mhtab_reverse.md` | 后续可继续工作 |
| `mhtab.exe.i64` | IDA 数据库（含全部命名与注释） |

## 7. 量化指标

| 指标 | 数值 |
|---|---|
| 函数总数 | 1140 |
| 命名前已识别 | 26 |
| 本次新增命名 | **60+** |
| 添加中文注释 | **22** 处 |
| 全局变量重命名 | **8** 个 |
| 已绘制流程图 | 3 个（mermaid） |

## 8. 局限与已知不足

1. **`MainFrame_WndProc` 内部 216 case 未逐项展开**，仅识别整体职责（消息分发到 13 个 Dispatch_*）。如需具体某 WM_ID 的处理，可再针对性深入。
2. **绘图细节函数（`Skin_DrawBitmapEx`、UI bitmap blending）** 未做完整像素流程拆解。
3. 仍有 ~470 个 `sub_*` 函数未命名，多为 STL/CRT 内部、链表算法、CRT 异常处理框架，业务价值低。
4. **`my.exe` 子进程未分析**——子进程协议是双向的，此次仅看了主进程一侧。如要还原完整通信，需同等还原 my.exe。
