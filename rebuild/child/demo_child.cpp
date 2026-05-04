/**
 * demo_child.exe - 实现 mhx 握手协议的示例可嵌入子进程
 *
 * 启动流程：
 *  1. 解析命令行：--mhx-parent 0xHWND --mhx-slot N --mhx-instance 0xID
 *  2. 创建本地主窗口
 *  3. 向 host 发送 MHX_NEW_CLIENT(slot_id, child_hwnd)
 *     主进程收到后会 SetParent 把本窗口嵌入 Tab 页
 *  4. 进入消息循环
 *  5. WM_DESTROY 时退出（主进程 PostMessage(WM_CLOSE) 时触发）
 *
 * 注意：本程序也可独立运行（无 mhx 参数），便于单独调试。
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

/* === 与 mhtabx/include/common.h 对齐的协议号 === */
constexpr UINT MHX_NEW_CLIENT = WM_APP + 1;
constexpr UINT MHX_HEARTBEAT  = WM_APP + 200;

constexpr const wchar_t* kClassName       = L"MhtabxDemoChild";
constexpr const wchar_t* kArgParentHwnd   = L"--mhx-parent";
constexpr const wchar_t* kArgSlotId       = L"--mhx-slot";
constexpr const wchar_t* kArgInstanceId   = L"--mhx-instance";

struct MhxArgs {
    HWND host_hwnd   = nullptr;
    int  slot_id     = -1;
    UINT instance_id = 0;

    bool Valid() const noexcept {
        return host_hwnd && slot_id >= 0;
    }
};

/* ============================================================
 * 命令行解析
 * ============================================================ */
MhxArgs ParseArgs() {
    MhxArgs a;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return a;

    for (int i = 1; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], kArgParentHwnd) == 0) {
            a.host_hwnd = reinterpret_cast<HWND>(_wcstoui64(argv[i + 1], nullptr, 0));
        } else if (wcscmp(argv[i], kArgSlotId) == 0) {
            a.slot_id = (int)wcstol(argv[i + 1], nullptr, 0);
        } else if (wcscmp(argv[i], kArgInstanceId) == 0) {
            a.instance_id = (UINT)wcstoul(argv[i + 1], nullptr, 0);
        }
    }
    ::LocalFree(argv);
    return a;
}

/* ============================================================
 * 窗口过程
 * ============================================================ */
LRESULT CALLBACK ChildWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = ::BeginPaint(hwnd, &ps);
            RECT rc; ::GetClientRect(hwnd, &rc);

            DWORD pid = ::GetCurrentProcessId();
            BYTE r = (BYTE)((pid * 73) | 0x60);
            BYTE g = (BYTE)((pid * 37) | 0x60);
            BYTE b = (BYTE)((pid * 29) | 0x60);
            HBRUSH br = ::CreateSolidBrush(RGB(r, g, b));
            ::FillRect(hdc, &rc, br);
            ::DeleteObject(br);

            wchar_t buf[256];
            _snwprintf_s(buf, _TRUNCATE,
                         L"demo_child\nPID = %lu\nHWND = %p\n\n"
                         L"按 ESC 关闭本子进程",
                         pid, hwnd);
            ::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, RGB(0, 0, 0));
            ::DrawTextW(hdc, buf, -1, &rc,
                        DT_CENTER | DT_VCENTER | DT_NOPREFIX);

            ::EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
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
            return 1;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

/* ============================================================
 * 创建窗口
 * ============================================================ */
HWND CreateChildWindow(HINSTANCE hInst, bool embedded_mode) {
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

    /* 嵌入模式：用 WS_POPUP，host SetParent 后会改成 WS_CHILD */
    DWORD style = embedded_mode ? WS_POPUP
                                : WS_OVERLAPPEDWINDOW;

    return ::CreateWindowExW(
        0, kClassName, title, style,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        nullptr, nullptr, hInst, nullptr);
}

/* ============================================================
 * 发送握手消息
 * ============================================================ */
void SendHandshake(const MhxArgs& args, HWND child_hwnd) {
    if (!::IsWindow(args.host_hwnd)) {
        ::OutputDebugStringW(L"[demo_child] host_hwnd invalid");
        return;
    }

    LRESULT r = ::SendMessageW(args.host_hwnd, MHX_NEW_CLIENT,
                               static_cast<WPARAM>(args.slot_id),
                               reinterpret_cast<LPARAM>(child_hwnd));

    wchar_t msg[256];
    _snwprintf_s(msg, _TRUNCATE,
                 L"[demo_child] handshake: slot=%d host=%p child=%p result=%lld\n",
                 args.slot_id, args.host_hwnd, child_hwnd, (long long)r);
    ::OutputDebugStringW(msg);
}

} /* namespace */

/* ============================================================
 * Entry
 * ============================================================ */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShowCmd) {
    MhxArgs args = ParseArgs();
    bool embedded = args.Valid();

    HWND hwnd = CreateChildWindow(hInst, embedded);
    if (!hwnd) {
        ::MessageBoxW(nullptr, L"demo_child CreateWindow 失败",
                      L"demo_child", MB_ICONERROR);
        return 1;
    }

    if (embedded) {
        /* 嵌入模式：先发握手，host 会立即 SetParent + ShowWindow */
        SendHandshake(args, hwnd);
        /* 不主动 ShowWindow；host 那边会处理显示 */
    } else {
        /* 独立模式：正常显示 */
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
