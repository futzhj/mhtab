/**
 * mhtab.exe 还原 - 13 个消息分发处理器
 *
 * 由 TabCtrlApp::init 通过 MainFrame_RegisterMsgHandler 注册到主窗口。
 * 调用约定: __int64 handler(__int64 mainframe_self, __int64 msg_arg)
 *
 * 命名前缀:
 *   Dispatch_*  - 子进程→主进程协议处理（含完整业务）
 *   MainFrame_* - 主窗口本地消息处理
 *
 * 通用模式:
 *   - msg_arg 总是指向某结构，msg_arg+16 是真正的参数 payload
 *   - payload[0] (int) 通常是 view 槽位索引
 *   - 验证流程: 索引边界检查 → 槽位 active 检查 → ViewContainer ready 检查 → 业务
 */

#include "include/globals.h"

/*
 * @0x140015480  Dispatch::newClient (msg = 1)
 *
 * my.exe 子进程启动后发送的第一条消息。主进程在此：
 *  - 找到对应 view 槽位
 *  - 记录子进程的窗口 + 类型
 *  - SetParent 把子窗口嵌入主框架（通过 sub_1400084D0）
 *  - 设置 active_flag = 2（已绑定）
 *  - SendMessage(0x1304) 心跳确认
 *  - 4 状态标志切换（用于"超过 4 个 view"的特殊布局）
 *
 * 字符串证据:
 *   "Dispacth::newClient : first receive the message, clientType = %d, clientIdx = %d, ..."
 *   "Dispacth::newClient END: srcWnd = %08x, mainframeIdx = %d, clientid = %d, ..."
 *   （注意：原作者拼写错误 "Dispacth"）
 */
