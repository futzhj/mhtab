/**
 * mhtabx - IPC 协议定义
 *
 * 主进程 ↔ 子进程之间的消息全部走 SendMessage / PostMessage
 * （除了大块数据用 WM_COPYDATA）。所有消息号在 common.h 的 MhxMsg 枚举里。
 *
 * 本文件集中定义：
 *   - 每个消息的 wParam / lParam 语义（注释）
 *   - 复杂 payload 的结构体（用于 WM_COPYDATA）
 *   - 协议帮助函数（封装 SendMessage 调用，便于调用点检查）
 */

#pragma once
#include "common.h"

namespace mhx::ipc {

/* ============================================================
 * 消息分类（仅用于文档）
 *
 * 子→主 (Child → Host):
 *   - MHX_NEW_CLIENT      slot_id, child_hwnd       注册握手
 *   - MHX_NEW_VIEW        slot_id, hint             请求新 Tab
 *   - MHX_CLEANUP_VIEW    slot_id, 0                请求清理
 *   - MHX_READY_CONFIRM   slot_id, tick             心跳响应
 *   - MHX_UPDATE_POS      slot_id, packed_xy        位置变更
 *
 * 主→子 (Host → Child):
 *   - MHX_HEARTBEAT       slot_id, tick             心跳询问
 *   - MHX_ACTIVATE_VIEW   slot_id, 0                激活
 *   - MHX_HIDE_VIEW       slot_id, 0                隐藏
 *   - MHX_SHOW_WINDOW     slot_id, SW_*             ShowWindow
 *   - MHX_FORWARD_INPUT   wm_msg,  packed_input     键鼠转发
 *
 * 跨实例 (Instance ↔ Instance):
 *   - INST_QUERY_INSTANCE_ID   返回 instance_id
 *   - INST_QUERY_STATE         返回当前选中 slot_id
 *   - WM_COPYDATA              CmdLineForward
 * ============================================================ */

/* ============================================================
 * Payload 结构体
 * ============================================================ */

/**
 * MHX_FORWARD_INPUT 用 WM_COPYDATA 转发的键盘消息数据。
 * 鼠标消息嵌入后会自然路由，不需要转发。
 */
struct InputForwardPayload {
    u32   magic;       /* 'IFWD' = 0x44574649 */
    UINT  msg;         /* WM_KEYDOWN / WM_KEYUP / WM_CHAR */
    WPARAM wp;
    LPARAM lp;
};
constexpr u32 kInputForwardMagic = 0x44574649;  /* 'IFWD' */

/**
 * MHX_UPDATE_POS 用 lParam 打包 (x, y) 两个 int16，
 * x = LOWORD, y = HIWORD（与 Win32 WM_MOUSEMOVE 风格一致）。
 */
inline LPARAM PackXY(int x, int y) noexcept {
    return MAKELPARAM(x & 0xFFFF, y & 0xFFFF);
}
inline int UnpackX(LPARAM lp) noexcept { return (int)(SHORT)LOWORD(lp); }
inline int UnpackY(LPARAM lp) noexcept { return (int)(SHORT)HIWORD(lp); }

/* ============================================================
 * 协议封装：子→主
 *
 * 这些函数都是阻塞 SendMessage（带超时）。返回 LRESULT 为目标处理结果。
 * ============================================================ */

/**
 * 子进程登记。
 * @return 主进程 OnNewClient 的返回值（TRUE=成功嵌入）
 */
inline LRESULT SendNewClient(HWND host, int slot_id, HWND child_hwnd, UINT timeout_ms = 2000) {
    DWORD_PTR result = 0;
    LRESULT r = ::SendMessageTimeoutW(
        host, MHX_NEW_CLIENT,
        static_cast<WPARAM>(slot_id),
        reinterpret_cast<LPARAM>(child_hwnd),
        SMTO_ABORTIFHUNG, timeout_ms, &result);
    return r ? static_cast<LRESULT>(result) : 0;
}

/**
 * 子进程响应主进程心跳。tick 原样回传。
 */
inline void PostReadyConfirm(HWND host, int slot_id, ULONG tick) {
    ::PostMessageW(host, MHX_READY_CONFIRM,
                   static_cast<WPARAM>(slot_id),
                   static_cast<LPARAM>(tick));
}

/**
 * 子进程上报位置变化（被嵌入前才有意义）。
 */
inline void PostUpdatePos(HWND host, int slot_id, int x, int y) {
    ::PostMessageW(host, MHX_UPDATE_POS,
                   static_cast<WPARAM>(slot_id),
                   PackXY(x, y));
}

/**
 * 子进程主动请求清理：通常在 WM_CLOSE 之前发，让主进程优雅删除 Tab。
 */
inline void PostCleanupView(HWND host, int slot_id) {
    ::PostMessageW(host, MHX_CLEANUP_VIEW,
                   static_cast<WPARAM>(slot_id), 0);
}

/* ============================================================
 * 协议封装：主→子
 * ============================================================ */

/**
 * 主进程发心跳询问。tick 是主进程的 GetTickCount 值。
 * 子进程应在 timeout_ms 内回复 MHX_READY_CONFIRM。
 */
inline void PostHeartbeat(HWND child, int slot_id, ULONG tick) {
    ::PostMessageW(child, MHX_HEARTBEAT,
                   static_cast<WPARAM>(slot_id),
                   static_cast<LPARAM>(tick));
}

/**
 * 主进程通知子进程被激活/取消激活。
 */
inline void PostActivateView(HWND child, int slot_id) {
    ::PostMessageW(child, MHX_ACTIVATE_VIEW,
                   static_cast<WPARAM>(slot_id), 0);
}

inline void PostHideView(HWND child, int slot_id) {
    ::PostMessageW(child, MHX_HIDE_VIEW,
                   static_cast<WPARAM>(slot_id), 0);
}

inline void PostShowWindow(HWND child, int slot_id, int show_cmd) {
    ::PostMessageW(child, MHX_SHOW_WINDOW,
                   static_cast<WPARAM>(slot_id),
                   static_cast<LPARAM>(show_cmd));
}

/**
 * 主进程把键盘消息转发到子窗口。
 *
 * 用 WM_COPYDATA 因为 wp/lp 在 32/64 位混用时大小不一致，需要稳定包装。
 * 同时给消息号 MHX_FORWARD_INPUT 留一个识别 magic。
 */
inline LRESULT SendForwardInput(HWND child, UINT msg, WPARAM wp, LPARAM lp,
                                UINT timeout_ms = 200) {
    InputForwardPayload p = {};
    p.magic = kInputForwardMagic;
    p.msg   = msg;
    p.wp    = wp;
    p.lp    = lp;

    COPYDATASTRUCT cds = {};
    cds.dwData = MHX_FORWARD_INPUT;
    cds.cbData = sizeof(p);
    cds.lpData = &p;

    DWORD_PTR result = 0;
    LRESULT r = ::SendMessageTimeoutW(
        child, WM_COPYDATA, 0,
        reinterpret_cast<LPARAM>(&cds),
        SMTO_ABORTIFHUNG, timeout_ms, &result);
    return r ? static_cast<LRESULT>(result) : 0;
}

/**
 * 解码 InputForwardPayload。子进程收到 WM_COPYDATA 时调用：
 *   if (cds->dwData == MHX_FORWARD_INPUT)
 *       if (auto p = ipc::DecodeInputForward(cds))
 *           handle(p->msg, p->wp, p->lp);
 *
 * 失败返回 nullptr。
 */
inline const InputForwardPayload* DecodeInputForward(const COPYDATASTRUCT* cds) noexcept {
    if (!cds || cds->dwData != MHX_FORWARD_INPUT) return nullptr;
    if (cds->cbData < sizeof(InputForwardPayload)) return nullptr;
    auto* p = static_cast<const InputForwardPayload*>(cds->lpData);
    if (!p || p->magic != kInputForwardMagic) return nullptr;
    return p;
}

} /* namespace mhx::ipc */
