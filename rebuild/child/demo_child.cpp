/**
 * demo_child.exe - W3 完整实现 IPC 协议的示例可嵌入子进程
 *
 * 启动流程：
 *   1. 解析命令行：--mhx-parent / --mhx-slot / --mhx-instance
 *   2. 创建本地主窗口（嵌入模式：WS_POPUP）
 *   3. SendMessage(MHX_NEW_CLIENT) 握手
 *   4. 进入消息循环
 *
 * IPC 消息处理（来自 host）：
 *   - MHX_HEARTBEAT       → PostMessage(MHX_READY_CONFIRM) 回应
 *   - MHX_ACTIVATE_VIEW   → 切换到 active 状态（背景变亮）
 *   - MHX_HIDE_VIEW       → 切换到 inactive 状态（背景变暗）
 *   - MHX_SHOW_WINDOW     → ShowWindow(SW_*)
 *   - MHX_FORWARD_INPUT   → 转发为本地按键事件
 *
 * 退出条件：
 *   - 窗口被销毁（host 关闭/拖拽出/进程外终止）
 *   - 用户按 ESC（也会通知 host 清理）
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace {

/* ============================================================
 * 与 mhtabx/include/common.h 对齐的协议号
 *
 * 故意复制而非 #include，让 demo_child 可作为独立示例。
 * ============================================================ */
constexpr UINT MHX_NEW_CLIENT     = WM_APP + 1;
constexpr UINT MHX_READY_CONFIRM  = WM_APP + 3;
constexpr UINT MHX_FORWARD_INPUT  = WM_APP + 4;
constexpr UINT MHX_UPDATE_POS     = WM_APP + 6;
constexpr UINT MHX_ACTIVATE_VIEW  = WM_APP + 7;
constexpr UINT MHX_HIDE_VIEW      = WM_APP + 8;
constexpr UINT MHX_NEW_VIEW       = WM_APP + 16;
constexpr UINT MHX_CLEANUP_VIEW   = WM_APP + 25;
constexpr UINT MHX_SHOW_WINDOW    = WM_APP + 26;
constexpr UINT MHX_SET_TAB_ICON   = WM_APP + 27;   /* W6-2 */
constexpr UINT MHX_REMBED_REQUEST = WM_APP + 28;   /* W6-bugfix: reembed 请求 */
constexpr UINT MHX_HEARTBEAT      = WM_APP + 200;

/* MHX_FORWARD_INPUT payload (与 IpcProtocol.h InputForwardPayload 对齐) */
struct InputForwardPayload {
    UINT32 magic;         /* 'IFWD' = 0x44574649 */
    UINT   msg;
    WPARAM wp;
    LPARAM lp;
};
constexpr UINT32 kInputForwardMagic = 0x44574649;

constexpr const wchar_t* kClassName     = L"MhtabxDemoChild";
constexpr const wchar_t* kArgParentHwnd = L"--mhx-parent";
constexpr const wchar_t* kArgSlotId     = L"--mhx-slot";
constexpr const wchar_t* kArgInstanceId = L"--mhx-instance";

/* ============================================================
 * 子进程状态（通过 GWLP_USERDATA 关联到 HWND）
 * ============================================================ */
struct ChildState {
    HWND host_hwnd   = nullptr;
    int  slot_id     = -1;
    UINT instance_id = 0;
    bool is_active   = true;     /* 当前是否被 ACTIVATE_VIEW */
    int  key_count   = 0;        /* 收到的 FORWARD_INPUT 计数（用于显示） */
};

/* ============================================================
 * 命令行解析
 * ============================================================ */