__int64 __fastcall Dispatch_NewClient_msg1(__int64 a1, __int64 a2)
{
    if (!a2) return -1LL;
    __int64 payload = *(_QWORD*)(a2 + 16);
    __int64 slot_idx = *(int*)(payload + 4);

    /* 检查 slot 索引有效性 */
    __int128* tabs = TabsList_GetInstance();
    if ((int)slot_idx >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3))
        return -1LL;

    int* slot = *(int**)(*(_QWORD*)tabs + 8 * slot_idx);
    if (!((_BYTE*)slot)[356] || !slot) return -1LL;

    /* 在 mainframe view list 中找 ViewContainer */
    __int64 container = ((_QWORD*)slot)[1];
    unsigned __int64 v8 = 0;
    int* mf = MainFrameSingleton_GetInstance();
    __int64 vec_begin = ((_QWORD*)mf)[129];
    unsigned __int64 vec_size = (((_QWORD*)mf)[130] - vec_begin) >> 3;
    if (vec_size) {
        do {
            __int64 v12 = *(_QWORD*)(vec_begin + 8 * v8);
            if (v12 == container && *(_DWORD*)(v12 + 968)) break;
            ++v8;
        } while (v8 < vec_size);
    }
    if (v8 == vec_size || (v8 & 0x80000000) != 0) return -1LL;

    /* 缓存旧的 client_type 供后面 4-状态判断 */
    int v13 = slot[154];
    slot[88] = *(_DWORD*)payload;            /* clientType */
    *slot    = (int)slot_idx;
    ((_QWORD*)slot)[42] = a1;
    slot[154] = *(_DWORD*)(payload + 12);   /* clientIdx */
    int v14 = *(_DWORD*)(payload + 8);      /* flags */

    /* 强制结束之前的关联（state=2） */
    if (*(_DWORD*)(container + 968) != 2)
        View_ForceClose(container, 2);

    /* === 首次注册（active==1 → 2）=== */
    if (((_BYTE*)slot)[356] == 1) {
        strncpy((char*)slot + 357, (const char*)(payload + 41), 0x80);
        strncpy((char*)slot + 485, (const char*)(payload + 41), 0x80);

        unsigned int clientType = slot[88];
        slot[155] = Calc_30s_Bucket(clientType);

        char v33;
        Log_Stub(&v33,
                 "Dispacth::newClient : first receive the message, clientType = %d, clientIdx = %d, "
                 "wndHandle = %d, and insert tab\n",
                 clientType, *slot, ((_QWORD*)slot)[42]);

        sub_1400084D0(container, slot, (char*)slot + 485);

        unsigned int v34, v35;
        sub_140007E10(container, &v34, &v35);
        if ((SendMessageA(*(HWND*)(container + 224), MHMSG_VIEW_HEARTBEAT, 0, 0) != 1
             && v34 == *(_DWORD*)(payload + 28) && v35 == *(_DWORD*)(payload + 32))
            || sub_140007540(container, *(unsigned*)(payload + 28), *(unsigned*)(payload + 32)))
        {
            sub_140018880((unsigned)TabsList_GetInstance(), slot_idx,
                          *(_DWORD*)(payload + 28), *(_DWORD*)(payload + 32), 0);
        }

        ((_BYTE*)slot)[356] = 2;            /* 状态推进 */
        Sub_OnViewLost((__int64)Sub_GetSomeSingletonA(), 0, container);
        sub_140009310(container);
        goto LABEL_42;
    }

    /* === 重复注册或更新 === */
    int v20 = sub_140007E70(container, (unsigned)slot_idx);
    __int64 v21 = *(unsigned*)(payload + 36);
    if (v20 <= 0 || (int)v21 <= 0 || v20 == (DWORD)v21) {
        sub_140008E10(container, (unsigned)slot[7], v21);

        if ((v14 & 2) != 0) {
            strncpy((char*)slot + 357, (const char*)(payload + 41), 0x80);
            sub_1400092C0(container, (unsigned)slot[7]);
        }

        unsigned int v33, v34;
        sub_140007E10(container, &v33, &v34);

        if ((v14 & 4) != 0) {
            if (*(_DWORD*)(container + 1000) == slot[7]) {
                __int64 nx = *(unsigned*)(payload + 28);
                __int64 ny = *(unsigned*)(payload + 32);
                if (((__int64)v34 << 32 | v33) != ((__int64)ny << 32 | nx))
                    sub_140007540(container, nx, ny);
            }
            sub_140018880((unsigned)TabsList_GetInstance(), slot_idx,
                          *(_DWORD*)(payload + 28), *(_DWORD*)(payload + 32),
                          *(_BYTE*)(payload + 40) != 0);
        }

        if (v14 == 255) {
            int cur = *(_DWORD*)(container + 1000);
            if (slot[7] != cur && !((_BYTE*)slot)[613]) {
                ((_BYTE*)slot)[613] = 1;
                TabCtrl_SwitchTab(container + 200);
            }
            HWND mfwnd = *(HWND*)(container + 24);
            if (GetForegroundWindow() != mfwnd) FlashWindow(mfwnd, 0);
            View_Activate(container, cur, 0);
        }
        goto LABEL_42;
    }

    /* === 索引冲突：转移到新主框架 === */
    int* mf2 = MainFrameSingleton_GetInstance();
    __int64 v23 = slot[7];
    int v25 = MainFrame_AcquireViewContainer((__int64)mf2);
    if (v25 == -1) return -1LL;

    __int64 v26 = *(_QWORD*)(((_QWORD*)mf2)[129] + 8LL * v25);
    (*(void(__fastcall**)(__int64, void*))(*(_QWORD*)(v26 + 200) + 112LL))(v26 + 200, &unk_14003E4B0);
    View_ForceClose(v26, 1);
    if (!MainFrame_PostMsg((__int64)mf2, container, v23, 3, v26)) {
        TabCtrlApp_BeginDragging((__int64)mf2, container, v26, v23, 0);
        return -1LL;
    }
    (*(void(__fastcall**)(__int64, __int64))(*(_QWORD*)(container + 200) + 136LL))(container + 200, v23);

