# ACCEPTANCE — mhtab.exe 代码还原验收

## 验收时间
本次执行：通过 IDA Pro MCP 对 `mhtab.exe` 完成业务核心代码还原

## 任务完成状态

| 任务 | 状态 | 验证 |
|---|---|---|
| 建立任务目录与对齐文档 | ✅ 完成 | `ALIGNMENT_mhtab_reverse.md` 已生成 |
| 定位 WinMain 与窗口类注册点 | ✅ 完成 | `WinMain @ 0x140017340` + 类名 `MHXYMainFrame+UID` |
| 还原主消息循环 + 主 WndProc | ✅ 完成 | `MainFrame_WndProc @ 0x14000AFF0` (4041 字节) |
| 还原 MHXYWinMgr Tab 管理核心 | ✅ 完成 | 13 个 Dispatch_* + TabsList + ViewContainer 已命名 |
| 还原 RookieTipWindow | ✅ 完成 | `RookieTipWindow_Init` + WndProc + `loadState` |
| 还原 INI / Hash / 定时器 | ✅ 完成 | `get_ini`、`GetUniqueIdSuffix`(MD5)、`Calc_30s_Bucket` |
| 批量重命名高频函数 + 全局变量 | ✅ 完成 | 60+ 函数 + 8 全局变量已重命名 |
| 添加中文注释 + 导出 FINAL 报告 | ✅ 完成 | 22 处中文注释 + `FINAL_mhtab_reverse.md` |

## 质量门控

### 代码质量
- [x] 命名规范一致（`MHXYWinMgr_xxx`/`Dispatch_xxx_msgN`/`View_xxx`/`TabCtrlApp_xxx`）
- [x] 中文注释清晰，对关键函数说明用途与算法
- [x] 通过 string xref 与 API 调用模式交叉验证命名准确性
- [x] IDB 已保存到磁盘

### 文档质量
- [x] 对齐文档（ALIGNMENT）说明任务边界
- [x] 设计文档（DESIGN）含 mermaid 流程图、数据结构表、消息映射表
- [x] 最终文档（FINAL）含函数清单、关键技术点、量化指标
- [x] TODO 文档列出未覆盖项与后续工作建议

### 业务理解
- [x] 程序定性：梦幻西游多开 Tab 管理器
- [x] 主进程与 my.exe 子进程的关系已厘清
- [x] 8 实例上限机制已识别（mhxy0~mhxy7）
- [x] 防多开冲突机制（MD5 类名后缀）已识别
- [x] 强制置顶技巧（AttachThreadInput）已识别

## 关键证据汇总

### 字符串证据
```
"MHXYMainFrame"              主窗口类名前缀
"MHXYWinMgr"                 子窗口类名前缀
"mhxy%d"                     互斥锁命名模板
"TabCtrlApp::viewNew : start my.exe"   主类名 + 子进程命名
"TabCtrlApp::waitforCmd"     子进程监控
"Dispacth::newClient"        消息分发（原文拼写错误）
"RookieTipWindow::init"      新手提示
"RookieGuide::loadState"     新手引导持久化
"TabBarPlus::doDragging"     Tab 拖拽
"get_ini, file = %s, session = %s..."  配置读取
```

### 函数命名前后对比示例

| 原 | 现 |
|---|---|
| `sub_140017340` | `WinMain` |
| `sub_140014E50` | `TabCtrlApp_Init` |
| `sub_14000AFF0` | `MainFrame_WndProc` |
| `sub_1400165F0` | `TabCtrlApp_ViewNew` |
| `sub_140016BE0` | `TabCtrlApp_WaitForCmd` |
| `sub_1400146C0` | `GetUniqueIdSuffix` (MD5) |
| `sub_140014B40` | `get_ini` |
| `sub_140018390` | `GetActiveTabCount` |
| `sub_140012EE0` | `Dispatch_BringToFront_msg4103` |

## 量化结果

```
函数总数：       1140
本次新命名：     60+
本次注释：       22 处中文
全局变量命名：   8 个
mermaid 流程图： 3 个
输出文档：       4 份 (ALIGNMENT/DESIGN/FINAL/TODO)
IDB 持久化：     已保存到 mhtab.exe.i64
```

## 验收结论

**核心业务流程已可读：从 WinMain → TabCtrlApp 初始化 → 主框架 WndProc → 13 个 Dispatch 消息处理器 → 子进程拉起/监控/退出 → 强制置顶/拖拽/INI 配置等关键路径均已命名并加注释。**

剩余约 470 个未命名函数主要为 STL/CRT 内部实现，**业务价值低**，已在 TODO 中说明可继续方向。
