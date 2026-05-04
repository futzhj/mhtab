/**
 * mhtab.exe 还原 - View 操作
 *
 * View 即 ViewContainer（0xB50 字节大对象），是真正承载某个游戏 Tab 的容器。
 * 它内部又指向某个 ViewSlot（TabsList 中的小结构）。
 */

#include "include/globals.h"

/*
 * @0x140007140  View_Activate
 *
 * 把指定 view 设为活动（显示在前台位置）。包含复杂的状态机：
 *  - byte_14004E161 是全局 reentrant guard（防递归）
 *  - SendMessage(0x1304) 心跳取得当前可用 view 数
 *  - 边界 + 队列 + 索引一致性检查
 *  - 旧 active view 状态根据其 state_machine 区分处理（state=4 走 timer，否则走 ShowWindow(0)）
 *  - SendMessage(0x130B) 同步当前 active 索引
 *  - 切换 view 后调整位置（按 mainframe 工作区裁剪）
 *  - state>3 的 view 走 sub_140018800 4097 命令（特殊布局变更）
 */
__int64 __fastcall View_Activate(__int64 a1, int target_id, int focus_only)
{
    __int64 v4 = target_id;
    if (!byte_14004E161) return 0xFFFFFFFFLL;            /* reentrant guard */
    HWND owner = *(HWND*)(a1 + 224);
    byte_14004E161 = 0;

    LRESULT v9 = SendMessageA(owner, MHMSG_VIEW_HEARTBEAT, 0, 0);
    if ((int)v4 < 0 || (int)v4 >= (int)v9) goto LABEL_37;

    __int64 q_begin = *(_QWORD*)(a1 + 976);
    if ((DWORD)v9 != (unsigned)((*(_QWORD*)(a1 + 984) - q_begin) >> 3)) goto LABEL_37;

    /* 找到当前 active view，处理转移逻辑 */
    __int64 cur_id = *(int*)(a1 + 1000);
    bool topmost = false;
    __int64 cur_view = 0;
    if ((int)cur_id >= 0 && (int)cur_id < (int)v9 && (DWORD)cur_id != (DWORD)v4) {
        cur_view = *(_QWORD*)(q_begin + 8 * cur_id);
        if (!*(_BYTE*)(cur_view + 356)) goto LABEL_37;

        if (*(_DWORD*)(cur_view + 616) == 4) {
            /* state=4: 切换计时器到下一格 */
            (*(void(__fastcall**)(__int64, LRESULT))(*(_QWORD*)(a1 + 1984) + 104LL))
                (a1 + 1984, 1);
        } else {
            ShowWindow(*(HWND*)(cur_view + 336), SW_HIDE);
            topmost = (*(_DWORD*)(cur_view + 616) > 3);
        }
    }

    /* 设置新 active id 并通知子窗口 */
    *(_DWORD*)(a1 + 1000) = (DWORD)v4;
    int sw_result = SendMessageA(*(HWND*)(a1 + 224), 0x130B, 0, 0);
    unsigned new_id = *(_DWORD*)(a1 + 1000);
    if (new_id != (unsigned)sw_result) {
        (*(void(__fastcall**)(__int64, _QWORD))(*(_QWORD*)(a1 + 200) + 128LL))(a1 + 200, new_id);
        new_id = *(_DWORD*)(a1 + 1000);
    }

    __int64 q2_begin = *(_QWORD*)(a1 + 976);
    __int64 new_view = *(_QWORD*)(q2_begin + 8LL * (int)new_id);
    if (!*(_BYTE*)(new_view + 356)) goto LABEL_37;

    if (focus_only == 1) {
        /* 只 SetFocus，不动窗口位置 */
        HWND game_hwnd = *(HWND*)(new_view + 336);
        if (game_hwnd) SetFocus(game_hwnd);
    } else {
        /* === 切换 Tab 控件高亮 === */
        if (*(_BYTE*)(*(_QWORD*)(q2_begin + 8 * v4) + 613LL)) {
            TabCtrl_SwitchTab(a1 + 200);
            *(_BYTE*)(*(_QWORD*)(*(_QWORD*)(a1 + 976) + 8 * v4) + 613LL) = 0;
        }

        unsigned int v33, v32;
        if (sub_1400186A0(new_view, &v33, &v32)
            && (v33 != *(_DWORD*)(a1 + 168) - *(_DWORD*)(a1 + 160)
                || v32 != *(_DWORD*)(a1 + 172) - *(_DWORD*)(a1 + 164)))
        {
            sub_140007540(a1, v33, v32);
        }

        if (*(_DWORD*)(new_view + 616) == 4) {
            HWND mfwnd = *(HWND*)(a1 + 24);
            if (GetForegroundWindow() == mfwnd) SetFocus(mfwnd);
            (*(void(__fastcall**)(__int64, _QWORD))(*(_QWORD*)(a1 + 1984) + 104LL))(a1 + 1984, 0);
            sub_14000A880(a1 + 1984, (unsigned)(int)(*(float*)(new_view + 288) * 1000.0f));
        } else {
            int X, Y;
            DivMod_helper(a1 + 1004, &X, &Y);

            if (IsIconic(*(HWND*)(new_view + 336)))
                ShowWindow(*(HWND*)(new_view + 336), SW_RESTORE);
            SetWindowPos(*(HWND*)(new_view + 336), 0, X, Y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

            if (*(int*)(new_view + 616) > 3) {
                /* 自定义布局命令 4097 */
                __int64 v34 = *(_QWORD*)(a1 + 24);
                /* ... 调用 sub_140018800(view, 4097, &v34, 56, -2) ... */
                sub_140018800(new_view, 4097, &v34, 56, -2);
            }
        }

        /* 调用 vtable[4]() 通知 Tab 标题刷新 */
        (*(void(__fastcall**)(__int64, __int64))(*(_QWORD*)a1 + 32LL))(a1, new_view + 357);

        /* 之前 active 是 topmost 状态：切换到 detach 模式 */
        if (cur_view && topmost) {
            __int64 v34 = *(_QWORD*)(a1 + 24);
            sub_140018800(cur_view, 4097, &v34, 56, 0);
        }
    }

    byte_14004E161 = 1;            /* release guard */
    return 0;

LABEL_37:
    byte_14004E161 = 1;
    return 0xFFFFFFFFLL;
}

/*
 * @0x140007670  View_Deactivate
 *
 * 取消 view 的"活动/置顶"状态：
 *  - SetWindowPos(HWND_NOTOPMOST)
 *  - 清除 active flag (+1072)
 *  - 内部状态对象重置（sub_14000A700）
 *  - PostMsg 给主框架，msg=4（FORWARD_INPUT），但 wParam=0 表示 detach
 */
void __fastcall View_Deactivate(__int64 view)
{
    if (!*(_BYTE*)(view + 1072)) return;

    SetWindowPos(*(HWND*)(view + 24), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    *(_BYTE*)(view + 1072) = 0;
    sub_14000A700(view + 1080, 0);

    int* mf = MainFrameSingleton_GetInstance();
    MainFrame_PostMsg((__int64)mf, view, *(_DWORD*)(view + 1000), 4, 0);
}