LABEL_42:
    /* 4-状态切换（≥4 个 view 时切到平铺布局） */
    if (v13 >= 4) {
        if (v13 == 4 && slot[154] > 4) sub_140018690(slot + 72);
    } else if (slot[154] == 4) {
        sub_1400186D0(slot + 76, &unk_14003E120, 3);
        sub_1400187D0(slot + 72);
    }

    char v33;
    Log_Stub(&v33,
             "Dispacth::newClient END: srcWnd = %08x, mainframeIdx = %d, clientid = %d, "
             "processHandle = %d, used = %d\n",
             a1, *(_DWORD*)(container + 196), *(_DWORD*)(payload + 4),
             ((_QWORD*)slot)[43], ((char*)slot)[356]);
    return 0;
}

/*
 * @0x140013C80  Dispatch::onReadyConfirm (msg = 3)
 *
 * 子进程报告"已经初始化完成、可以接收输入"。
 *  - 检查 mainframe.global_ready_flag → 清零
 *  - flags & 1: 是 OnReady 包，否则丢弃
 *  - 提取 view_idx / target_idx / size_w / size_h / cur_active
 *  - 移动 ViewContainer 状态：BringWindowToTop + KillTimer(0xFA0)
 *  - 调整大小同步（sub_140007540）
 *  - flags & 2: 强制设置位置和命令
 *  - TryReloadView：成功直接返回；失败则停用 view
 *  - 通知 SingletonA "view已就绪"（OnViewLost + 状态码 2/3）
 */
__int64 __fastcall Dispatch_OnReadyConfirm_msg3(__int64 a1, __int64 a2)
{
    /* 完整逻辑较复杂，此处保留核心路径，省略嵌套校验返回路径 */
    if (!*((_BYTE*)MainFrameSingleton_GetInstance() + 1096)) return -1;
    *((_BYTE*)MainFrameSingleton_GetInstance() + 1096) = 0;
    if (!a2) return -1;

    int* payload = *(int**)(a2 + 16);
    if ((payload[1] & 1) == 0) return 0;

    __int64 slot_idx   = payload[0];
    __int64 dst_view   = payload[3];
    unsigned w         = payload[4];
    unsigned h         = payload[5];
    int active_idx     = payload[6];

    int* mf = MainFrameSingleton_GetInstance();
    if ((int)slot_idx < 0) return 0;
    __int64 vec = ((_QWORD*)mf)[129];
    if ((int)slot_idx >= (int)((((_QWORD*)mf)[130] - vec) >> 3)) return 0;
    __int64 src_view = *(_QWORD*)(vec + 8 * slot_idx);
    if (!*(_DWORD*)(src_view + 968) || !src_view) return 0;

    if ((int)dst_view < 0) return 0;
    if ((int)dst_view >= (int)((((_QWORD*)mf)[130] - vec) >> 3)) return 0;
    __int64 dst = *(_QWORD*)(vec + 8 * dst_view);
    if (!*(_DWORD*)(dst + 968) || !dst) return 0;

    /* 找匹配的 TabsList 槽 */
    __int128* tabs = TabsList_GetInstance();
    __int64 ti = payload[0];
    if ((int)ti >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3)) return 0;
    __int64 slot = *(_QWORD*)(*(_QWORD*)tabs + 8 * ti);
    if (!*(_BYTE*)(slot + 356)) return 0;
    if (*(_QWORD*)(slot + 8) != src_view) return 0;

    if (*(_DWORD*)(dst + 968) != 2) View_ForceClose(dst, 2);
    BringWindowToTop(*(HWND*)(dst + 24));
    KillTimer(*(HWND*)(dst + 24), 0xFA0u);

    if (((*(_QWORD*)(dst + 984) - *(_QWORD*)(dst + 976)) & ~7uLL) == 0)
        sub_1400077E0(dst, src_view);

    int sz_w, sz_h;
    sub_140007E10(src_view, &sz_w, &sz_h);
    sub_140007A20(src_view, *(unsigned*)(slot + 28));
    *(_QWORD*)(slot + 16) = *(_QWORD*)(dst + 24);
    *(_QWORD*)(slot + 8)  = dst;
    *(_DWORD*)(slot + 24) = (int)dst_view;

    if ((payload[1] & 2) != 0) {
        *(_DWORD*)(slot + 276) = w;
        *(_DWORD*)(slot + 280) = h;
        *(_DWORD*)(slot + 28)  = 0;
        sub_140008610(dst, 0, slot);
        sub_140007540(dst, w, h);
        if (active_idx > 0) sub_140008E10(dst, 0, (unsigned)active_idx);
        sub_1400076F0(dst);
    } else {
        sub_140008610(dst, (unsigned)((int*)mf)[279], slot);
    }

    if (!(unsigned)TryReloadView((__int64)mf, src_view)) {
        if (sub_1400089B0(src_view)) {
            /* 在 mainframe 中重新搜索 dst */
            __int64* end   = (__int64*)((_QWORD*)mf)[130];
            __int64* begin = (__int64*)((_QWORD*)mf)[129];
            unsigned __int64 idx = 0;
            unsigned __int64 cnt = ((char*)end - (char*)begin) / 8;
            for (; idx < cnt; ++idx) if (begin[idx] == dst && *(_DWORD*)(begin[idx] + 968)) break;
            if (idx != cnt) {
                if (sub_1400089B0(dst)) {
                    View_Deactivate(dst);
                } else {
                    /* 把所有 view 取消置顶后清理 dst */
                    int n = 0;
                    if (cnt) {
                        do { View_Deactivate(begin[n]); ++n; } while (n < (int)cnt);
                    }
                    sub_140008FC0(dst);
                }
            }
        }
        sub_1400141C0(MainFrameSingleton_GetInstance(), src_view, 0);
    }

    BOOL alive = SendMessageA(*(HWND*)(dst + 24), MHMSG_VIEW_HEARTBEAT, 0, 0) != 1;
    Sub_OnViewLost((__int64)Sub_GetSomeSingletonA(), alive + 2, dst);

    int* mf3 = MainFrameSingleton_GetInstance();
    if (((int*)mf3)[280] == 2) ((int*)mf3)[280] = 0;
    BringWindowToTop(*(HWND*)(dst + 24));
    return 0;
}

