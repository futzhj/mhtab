/**
 * demo_child.exe - 示例可嵌入子进程
 *
 * 作用：验证 mhtabx 主窗口能正确：
 *  1. CreateProcess 启动本程序
 *  2. 等待本程序注册（MHX_NEW_CLIENT）
 *  3. SetParent 把本程序主窗口嵌入 Tab 页
 *  4. 收到 WM_DESTROY 时优雅退出
 *
 * W1 阶段：只实现一个自绘的彩色窗口，能显示 PID/HWND，便于调试。
 * W2+：会发送 MHX_NEW_CLIENT 完成协议握手。
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <tchar.h>
#include <cstdio>

namespace {

constexpr const wchar_t* kClassName = L"MhtabxDemoChild";

LRESULT CALLBACK ChildWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = ::BeginPaint(hwnd, &ps);
            RECT rc; ::GetClientRect(hwnd, &rc);

            /* 根据 PID 产生一个伪随机背景色，便于视觉区分多个 Tab */
            DWORD pid = ::GetCurrentProcessId();
            BYTE r = (BYTE)((pid * 73) & 0xFF);
            BYTE g = (BYTE)((pid * 37) & 0xFF);
            BYTE b = (BYTE)((pid * 29) & 0xFF);
            HBRUSH br = ::CreateSolidBrush(RGB(r | 0x80, g | 0x80, b | 0x80));
            ::FillRect(hdc, &rc, br);
            ::DeleteObject(br);

            wchar_t buf[256];
            _snwprintf_s(buf, _TRUNCATE,
                         L"demo_child\nPID=%lu\nHWND=%p", pid, hwnd);
            ::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, RGB(0, 0, 0));
            ::DrawTextW(hdc, buf, -1, &rc,
                        DT_CENTER | DT_VCENTER | DT_NOPREFIX);

            ::EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;    /* 让 WM_PAINT 全量绘 */
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

HWND CreateChildWindow(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ChildWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc);

    wchar_t title[64];
    _snwprintf_s(title, _TRUNCATE, L"demo_child [PID=%lu]",
                 ::GetCurrentProcessId());

    return ::CreateWindowExW(
        0, kClassName, title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        nullptr, nullptr, hInst, nullptr);
}

} /* namespace */

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShowCmd) {
    HWND hwnd = CreateChildWindow(hInst);
    if (!hwnd) {
        ::MessageBoxW(nullptr, L"demo_child CreateWindow 失败",
                      L"demo_child", MB_ICONERROR);
        return 1;
    }

    ::ShowWindow(hwnd, nShowCmd);
    ::UpdateWindow(hwnd);

    MSG msg = {};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
