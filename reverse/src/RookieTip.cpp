/**
 * mhtab.exe 还原 - 新手提示组件
 * 基于 Win32 标准 Tooltip 子类化
 */
#include "include/globals.h"

/*
 * @0x14000DD20  RookieTipWindow::init
 *  - CreateWindowExA("tooltips_class32", ...)
 *  - 子类化 WndProc
 *  - 存 this 到 GWLP_USERDATA
 *  - SendMessage(TTM_ADDTOOL=0x404) / TTM_TRACKACTIVATE(0x411)
 */
LRESULT __fastcall RookieTipWindow_Init(LONG_PTR self, __int64 hInstance, __int64 parent_hwnd)
{
    *(_QWORD*)(self + 8)  = hInstance;
    *(_QWORD*)(self + 16) = parent_hwnd;

    INITCOMMONCONTROLSEX icc = { 8, ICC_BAR_CLASSES };
    if (!InitCommonControlsEx(&icc))
        throw "RookieTipWindow::init : CreateWindowEx() function return null";

    HWND tip = CreateWindowExA(0, "tooltips_class32", NULL,
        0x80000042, /* WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP|TTS_BALLOON */
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        (HWND)parent_hwnd, NULL, (HINSTANCE)hInstance, NULL);
    *(_QWORD*)(self + 24) = (_QWORD)tip;
    if (!tip)
        throw "RookieTipWindow::init : CreateWindowEx() function return null";

    /* 填充 TOOLINFO (self+40..+104) */
    *(_QWORD*)(self + 48) = (_QWORD)parent_hwnd;
    *(_QWORD*)(self + 56) = (_QWORD)parent_hwnd;
    *(_QWORD*)(self + 80) = hInstance;
    *(_DWORD*)(self + 40) = 64;    /* cbSize */
    *(_DWORD*)(self + 44) = 161;   /* TTF_IDISHWND|TTF_SUBCLASS|TTF_TRANSPARENT */
    GetClientRect((HWND)parent_hwnd, (LPRECT)(self + 64));
    strncpy((char*)(self + 112), "RookieTipWindow", 0x400);
    *(_QWORD*)(self + 88) = self + 112;

    SendMessageA(tip, TTM_ADDTOOLA, 0, self + 40);
    SetWindowPos(tip, NULL, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    /* 子类化 */
    SetWindowLongPtrA(tip, GWLP_USERDATA, self);
    LONG_PTR old = SetWindowLongPtrA(tip, GWLP_WNDPROC,
                                      (LONG_PTR)RookieTipWindow_NewWndProc);
    *(_QWORD*)(self + 32) = old;

    return SendMessageA(tip, TTM_TRACKACTIVATE, 0, self + 40);
}

/*
 * @0x14000DF20  RookieGuide::loadState
 *
 * 从 "{workdir}\xy2.ini" 读 [TabCtrl]RookieGuide 的值（范围限制到 0~5）。
 * 字符串参数由 IDA 反编译显示为 std::string SSO 构造（栈上）：
 *   file    = "xy2.ini"
 *   section = "TabCtrl"
 *   key     = "RookieGuide"
 *   default = "0"
 * 调用 get_ini(buf, file, section, key, default) → my_sscanf("%d", &state)
 */
void __fastcall RookieGuide_loadState(int* dst)
{
    /* IDA 还原: std::string 四个参数在栈上构造，SSO 方式 */
    /* 以下以伪代码呈现（原代码有大量内联 std::string 析构） */
    int state = 0;
    char buf[1024];

    /* get_ini(&result, workdir=?, section="TabCtrl", key="RookieGuide", default="0") */
    /* 省略 std::string 构造细节 */

    /* 规范化 state 到 0..5 */
    if (state && state != 5) state -= 1;
    if (state < 0) state = 0;
    if (state > 5) state = 5;
    *dst = state;

    char v13;
    Log_Stub(&v13, "RookieGuide::loadState, state = %d, statestr = %d\n", state, state);
}

/*
 * @0x14000E950  RookieTipWindow_NewWndProc
 * 子类化后的窗口过程，拦截 Tooltip 的 WM_* 消息做自定义绘制。
 * （完整代码参见 IDB 中 0x14000E950）
 */
/* extern LRESULT RookieTipWindow_NewWndProc(HWND, UINT, WPARAM, LPARAM); */