/* @0x1400133F0  msg=4 转发键盘/鼠标事件到子窗口 */
__int64 __fastcall MainFrame_OnMsg4_ForwardInput(__int64 a1, __int64 a2)
{
    if (!a2) return -1;
    int* payload = *(int**)(a2 + 16);
    __int64 idx = payload[0];

    __int128* tabs = TabsList_GetInstance();
    if ((int)idx >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3)) return 0;
    __int64 slot = *(_QWORD*)(*(_QWORD*)tabs + 8 * idx);
    if (!*(_BYTE*)(slot + 356) || !slot) return 0;

    /* 在 mainframe view 中找匹配的 ViewContainer */
    __int64 container = *(_QWORD*)(slot + 8);
    int* mf = MainFrameSingleton_GetInstance();
    __int64 vec = ((_QWORD*)mf)[129];
    unsigned __int64 cnt = (((_QWORD*)mf)[130] - vec) >> 3;
    unsigned __int64 i;
    for (i = 0; i < cnt; ++i) {
        __int64 v = *(_QWORD*)(vec + 8 * i);
        if (v == container && *(_DWORD*)(v + 968)) break;
    }
    if (i == cnt || (i & 0x80000000)) return -1;

    if (!*(_BYTE*)(container + 1077)) {
        /* 调用 sub_140009070 转发输入：(container, 1, has_data, key1, key2, key3, key4) */
        ForwardInputToChildWnd(container, 1, ((_BYTE*)payload)[4] != 0,
                               payload[2], payload[3], payload[4], payload[5]);
    }
    return 0;
}

