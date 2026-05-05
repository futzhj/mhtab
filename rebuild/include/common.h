/**
 * mhtabx - 公共类型 / 常量 / 日志
 *
 * 所有 .cpp 文件第一个 #include 就是这个头。
 */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>   /* GET_X_LPARAM / GET_Y_LPARAM */
#include <commctrl.h>
#include <tchar.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>

namespace mhx {

/* ============================================================
 * 基础类型别名
 * ============================================================ */
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using String     = std::wstring;
using StringView = std::wstring_view;
using AString    = std::string;

/* ============================================================
 * 项目常量
 * ============================================================ */
constexpr int  kMaxChildren         = 8;       /* 单主框架内最多子进程数 */
constexpr UINT kTimerCheckChildren  = 4001;    /* WM_TIMER id：检查子进程 */
constexpr UINT kTimerRecycleSlot    = 4002;    /* WM_TIMER id：延迟重建 slot */
constexpr UINT kHeartbeatTimeoutMs  = 3000;    /* 心跳超时 */
constexpr UINT kChildStartupTimeoutMs = 10000; /* 子进程注册超时 */

/* 窗口类名前缀（后缀为 workdir 的 MD5 hex） */
constexpr const wchar_t* kMutexNamePrefix      = L"Global\\mhtabx_";
constexpr const wchar_t* kMainFrameClassPrefix = L"mhtabx_MainFrame_";

/* === 命令行参数 === */
/* 仅出现在 detach 后启动的新 mhtabx 进程命令行中。
 * 主程序识别后绕过单实例转发，启动新窗口并 AdoptExternalWindow 接管 child。
 *
 * 用法：mhtabx.exe --mhx-adopt-hwnd 0xHHHHHHHH
 */
constexpr const wchar_t* kArgAdoptHwnd = L"--mhx-adopt-hwnd";

/* 可选：为 detach 出的新实例指定屏幕坐标窗口左上角位置。
 * 格式：--mhx-spawn-at X,Y  （例如 --mhx-spawn-at 800,450）
 * 用于让 detach 出的新 mhtabx 窗口在用户拖拽松手的位置出现，
 * 而不是 Win32 CW_USEDEFAULT 的系统默认位置。 */
constexpr const wchar_t* kArgSpawnAt   = L"--mhx-spawn-at";

/* ============================================================
 * 错误处理
 * ============================================================ */
#define MHX_LAST_ERROR()  ::GetLastError()

/* RAII 包装 HANDLE */
struct HandleDeleter {
    void operator()(HANDLE h) const noexcept {
        if (h && h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

/* ============================================================
 * 日志
 *
 * Debug 编译：OutputDebugStringW + printf 到控制台
 * Release 编译：仅 OutputDebugStringW
 * ============================================================ */
enum class LogLevel { Trace, Info, Warn, Error };

void LogImpl(LogLevel level, const wchar_t* file, int line, const wchar_t* fmt, ...);

#define MHX_LOG_TRACE(fmt, ...) ::mhx::LogImpl(::mhx::LogLevel::Trace, _CRT_WIDE(__FILE__), __LINE__, fmt, ##__VA_ARGS__)
#define MHX_LOG_INFO(fmt, ...)  ::mhx::LogImpl(::mhx::LogLevel::Info,  _CRT_WIDE(__FILE__), __LINE__, fmt, ##__VA_ARGS__)
#define MHX_LOG_WARN(fmt, ...)  ::mhx::LogImpl(::mhx::LogLevel::Warn,  _CRT_WIDE(__FILE__), __LINE__, fmt, ##__VA_ARGS__)
#define MHX_LOG_ERROR(fmt, ...) ::mhx::LogImpl(::mhx::LogLevel::Error, _CRT_WIDE(__FILE__), __LINE__, fmt, ##__VA_ARGS__)

/* ============================================================
 * 自定义消息号（基于 WM_APP，避免与系统消息冲突）
 *
 * 见 docs/mhtabx/DESIGN.md "Layer 4" 一节。
 * ============================================================ */
enum MhxMsg : UINT {
    MHX_NEW_CLIENT      = WM_APP + 1,
    MHX_READY_CONFIRM   = WM_APP + 3,
    MHX_FORWARD_INPUT   = WM_APP + 4,
    MHX_UPDATE_POS      = WM_APP + 6,
    MHX_ACTIVATE_VIEW   = WM_APP + 7,
    MHX_HIDE_VIEW       = WM_APP + 8,
    MHX_NEW_VIEW        = WM_APP + 16,
    MHX_GET_BY_PARENT   = WM_APP + 17,
    MHX_CLEANUP_VIEW    = WM_APP + 25,
    MHX_SHOW_WINDOW     = WM_APP + 26,
    MHX_SET_TAB_ICON    = WM_APP + 27,    /* W6-2: 子→主，wp=slot_id, lp=HICON */
    MHX_REMBED_REQUEST  = WM_APP + 28,    /* W6-bugfix: 子→主。wp=child_hwnd, lp=0, 返回 slot_id 或 -1 */
    MHX_RELEASE_CHILD   = WM_APP + 29,    /* 主→主广播：wp=child_hwnd, lp=0。若本实例持有该 child 则移除并触发 CheckAutoExit。
                                           * 用于修复多实例下合并回主窗口后，前 owner 不知情导致 slot 遗留的 bug。 */
    MHX_ACTIVATE        = WM_APP + 100,
    MHX_BRING_TO_FRONT  = WM_APP + 101,
    MHX_FIND_BY_HWND    = WM_APP + 102,
    MHX_HEARTBEAT       = WM_APP + 200,
};

/* 跨实例 IPC（同目录不同启动） */
enum InstanceMsg : UINT {
    INST_QUERY_INSTANCE_ID = WM_APP + 500,
    INST_QUERY_STATE       = WM_APP + 501,
    INST_FORWARD_CMDLINE   = WM_COPYDATA,
};

/* WM_COPYDATA 协议 */
struct CmdLineForward {
    u32     magic;              /* 'MHXF' */
    u32     flags;
    wchar_t cmdline[1024];
};
constexpr u32 kCmdLineForwardMagic = 0x4658484D;  /* 'MHXF' */

/* ============================================================
 * 子进程状态机
 * ============================================================ */
enum class ChildState : int {
    Starting,   /* 已 CreateProcess，等待 NEW_CLIENT 注册 */
    Running,    /* 注册完成，交互中 */
    Closing,    /* 收到关闭请求，等待进程退出 */
    Dead,       /* 进程已退出，slot 可回收 */
};

constexpr const wchar_t* ToString(ChildState s) noexcept {
    switch (s) {
        case ChildState::Starting: return L"Starting";
        case ChildState::Running:  return L"Running";
        case ChildState::Closing:  return L"Closing";
        case ChildState::Dead:     return L"Dead";
    }
    return L"?";
}

} /* namespace mhx */
