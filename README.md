# mhtab

多进程 Tab 容器程序 `mhtab.exe` 的逆向分析与开源复刻。

[![Build mhtabx](https://github.com/futzhj/mhtab/actions/workflows/build.yml/badge.svg)](https://github.com/futzhj/mhtab/actions/workflows/build.yml)

## 仓库内容

```
mhtab/                          # 仓库根
├── reverse/                    # 逆向分析结果
│   ├── docs/                   # 架构文档、流程图、消息协议
│   │   └── mhtab_reverse/      # 6A 工作流产物（ALIGNMENT/DESIGN/FINAL 等）
│   └── src/                    # IDA Hex-Rays 伪代码（仅供研究）
│       ├── include/            # 类型、全局变量声明
│       ├── main.c              # WinMain
│       ├── TabCtrlApp.cpp      # 应用主类
│       ├── Dispatch.cpp        # 13 个消息处理器
│       ├── MainFrame.cpp       # 主窗口过程骨架
│       ├── View.cpp            # View 激活/停用
│       ├── TabsList.cpp        # 槽位管理
│       ├── RookieTip.cpp       # 新手提示
│       └── Util.c              # 工具函数
│
├── rebuild/                    # 开源复刻实现（可编译）
│   ├── include/                # C++17 头文件
│   ├── src/                    # 主程序实现
│   ├── child/                  # 示例可嵌入子进程
│   └── docs/mhtabx/            # 复刻工程文档
│
└── .github/workflows/          # CI 构建
    └── build.yml               # 在 Windows 2022 上自动构建
```

## 两个子项目

### 1. `reverse/` - 逆向分析（仅文档和伪代码）

- **不包含** 原 `mhtab.exe` 二进制（避免版权风险）
- **不包含** IDA 数据库文件（`.i64` / `.id*` / `.til`）
- **包含** 从 IDA Hex-Rays 导出的伪代码（经过函数/变量重命名 + 中文业务注释）
- **包含** 完整的架构设计文档，含 mermaid 流程图
- **用途**：学习 Win32 多进程架构；作为 `rebuild/` 的蓝图参考

### 2. `rebuild/` - 开源复刻 (mhtabx)

- **技术栈**：C++17 + Win32 API + CMake 3.20
- **依赖**：无第三方（仅系统库 user32/gdi32/comctl32/advapi32/uxtheme）
- **构建**：`cmake -B build -A x64 && cmake --build build --config Release`
- **CI**：推送后自动在 GitHub Actions 上构建 Debug + Release，产物可下载
- **发布**：打 `v*` tag 自动创建 GitHub Release

## 快速构建

### 本地构建

```powershell
cd rebuild
cmake -B build -A x64 -G "Visual Studio 17 2022"
cmake --build build --config Release --parallel
.\build\bin\Release\mhtabx.exe
```

### 使用 CI 产物

1. 进入 [Actions 页面](https://github.com/futzhj/mhtab/actions)
2. 选择最新成功的 `Build mhtabx` workflow
3. 下载 `mhtabx-Release-x64` artifact

## 进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| W1 | CMake 骨架 + 单实例 + 空主窗口 + CI | ✅ 完成 |
| W2 | Tab 控件 + 子进程启动 + SetParent 嵌入 | 🚧 进行中 |
| W3 | 13 种 IPC 消息 + 心跳 + reload 机制 | ⏳ 计划中 |
| W4 | Tab 拖拽 + 跨主框架 + 自绘皮肤 | ⏳ 计划中 |

## License

本仓库中 `rebuild/` 的所有代码采用 **MIT License** 发布，详见 [LICENSE](./LICENSE)。

`reverse/` 中的伪代码源自 IDA Hex-Rays 反编译，仅用于**学术研究**；原 `mhtab.exe` 的版权归原作者所有。

## 相关

- 原程序：梦幻西游多开管理器
- 还原工具：[IDA Pro](https://hex-rays.com/ida-pro/) + Hex-Rays 反编译器
- 参考资料：`reverse/docs/mhtab_reverse/` 下的 6A 工作流文档