/* @0x1400162D0  msg=6 子窗口位置/大小同步 */
__int64 __fastcall Dispatch_UpdatePos_msg6(__int64 a1, __int64 a2)
{
    if (!a2) return -1;
    const char* p = *(const char**)(a2 + 16);
    __int64 idx = *(int*)p;

    __int128* tabs = TabsList_GetInstance();
    if ((int)idx >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3)) return -1;
    __int64 slot = *(_QWORD*)(*(_QWORD*)tabs + 8 * idx);
    if (!*(_BYTE*)(slot + 356) || !slot) return -1;

    unsigned serial = *(_DWORD*)(slot + 352);
    *(_BYTE*)(slot + 32)   = 1;                         /* mouse_down_flag */
    *(_DWORD*)(slot + 264) = ((int*)p)[58];             /* mouse_x */
    *(_DWORD*)(slot + 260) = ((int*)p)[57];             /* mouse_y */
    *(_DWORD*)(slot + 268) = ((int*)p)[59];             /* mouse_z */
    *(_DWORD*)(slot + 620) = Calc_30s_Bucket(serial);   /* 时间桶刷新 */

    /* 拷贝 4 个文本字段（人物/队伍/伙伴名） */
    strncpy((char*)(slot + 33),  p + 4,   0x40);
    strncpy((char*)(slot + 97),  p + 68,  0x40);
    strncpy((char*)(slot + 161), p + 132, 0x40);
    strncpy((char*)(slot + 225), p + 196, 0x20);

    sub_1400091E0(*(_QWORD*)(slot + 8), *(unsigned*)(slot + 28));
    return 0;
}

/* @0x140013150  msg=7 激活某 view（不抢前台） */
__int64 __fastcall Dispatch_ActivateView_msg7(__int64 a1, __int64 a2)
{
    if (!a2) return -1;
    int* payload = *(int**)(a2 + 16);
    __int64 idx = payload[0];

    __int128* tabs = TabsList_GetInstance();
    if ((int)idx >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3)) return -1;
    __int64 slot = *(_QWORD*)(*(_QWORD*)tabs + 8 * idx);
    if (!*(_BYTE*)(slot + 356) || !slot) return -1;

    __int64 container = *(_QWORD*)(slot + 8);
    unsigned view_id  = *(_DWORD*)(slot + 28);
    if (*(_DWORD*)(container + 1000) != view_id && !*(_BYTE*)(slot + 613)) {
        *(_BYTE*)(slot + 613) = ((_BYTE*)payload)[4];
        TabCtrl_SwitchTab(container + 200);
    }
    sub_140007630(container, view_id);
    return 0;
}

/* @0x140016140  msg=8 隐藏所有 view（按 flag 决定附加策略） */
__int64 __fastcall Dispatch_HideView_msg8(__int64 a1, __int64 a2)
{
    if (!a2) return -1;
    int* payload = *(int**)(a2 + 16);
    __int128* tabs = TabsList_GetInstance();
    if ((int)payload[0] >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3)) return -1;
    __int64 slot = *(_QWORD*)(*(_QWORD*)tabs + 8 * payload[0]);
    if (!*(_BYTE*)(slot + 356) || !slot) return -1;

    int* mf = MainFrameSingleton_GetInstance();
    __int64 vec_size = (__int64)(((_QWORD*)mf)[130] - ((_QWORD*)mf)[129]) >> 3;
    BOOL with_flag = ((_BYTE*)payload)[4] != 0;

    for (unsigned __int64 i = 0; i < (unsigned)vec_size; ++i) {
        __int64 v = *(_QWORD*)(((_QWORD*)mf)[129] + 8 * i);
        if (*(_DWORD*)(v + 968) && v) sub_140007F80(v, with_flag);
    }
    return 0;
}

/* @0x140015C90  msg=16 应用层请求新建 view */
__int64 __fastcall MainFrame_OnMsg16_NewView(__int64 a1, __int64 a2)
{
    if (!a2) return -1;
    const CHAR* payload = *(const CHAR**)(a2 + 16);
    if (!payload[4]) return 0;        /* flag=0 不创建 */

    int* mf = MainFrameSingleton_GetInstance();
    __int64 idx = *(int*)payload;
    __int64 vec_begin = ((_QWORD*)mf)[129];
    __int64 view = 0;
    if ((int)idx >= 0 && (int)idx < (int)((((_QWORD*)mf)[130] - vec_begin) >> 3)) {
        view = *(_QWORD*)(vec_begin + 8 * idx);
        if (!*(_DWORD*)(view + 968)) view = 0;
    }
    TabCtrlApp_ViewNew(MainFrameSingleton_GetInstance(), view, 0, payload + 5);
    return 0;
}