ChildState ParseArgs() {
    ChildState s;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return s;

    for (int i = 1; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], kArgParentHwnd) == 0) {
            s.host_hwnd = reinterpret_cast<HWND>(_wcstoui64(argv[i + 1], nullptr, 0));
        } else if (wcscmp(argv[i], kArgSlotId) == 0) {
            s.slot_id = (int)wcstol(argv[i + 1], nullptr, 0);
        } else if (wcscmp(argv[i], kArgInstanceId) == 0) {
            s.instance_id = (UINT)wcstoul(argv[i + 1], nullptr, 0);
        }
    }
    ::LocalFree(argv);
    return s;
}

/* ============================================================
 * Helper
 * ============================================================ */
ChildState* GetState(HWND hwnd) {
    return reinterpret_cast<ChildState*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

/* 输出 WM_KEYDOWN/UP/CHAR 的可读名称 */
const wchar_t* MsgName(UINT msg) {
    switch (msg) {
        case WM_KEYDOWN:    return L"WM_KEYDOWN";
        case WM_KEYUP:      return L"WM_KEYUP";
        case WM_CHAR:       return L"WM_CHAR";
        case WM_SYSKEYDOWN: return L"WM_SYSKEYDOWN";
        case WM_SYSKEYUP:   return L"WM_SYSKEYUP";
        default:            return L"?";
    }
}

void DbgLog(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list va;
    va_start(va, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, va);
    va_end(va);
    ::OutputDebugStringW(buf);
}

/* ============================================================
 * 绘制
 * ============================================================ */
void PaintBody(HWND hwnd, HDC hdc, ChildState* st) {
    RECT rc; ::GetClientRect(hwnd, &rc);

    DWORD pid = ::GetCurrentProcessId();
    BYTE r = (BYTE)((pid * 73) | 0x60);
    BYTE g = (BYTE)((pid * 37) | 0x60);
    BYTE b = (BYTE)((pid * 29) | 0x60);

    /* active 时背景亮，inactive 时暗 50% */
    if (!st->is_active) {
        r = (BYTE)(r / 2);
        g = (BYTE)(g / 2);
        b = (BYTE)(b / 2);
    }

    HBRUSH br = ::CreateSolidBrush(RGB(r, g, b));
    ::FillRect(hdc, &rc, br);
    ::DeleteObject(br);

    wchar_t buf[512];
    _snwprintf_s(buf, _TRUNCATE,
                 L"demo_child  [%s]\n\n"
                 L"PID    = %lu\n"
                 L"HWND   = %p\n"
                 L"slot   = %d\n"
                 L"keys   = %d\n\n"
                 L"ESC: 通知 host 清理本 Tab\n"
                 L"F6 : 合并回 mhtabx 主窗口",
                 st->is_active ? L"active" : L"inactive",
                 pid, hwnd, st->slot_id, st->key_count);

    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, st->is_active ? RGB(0, 0, 0) : RGB(200, 200, 200));
    ::DrawTextW(hdc, buf, -1, &rc,
                DT_CENTER | DT_VCENTER | DT_NOPREFIX);
}

/* ============================================================
 * IPC 处理
 * ============================================================ */
void HandleHeartbeat(HWND hwnd, ChildState* st, ULONG tick) {
    if (st->host_hwnd && ::IsWindow(st->host_hwnd)) {
        ::PostMessageW(st->host_hwnd, MHX_READY_CONFIRM,
                       static_cast<WPARAM>(st->slot_id),
                       static_cast<LPARAM>(tick));
    }
}

void HandleActivate(HWND hwnd, ChildState* st, bool active) {
    st->is_active = active;
    ::InvalidateRect(hwnd, nullptr, TRUE);
    DbgLog(L"[demo_child] slot=%d %s\n", st->slot_id,
           active ? L"ACTIVATE" : L"HIDE");
}

