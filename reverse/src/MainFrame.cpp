/**
 * mhtab.exe 还原 - 主窗口过程
 *
 * MainFrame_WndProc 是程序最大的函数（4041 字节，216 case 大 switch），
 * 此处仅提供：
 *   1. C++ this 绑定 thunk（完整代码）
 *   2. WndProc 的结构骨架与已识别的关键 case
 * 完整 WndProc 内部 switch 未在此完整呈现（参考 IDB 中 0x14000AFF0）。
 */

#include "include/globals.h"

/*
 * @0x14000AD90  MainFrame_WndProc_thunk
 *
 * 经典 C++ 窗口过程绑定模板：
 *  - WM_NCCREATE (0x81): 把 lpCreateParams 中的 this 指针存到 GWL_USERDATA(-21)
 *  - 其他消息: 取出 this，转发到 MainFrame_WndProc(this, hwnd, msg, wparam, lparam)
 *
 * 由 RegisterClassA 时设为 lpfnWndProc。
 */
LRESULT __fastcall MainFrame_WndProc_thunk(HWND hwnd, UINT msg, WPARAM wp, LONG_PTR* lp)
{
    if (msg == WM_NCCREATE /* 0x81 */) {
        /* lp 指向 CREATESTRUCT，*lp 指向 CREATESTRUCT.lpCreateParams (this 指针) */
        LONG_PTR this_ptr = *lp;
        *(_QWORD*)(this_ptr + 24) = (_QWORD)hwnd;       /* this->hwnd = hwnd */
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, this_ptr);
        return 1;
    }
    LONG_PTR this_ptr = GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    return MainFrame_WndProc(this_ptr, hwnd, msg, wp, (LPARAM)lp);
}

/*
 * @0x14000AFF0  MainFrame_WndProc
 *
 * 主窗口过程，4041 字节，cyclomatic complexity 35，包含 216 个 case 的大 switch
 * 通过 jump table 分发。仅在此给出结构示意：
 *
 * LRESULT MainFrame_WndProc(__int64 this, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
 * {
 *     switch (msg) {
 *
 *         case WM_CREATE:               // 创建子控件、加载皮肤、初始化 Tab 控件
 *         case WM_DESTROY:              // 退出清理 + PostQuitMessage
 *         case WM_PAINT:                // 标题栏自绘、Tab 头自绘
 *         case WM_SIZE:                 // 重新布局子窗口
 *         case WM_NCHITTEST:            // 自定义非客户区命中测试（实现拖拽）
 *         case WM_SYSCOMMAND:           // SC_CLOSE 特殊处理（"WM_SYSCOMMAND SC_CLOSE, here!" 字符串）
 *
 *         case WM_COPYDATA:             // 接收其他实例转发的命令行
 *
 *         case 0x8101:                  // 查询主框架状态（返回当前 active view）
 *         case 0x8102:                  // 查询 workdir hash（实例 ID）
 *         case 0x8029:                  // 接受激活通知
 *
 *         case WM_LBUTTONDOWN:
 *         case WM_LBUTTONUP:
 *         case WM_MOUSEMOVE:            // 鼠标拖拽 Tab 头：DragDetect → BeginDragging
 *         case WM_RBUTTONUP:            // Tab 头右键菜单：TrackPopupMenu
 *
 *         case WM_NOTIFY:               // SysTabControl32 通知（TCN_SELCHANGE 等）
 *         case WM_COMMAND:              // 菜单命令
 *
 *         case WM_TIMER:                // 0xFA0/0xFA1/0xFA2 三个定时器
 *
 *         default:
 *             return DefWindowProcA(hwnd, msg, wp, lp);
 *     }
 * }
 *
 * 已通过反编译确认的字符串证据：
 *   "WM_SYSCOMMAND SC_CLOSE, here! wParam = %08x, childWnd = %08x, GetCapture = %08x"
 *   "ClientInfo %d"
 *   "[%s]%s, %d"
 */
extern LRESULT __fastcall MainFrame_WndProc(__int64 this_ptr, HWND hwnd, UINT msg,
                                            WPARAM wp, LPARAM lp);

/* ============================================================
 * 拖拽：TabBarPlus::doDragging
 * @0x14000F050 (2295 字节)
 *
 * Tab 头的拖拽过程（每次 WM_MOUSEMOVE 调用）：
 *  - GetCursorPos + WindowFromPoint 找鼠标下的窗口
 *  - GetClassNameW 验证目标是 MHXYMainFrame 类
 *  - GetAncestor 找到顶层 mainframe
 *  - 计算拖拽指示器（drag indicator）位置
 *  - SetCursor 切换光标
 *
 * 字符串证据：
 *   "TabBarPlus::doDragging window from point result = %d, dragWnd = %d"
 *   "TabBarPlus::doDragging : get window = %d, parentwnd = %d, _hParent=%d, ..."
 */
extern void TabBarPlus_doDragging(__int64* self);