/* @0x140015A30  msg=17 通过父窗口 HWND 查找 view */
__int64 __fastcall Dispatch_GetByParentHwnd_msg17(HWND hwnd, __int64 a2)
{
    __int64 out = *(_QWORD*)(a2 + 16);
    HWND parent = GetParent(hwnd);

    int* mf = MainFrameSingleton_GetInstance();
    __int64 vec = ((_QWORD*)mf)[129];
    unsigned __int64 cnt = (((_QWORD*)mf)[130] - vec) >> 3;
    unsigned __int64 i = 0;
    for (; i < cnt; ++i) {
        __int64 v = *(_QWORD*)(vec + 8 * i);
        if (*(HWND*)(v + 24) == parent && *(_DWORD*)(v + 968)) break;
    }
    if (i == cnt || (DWORD)i == (DWORD)-1) return -1;

    __int64 found = 0;
    if (!(i & 0x80000000) && (int)i < (int)((((_QWORD*)mf)[130] - vec) >> 3)) {
        found = *(_QWORD*)(vec + 8 * i);
        if (!*(_DWORD*)(found + 968)) found = 0;
    }
    sub_140012C20(MainFrameSingleton_GetInstance(), found, out + 512);
    return 0;
}

/* @0x1400152E0  msg=25 view 清理（state=4 + 计时器到期 → 取消订阅） */
__int64 __fastcall Dispatch_CleanupView_msg25(__int64 a1, __int64 a2)
{
    if (!a2) return -1;
    __int64 idx = **(int**)(a2 + 16);
    __int128* tabs = TabsList_GetInstance();
    if ((int)idx >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3)) return -1;
    __int64 slot = *(_QWORD*)(*(_QWORD*)tabs + 8 * idx);
    if (!*(_BYTE*)(slot + 356) || !slot) return -1;

    if (*(_DWORD*)(slot + 616) == 4) {
        if (View_TimerExpired(slot + 288)) {
            __int64 container = *(_QWORD*)(slot + 8);
            if (*(_DWORD*)(container + 1000) == *(_DWORD*)(slot + 28)
                && !(*(unsigned __int8(__fastcall**)(__int64))
                       (*(_QWORD*)(container + 1984) + 88LL))(container + 1984))
            {
                TabCtrlApp_RemoveListener(container, -1.0f);
            }
        }
    }
    return 0;
}

/* @0x140015F00  msg=26 ShowWindow 转发 */
__int64 __fastcall Dispatch_ShowWindow_msg26(__int64 a1, __int64 a2)
{
    if (!a2) return -1;
    int* payload = *(int**)(a2 + 16);
    __int64 idx = payload[0];

    __int128* tabs = TabsList_GetInstance();
    if ((int)idx >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3)) return -1;
    __int64 slot = *(_QWORD*)(*(_QWORD*)tabs + 8 * idx);
    if (!*(_BYTE*)(slot + 356) || !slot) return -1;

    int show_cmd = payload[1];
    __int64 container = *(_QWORD*)(slot + 8);
    /* SW_MINIMIZE=6 时若 flag_1078 已设则忽略 */
    if (show_cmd != SW_MINIMIZE || !*(_BYTE*)(container + 1078))
        ShowWindow(*(HWND*)(container + 24), show_cmd);
    return 0;
}

/* @0x1400163F0  msg=4102 激活 + focus（如果 payload[4]=true） */
__int64 __fastcall Dispatch_Activate_msg4102(__int64 a1, __int64 a2)
{
    if (!a2) return -1;
    int* payload = *(int**)(a2 + 16);
    __int64 idx = payload[0];
    __int128* tabs = TabsList_GetInstance();
    if ((int)idx >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3)) return -1;
    __int64 slot = *(_QWORD*)(*(_QWORD*)tabs + 8 * idx);
    if (!*(_BYTE*)(slot + 356) || !slot) return -1;

    if (((_BYTE*)payload)[4])
        View_Activate(*(_QWORD*)(slot + 8), *(_DWORD*)(slot + 28), 1);
    return 0;
}