void HandleForwardInput(HWND hwnd, ChildState* st, const COPYDATASTRUCT* cds) {
    if (!cds || cds->dwData != MHX_FORWARD_INPUT) return;
    if (cds->cbData < sizeof(InputForwardPayload)) return;
    auto* p = static_cast<const InputForwardPayload*>(cds->lpData);
    if (!p || p->magic != kInputForwardMagic) return;

    ++st->key_count;
    DbgLog(L"[demo_child] FORWARD_INPUT: %s wp=0x%llX lp=0x%llX (count=%d)\n",
           MsgName(p->msg),
           (long long unsigned)p->wp, (long long unsigned)p->lp,
           st->key_count);

    /* 把消息派发到本地 - 同样会经过 ChildWndProc */
    ::SendMessageW(hwnd, p->msg, p->wp, p->lp);

    ::InvalidateRect(hwnd, nullptr, TRUE);
}

/* ============================================================
 * 主窗口过程
 * ============================================================ */
LRESULT CALLBACK ChildWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ChildState* st = GetState(hwnd);

    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = ::BeginPaint(hwnd, &ps);
            if (st) PaintBody(hwnd, hdc, st);
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                /* 优先通知 host 让它清理 Tab，然后再关闭自己 */
                if (st && st->host_hwnd && ::IsWindow(st->host_hwnd)) {
                    ::PostMessageW(st->host_hwnd, MHX_CLEANUP_VIEW,
                                   static_cast<WPARAM>(st->slot_id), 0);
                }
                ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            /* W6-bugfix + 多实例修复: F6 请求合并回最初主窗口
             *
             * 语义: 无论我当前被哪个 mhtabx 接管，按 F6 都"回家"到启动时命令行
             * 指定的原始 host_hwnd。原 host 的 OnRembedRequest 会 BroadcastReleaseChild
             * 通知当前 owner 放手，所以 demo_child 这边不需要知道当前 owner 是谁。
             *
             * 关键保护: 若 GetAncestor(hwnd, GA_ROOT) 已经 == 原 host，说明 child
             * 已经在"家"，再发 REMBED_REQUEST 会在原 host 里重复 Adopt 同一个 hwnd
             * (旧代码无限合并创建窗口的根因)，直接跳过。 */
            if (wp == VK_F6) {
                HWND target = st ? st->host_hwnd : nullptr;
                if (!target || !::IsWindow(target)) return 0;

                HWND current_host = ::GetAncestor(hwnd, GA_ROOT);
                if (current_host == target) {
                    DbgLog(L"[demo_child] F6 ignored: already under original host %p\n",
                           target);
                    return 0;
                }

                DWORD_PTR result = 0;
                LRESULT r = ::SendMessageTimeoutW(
                    target, MHX_REMBED_REQUEST,
                    reinterpret_cast<WPARAM>(hwnd), 0,
                    SMTO_ABORTIFHUNG, 3000, &result);
                if (r && static_cast<intptr_t>(result) >= 0) {
                    /* target 返回新 slot_id，更新本地状态 */
                    st->slot_id = static_cast<int>(static_cast<intptr_t>(result));
                    DbgLog(L"[demo_child] reembed OK: new slot=%d\n", st->slot_id);
                } else {
                    DbgLog(L"[demo_child] reembed failed: result=%lld\n",
                           (long long)result);
                }
                return 0;
            }
            break;

        case WM_CLOSE:
            ::DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        case WM_ERASEBKGND:
            return 1;   /* 让 WM_PAINT 全量绘 */

        /* === IPC 消息处理 === */
        case MHX_HEARTBEAT:
            if (st) HandleHeartbeat(hwnd, st, static_cast<ULONG>(lp));
            return 0;

        case MHX_ACTIVATE_VIEW:
            if (st) HandleActivate(hwnd, st, true);
            return 0;

        case MHX_HIDE_VIEW:
            if (st) HandleActivate(hwnd, st, false);
            return 0;

        case MHX_SHOW_WINDOW:
            ::ShowWindow(hwnd, static_cast<int>(lp));
            return 0;

        case WM_COPYDATA: {
            auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lp);
            if (st) HandleForwardInput(hwnd, st, cds);
            return TRUE;
        }
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

