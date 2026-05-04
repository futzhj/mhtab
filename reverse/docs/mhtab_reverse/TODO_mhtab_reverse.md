# TODO — mhtab.exe 后续可继续工作

## 1. 高价值待办

### 1.1 深入分析 my.exe 子进程
- **位置**：与 `mhtab.exe` 同目录，命令行格式：
  ```
  my.exe %s %s %s%d %d %d %d %d %08d
  ```
- **价值**：还原子进程发送 msg1/msg3/msg6 的具体协议字段含义，建立完整的双向通信文档
- **建议**：用同样的 IDA MCP 流程，从 `WinMain` → 注册回主进程的 `SendMessageA` 调用入手

### 1.2 拆解 MainFrame_WndProc 大 switch
- **位置**：`0x14000AFF0`，4041 字节，216 个 case
- **方法**：
  ```python
  # IDA Python 提取 jump table
  for case_addr in jump_table:
      msg_id = (case_addr - jt_base) // 4
      print(hex(msg_id), get_func_name(case_addr))
  ```
- **关注**：`WM_USER` 范围（0x0400~）的自定义消息映射

### 1.3 还原 Skin_DrawBitmapEx 像素管线
- **位置**：`0x14000D150`（0x9E0 字节）
- **API 序列**：BitBlt + StretchBlt + AlphaBlend + TransparentBlt + GetDIBits
- **常量**：`0xCC0020` (SRCCOPY)、`0xFFFFFF` (TRANSPARENT 色键)
- **价值**：定位 Tab 头/按钮的素材文件读取与渲染逻辑

## 2. 待澄清的不确定点

### 2.1 `Sub_GetSomeSingletonA` / `Sub_GetSomeSingletonB`
- **地址**：`0x140015260`、`0x1400151A0`
- **现状**：是单例 getter，但未识别其具体类型（可能是事件分发器/快捷键管理器）
- **建议**：通过其 vtable 第一个虚函数的字符串引用反推

### 2.2 `Calc_30s_Bucket` 真实用途
- **地址**：`0x140014890`
- **公式**：`x / 30000`（除以 30 秒）
- **猜测**：节流/限流，避免短时间内频繁切换 Tab，但具体策略未明确

### 2.3 `0x1304` 自定义消息含义
- **现象**：多处 `SendMessageA(child_hwnd, 0x1304, 0, 0)` 用于检测子进程是否存活
- **猜测**：是与 `my.exe` 约定的"心跳/存活"自定义消息号
- **验证**：在 my.exe 中查找 WM_USER+0x304 的处理

## 3. 配置 / 缺少的支持

### 3.1 INI 配置文件
- **位置**：`{程序当前目录}\xxx.ini`（具体文件名未确定，需运行时观测或在 `my_sscanf` 调用现场查看）
- **建议**：把 mhtab.exe 单独运行一次，用 ProcMon / API Monitor 抓 `GetPrivateProfileStringA(lpFileName=?)`

### 3.2 my.exe 是否在配套目录？
- 任务目录 `E:\jinyiNew\mhtab\` 中**未发现 my.exe**，仅有 mhtab.exe + IDB
- 如需完整还原通信链，需要：
  - 找到对应版本的 my.exe（同目录或同发布包）
  - 或对照梦幻西游目录里的可执行文件

### 3.3 资源 / 皮肤文件
- 程序使用 `LoadBitmapA`、`CreateDIBSection` 等加载位图素材
- 还原 Tab 头皮肤时可能需要：
  - 配套的 BMP/PNG 资源文件
  - 资源 ID 表（在 `MainFrame_WndProc` 的资源加载分支处可找到）

## 4. 建议的后续工作流（如继续）

```
Step 1 — 找 my.exe + 资源目录（运行时确认或同包查找）
   ↓
Step 2 — 用 IDA Pro 打开 my.exe，建立第二个 MCP 实例
   ↓
Step 3 — 把 mhtab.exe 的 13 个 Dispatch_* 函数对应的消息号
        映射到 my.exe 中 SendMessage 调用现场，匹配 wParam/lParam 含义
   ↓
Step 4 — 拆解 MainFrame_WndProc 的 216 case，逐个标注
   ↓
Step 5 — 输出完整的协议规范文档（msg ID/方向/参数/字段含义）
```

## 5. 需要你（用户）协助确认

- [ ] 是否需要继续还原 `my.exe` 子进程？
- [ ] 是否需要逐项拆解 `MainFrame_WndProc` 的 216 case？
- [ ] 是否提供配套的 INI / 资源文件用于运行时验证？
- [ ] 是否需要对程序做行为追踪（动态分析/日志插桩）补充静态分析？

---

完成本次任务后的状态：**主框架已可读，业务流程已厘清，可作为深入逆向的基础**。