/*
 * @0x140012EE0  Dispatch::bringToFront (msg = 4103)
 *
 * **关键技术**：用 AttachThreadInput 绕过 SetForegroundWindow 限制
 *  1. _time64 + difftime64 检查距上次 ≥1 秒（防抖）
 *  2. View_Activate 设置 view 为活动
 *  3. AttachThreadInput(前台窗口的线程, 当前线程, TRUE)
 *  4. SetWindowPos(HWND_TOPMOST → HWND_NOTOPMOST)
 *  5. SetForegroundWindow / SetFocus / SetActiveWindow
 *  6. AttachThreadInput(detach)
 *  7. ShowWindow(SW_SHOW)
 */
__int64 __fastcall Dispatch_BringToFront_msg4103(__int64 a1, __int64 a2)
{
    __time64_t Time1;
    unknown_libname_30(&Time1);                /* _time64(&Time1) */
    double dt = difftime64(Time1, Time2);
    Time2 = Time1;
    if (dt < 1.0) return -1;                   /* 防抖：1 秒内不重复执行 */

    if (!a2) return -1;
    __int64 idx = **(int**)(a2 + 16);
    __int128* tabs = TabsList_GetInstance();
    if ((int)idx >= (int)((__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3)) return -1;
    __int64 slot = *(_QWORD*)(*(_QWORD*)tabs + 8 * idx);
    if (!*(_BYTE*)(slot + 356) || !slot) return -1;

    __int64 container = *(_QWORD*)(slot + 8);
    View_Activate(container, *(_DWORD*)(slot + 28), 0);

    HWND target = *(HWND*)(container + 24);
    HWND fg     = GetForegroundWindow();
    DWORD myTid = GetCurrentThreadId();
    DWORD fgTid = GetWindowThreadProcessId(fg, 0);

    /* 关键技巧：附加到前台窗口的线程输入队列 */
    AttachThreadInput(fgTid, myTid, TRUE);
    SetWindowPos(target, HWND_TOPMOST,    0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    SetWindowPos(target, HWND_NOTOPMOST,  0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);
    SetForegroundWindow(target);
    SetFocus(target);
    SetActiveWindow(target);
    AttachThreadInput(fgTid, myTid, FALSE);

    ShowWindow(target, SW_SHOWNORMAL);
    return 0;
}

/* @0x140015B70  msg=4104 通过游戏 HWND 找 view（找不到则停用所有 view） */
__int64 __fastcall Dispatch_FindByHwnd_msg4104(__int64 a1, __int64 a2)
{
    if (!a2) return -1;
    HWND target_hwnd = *(HWND*)(*(_QWORD*)(a2 + 16) + 8LL);

    /* 在 TabsList 中找 game_hwnd 匹配的 slot */
    __int128* tabs = TabsList_GetInstance();
    int idx = 0;
    __int64 off = 0;
    unsigned __int64 cnt = (__int64)(((_QWORD*)tabs)[1] - *(_QWORD*)tabs) >> 3;
    BOOL found = FALSE;
    while (idx < (int)cnt) {
        __int64 v = *(_QWORD*)(off + *(_QWORD*)tabs);
        if (*(_BYTE*)(v + 356) && v && *(HWND*)(v + 336) == target_hwnd) { found = TRUE; break; }
        ++idx;
        off += 8;
    }

    if (!found) {
        /* 找不到目标：把所有 view 取消置顶（让用户能看到 hwnd） */
        int* mf = MainFrameSingleton_GetInstance();
        __int64 vec_cnt = (__int64)(((_QWORD*)mf)[130] - ((_QWORD*)mf)[129]) >> 3;
        for (int i = 0; i < (int)vec_cnt; ++i)
            View_Deactivate(*(_QWORD*)(8LL * i + ((_QWORD*)mf)[129]));
    }
    return -1;
}