/* ============================================================
 * 创建窗口
 * ============================================================ */
HWND CreateChildWindow(HINSTANCE hInst, ChildState* st, bool embedded) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = ChildWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc);

    wchar_t title[64];
    _snwprintf_s(title, _TRUNCATE, L"demo_child [PID=%lu]",
                 ::GetCurrentProcessId());

    DWORD style = embedded ? WS_POPUP : WS_OVERLAPPEDWINDOW;

    return ::CreateWindowExW(
        0, kClassName, title, style,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        nullptr, nullptr, hInst, st);
}

/* ============================================================
 * W6-2: 上报 Tab 图标
 *
 * 用 PID 哈希到一个系统图标（IDI_INFORMATION/QUESTION/...），
 * 不同子进程显示不同图标，便于视觉区分。
 *
 * IDI_* 系列是 user32 的系统共享资源，不需要 DestroyIcon。
 * 主进程会立即 CopyIcon 一份私有副本，所以这里用完即可丢弃引用。
 * ============================================================ */
void SendIcon(HWND host, int slot_id) {
    if (!host || !::IsWindow(host)) return;

    static LPCWSTR kIcons[] = {
        IDI_INFORMATION, IDI_QUESTION, IDI_EXCLAMATION,
        IDI_WARNING,     IDI_ASTERISK, IDI_APPLICATION,
    };
    DWORD pid = ::GetCurrentProcessId();
    LPCWSTR pick = kIcons[pid % _countof(kIcons)];

    HICON icon = ::LoadIconW(nullptr, pick);
    if (!icon) {
        DbgLog(L"[demo_child] LoadIcon failed\n");
        return;
    }

    DWORD_PTR result = 0;
    ::SendMessageTimeoutW(
        host, MHX_SET_TAB_ICON,
        static_cast<WPARAM>(slot_id),
        reinterpret_cast<LPARAM>(icon),
        SMTO_ABORTIFHUNG, 1000, &result);

    DbgLog(L"[demo_child] SendIcon: slot=%d icon=%p\n", slot_id, icon);
}

/* ============================================================
 * 握手
 * ============================================================ */
void SendHandshake(HWND host, int slot_id, HWND child_hwnd) {
    if (!host || !::IsWindow(host)) {
        DbgLog(L"[demo_child] host_hwnd invalid\n");
        return;
    }

    DWORD_PTR result = 0;
    LRESULT r = ::SendMessageTimeoutW(
        host, MHX_NEW_CLIENT,
        static_cast<WPARAM>(slot_id),
        reinterpret_cast<LPARAM>(child_hwnd),
        SMTO_ABORTIFHUNG, 2000, &result);

    DbgLog(L"[demo_child] handshake: slot=%d host=%p child=%p result=%lld ok=%d\n",
           slot_id, host, child_hwnd, (long long)result, r != 0);
}

} /* namespace */

/* ============================================================
 * Entry
 * ============================================================ */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShowCmd) {
    static ChildState g_state = ParseArgs();
    bool embedded = (g_state.host_hwnd != nullptr) && (g_state.slot_id >= 0);

    HWND hwnd = CreateChildWindow(hInst, &g_state, embedded);
    if (!hwnd) {
        ::MessageBoxW(nullptr, L"demo_child CreateWindow 失败",
                      L"demo_child", MB_ICONERROR);
        return 1;
    }

    if (embedded) {
        /* host 在收到 NEW_CLIENT 后会 SetParent + ShowWindow，
         * 子进程不应主动 ShowWindow */
        SendHandshake(g_state.host_hwnd, g_state.slot_id, hwnd);
        /* W6-2: 握手成功后立即上报 Tab 图标 */
        SendIcon(g_state.host_hwnd, g_state.slot_id);
    } else {
        ::ShowWindow(hwnd, nShowCmd);
        ::UpdateWindow(hwnd);
    }

    MSG msg = {};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
