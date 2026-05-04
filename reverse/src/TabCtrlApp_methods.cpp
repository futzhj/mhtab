/**
 * mhtab.exe 还原 - TabCtrlApp 核心业务方法
 *
 * 包含:
 *  - viewNew      创建新 Tab + 启动 my.exe 子进程
 *  - waitforCmd   监控子进程退出
 *  - closeAndRecycle  关闭旧 view 并补一个新的
 *  - endDragging  拖拽结束（支持跨主框架）
 *  - 3 个应用事件回调
 */

#include "include/globals.h"

/*
 * @0x1400165F0  TabCtrlApp::viewNew
 *
 * 通过 CreateProcessA 启动一个 my.exe 子进程作为 Tab 视图。
 * 命令行格式: "my.exe {arg4} {prefix}{slot} {div} {mod} {pid} {workdir_id} {pad08d}"
 *
 * 关键限制：GetActiveTabCount >= 8 时拒绝创建（保护用户不被多开过载）。
 *
 * 失败处理：
 *  - 如果已满，给所有 closing 状态的 view 强行 TerminateProcess
 *  - SetTimer(0xFA1=4001, 100ms) 等下次重试
 */
char __fastcall TabCtrlApp_ViewNew(_DWORD *self, __int64 view_in, void *prefix_arg, const CHAR *fmt_arg)
{
    /* === 检查实例上限 === */
    __int128 *tabs = TabsList_GetInstance();
    if ((int)GetActiveTabCount((__int64)tabs) >= 8) {
        HWND v10;
        if (view_in) {
            v10 = *(HWND *)(view_in + 24);
        } else {
            __int64 v9 = ((_QWORD*)self)[129];
            if (v9 == ((_QWORD*)self)[130]) goto LABEL_7;
            v10 = *(HWND *)(*(_QWORD *)v9 + 24LL);
        }
        SetTimer(v10, 0xFA1u, 100u, 0);  /* 100ms 后重试 */

LABEL_7:
        /* 强行终止所有处于 closing 状态的 view 进程 */
        for (__int64 *i = *(__int64 **)TabsList_GetInstance();
             i != *((__int64 **)TabsList_GetInstance() + 1); ++i)
        {
            __int64 v12 = *i;
            if (*(_BYTE *)(*i + 356)) {            /* active flag */
                if (*(_DWORD *)(v12 + 616) == 1)   /* state == closing */
                    TerminateProcess(*(HANDLE *)(v12 + 344), 0);
            }
        }
        return 0;
    }

    /* === 找到目标 view === */
    unsigned __int64 v15;
    if (view_in) {
        __int64 v16 = ((_QWORD*)self)[129];
        v15 = 0LL;
        unsigned __int64 v17 = (((_QWORD*)self)[130] - v16) >> 3;
        if (v17) {
            do {
                __int64 v18 = *(_QWORD *)(v16 + 8 * v15);
                if (v18 == view_in && *(_DWORD *)(v18 + 968)) break;
                ++v15;
            } while (v15 < v17);
        }
        if (v15 == v17) return 0;
    } else {
        int v14 = MainFrame_AcquireViewContainer((__int64)self);
        LODWORD(v15) = v14;
        if (v14 == -1) return 0;
        view_in = *(_QWORD *)(((_QWORD*)self)[129] + 8LL * v14);
        sub_1400076F0(view_in);
    }
    if ((v15 & 0x80000000) != 0LL) return 0;

    /* === 分配 view slot === */
    int v27 = 0;
    __int64 *tabs_p = (__int64 *)TabsList_GetInstance();
    _QWORD *v20 = TabsList_AllocSlot(tabs_p, &v27);
    v20[2] = *(_QWORD *)(view_in + 24);
    v20[1] = view_in;
    ((_DWORD *)v20)[7] = -1;
    ((_DWORD *)v20)[6] = v15;
    ((_BYTE *)v20)[356] = 1;     /* 标记 active */

    /* === 构造命令行 + CreateProcessA === */
    int v28, v29;
    DivMod_helper(view_in + 1004, &v29, &v28);
    if (!prefix_arg) prefix_arg = (void*)off_14004E4D0;
    int v25 = *(_DWORD *)(view_in + 196);
    int lpProcessInformation = g_workdir_instance_id;
    DWORD lpStartupInfo = GetCurrentProcessId();
    if (!fmt_arg) fmt_arg = (CHAR*)byte_14003DD3C;

    CHAR CommandLine[512];
    my_sprintf(CommandLine, "%s %s %s%d %d %d %d %d %08d",
               prefix_arg, fmt_arg, off_14004E1A0,
               v27, v29, v28, lpStartupInfo, lpProcessInformation, v25);

    _BYTE v26[4];
    __int64 v21 = -1LL;
    do ++v21; while (CommandLine[v21]);
    Log_Stub(v26, "TabCtrlApp::viewNew : start my.exe, ARG_LEN = %d; arg = %s\n", v21, CommandLine);

    STARTUPINFOA StartupInfo;
    PROCESS_INFORMATION ProcessInformation;
    memset(&StartupInfo, 0, sizeof(StartupInfo));
    StartupInfo.cb           = 104;
    StartupInfo.dwFlags      = STARTF_USESHOWWINDOW;
    StartupInfo.wShowWindow  = SW_HIDE;

    if (CreateProcessA(0, CommandLine, 0, 0, 0, 0, 0, 0, &StartupInfo, &ProcessInformation)) {
        Log_Stub(v26, "TabCtrlApp::viewNew : before add processHandle = %d, evtNum = %d\n",
                 v20[43], *self);
        v20[43] = (_QWORD)ProcessInformation.hProcess;
        ((_DWORD *)v20)[156] = ProcessInformation.dwProcessId;
        /* 把 hProcess 加入 self 中 WaitForMultipleObjects 数组 */
        *(_QWORD *)&self[2 * (*self)++ + 2] = v20[43];
        Log_Stub(v26, "TabCtrlApp::viewNew : add processHandle = %d, evtNum = %d\n",
                 v20[43], *self);
        CloseHandle(ProcessInformation.hThread);
        return 1;
    }

    /* CreateProcess 失败 */
    DWORD LastError = GetLastError();
    CHAR Text[128];
    my_sprintf(Text, (char *)&byte_14003E348, LastError);
    MessageBoxA(0, Text, &byte_14003E360, MB_ICONERROR);
    return 0;
}

/*
 * @0x140016BE0  TabCtrlApp::waitforCmd
 *
 * 在 WinMain 主循环每次迭代中被调用。
 * - WaitForMultipleObjects(0 timeout) 探测哪个子进程退出
 * - 退出后清理 Tab、移除 hProcess 数组中该项（左移）
 * - 调用 TryReloadView 看是否能复活 view
 * - 复活失败：View_ForceClose；如剩余 view==0 则程序整体退出
 */
__int64 __fastcall TabCtrlApp_WaitForCmd(__int64 self)
{
    DWORD v2 = WaitForMultipleObjects(*(_DWORD *)self, (const HANDLE *)(self + 8), FALSE, 0);
    unsigned __int64 v3 = v2;
    if (v2 == WAIT_TIMEOUT || v2 == (DWORD)-1 || v2 >= *(_DWORD *)self)
        return 0xFFFFFFFFLL;

    /* 通过 hProcess 找对应 view slot */
    __int128 *Instance = TabsList_GetInstance();
    int v6 = TabsList_FindByProcHandle(Instance, *(_QWORD *)(self + 8 * v3 + 8));
    __int64 v7 = v6;

    char v25;
    Log_Stub(&v25,
             "TabCtrlApp::waitforCmd START and receive an event: evtIdx = %d, evtHdle = %d, clientIdx = %d\n",
             v3, *(_QWORD *)(self + 8 * v3 + 8), v6);

    if ((int)v7 < 0) return 0xFFFFFFFFLL;

    __int128 *v8 = TabsList_GetInstance();
    if ((int)v7 >= (int)((__int64)(*((_QWORD *)v8 + 1) - *(_QWORD *)v8) >> 3))
        return 0xFFFFFFFFLL;

    __int64 v9 = *(_QWORD *)(*(_QWORD *)v8 + 8 * v7);
    if (!*(_BYTE *)(v9 + 356) || !v9) return 0xFFFFFFFFLL;

    /* 在 mainframe view list 中查找 container */
    __int64 v10 = *(_QWORD *)(self + 1032);
    unsigned __int64 v11 = 0LL;
    __int64 container = *(_QWORD *)(v9 + 8);
    unsigned __int64 v13 = (*(_QWORD *)(self + 1040) - v10) >> 3;
    if (v13) {
        do {
            __int64 v14 = *(_QWORD *)(v10 + 8 * v11);
            if (v14 == container && *(_DWORD *)(v14 + 968)) break;
            ++v11;
        } while (v11 < v13);
    }

    /* 找到则尝试移除 Tab + 通知失去 view */
    if (v11 != v13 && (v11 & 0x80000000) == 0LL) {
        __int64 v15 = *(unsigned int *)(v9 + 28);
        if ((int)v15 >= 0 && *(_DWORD *)(v9 + 616) != 1) {
            View_RemoveTab(*(_QWORD *)(v9 + 8), v15);
            Sub_OnViewLost((__int64)Sub_GetSomeSingletonA(), 1, container);
        }
    }

    /* 释放 slot + 关闭句柄 + 数组左移 */
    __int128 *v17 = TabsList_GetInstance();
    TabsList_FreeSlot(v17, (unsigned)v7);
    CloseHandle(*(HANDLE *)(self + 8 * v3 + 8));

    DWORD v18 = *(_DWORD *)self;
    if (v3 < (unsigned)(*(_DWORD *)self - 1)) {
        _QWORD *v19 = (_QWORD *)(self + 8 + 8 * v3);
        unsigned __int64 v5 = (unsigned)v3;
        do {
            ++v5;
            *v19 = v19[1];
            ++v19;
            v18 = *(_DWORD *)self;
        } while (v5 < (unsigned)(*(_DWORD *)self - 1));
    }
    *(_DWORD *)self = v18 - 1;

    /* 尝试 reload；失败则清理并可能整体退出 */
    if (!(unsigned)TryReloadView(self, container)) {
        View_Deactivate(container);
        int *v20 = MainFrameSingleton_GetInstance();
        _QWORD *v21 = (_QWORD *)((_QWORD*)v20)[130];
        _QWORD *v22 = (_QWORD *)((_QWORD*)v20)[129];
        if (v22 != v21) {
            while (*v22 != container) {
                if (++v22 == v21) return (unsigned)v3;
            }
            if (*(_DWORD *)(*v22 + 968LL)) {
                __int64 *rt = RookieTipWindow_GetInstance();
                RookieTipWindow_Hide((__int64)rt);
                View_ForceClose(*v22, 0);
                if (--((int*)v20)[265 * 2] <= 0) {  /* active_view_count-- */
                    MainFrame_Cleanup((__int64)v20);
                    PostQuitMessage(0);
                    exit(0);
                }
            }
        }
    }
    return (unsigned)v3;
}

/*
 * @0x140014000  TabCtrlApp::closeAndRecycle (推断名)
 *
 * 关闭一个 view 并立即创建新的填补：
 *  - View_RemoveTab 从 Tab 控件移除
 *  - state_machine = 1 (closing)
 *  - SendMessage(0x1304) 通知子进程优雅退出
 *  - 已满则启动定时器等待
 *  - 否则 my_sprintf 标题 + viewNew 创建新视图
 */
char __fastcall TabCtrlApp_CloseAndRecycle(__int64 self, __int64 view, int new_idx, char force)
{
    /* 在 mainframe view vector 中查找该 view */
    __int64 vec_begin = *(_QWORD *)(self + 1032);
    unsigned __int64 v6 = 0LL;
    unsigned __int64 v8 = (*(_QWORD *)(self + 1040) - vec_begin) >> 3;
    if (v8) {
        do {
            __int64 v11 = *(_QWORD *)(vec_begin + 8 * v6);
            if (v11 == view && *(_DWORD *)(v11 + 968)) break;
            ++v6;
        } while (v6 < v8);
    }
    if (v6 == v8 || (DWORD)v6 == (DWORD)-1) return 0;

    /* 在 TabsList 中查找 slot */
    __int128 *Instance = TabsList_GetInstance();
    if (new_idx >= (int)((__int64)(((_QWORD*)Instance)[1] - *(_QWORD*)Instance) >> 3))
        return 0;
    __int64 slot = *(_QWORD *)(*(_QWORD *)Instance + 8 * new_idx);
    if (!*(_BYTE *)(slot + 356) || !slot) return 0;

    /* 清理状态 */
    View_RemoveTab(view, *(unsigned int *)(slot + 28));
    *(_DWORD *)(slot + 28)  = -1;
    *(_DWORD *)(slot + 616) = 1;          /* state = closing */
    *(_QWORD *)(slot + 336) = 0;
    Sub_OnViewLost((__int64)Sub_GetSomeSingletonA(), 1, view);

    /* 通知子进程优雅退出 */
    if (SendMessageA(*(HWND *)(view + 224), MHMSG_VIEW_HEARTBEAT, 0, 0)) {
        if (!force) return 1;
    } else if (!force) {
        View_ForceClose(view, 1);
        return 1;
    }

    /* 检查是否已满 */
    __int128 *v16 = TabsList_GetInstance();
    if ((int)GetActiveTabCount((__int64)v16) >= 8) {
        if (!SendMessageA(*(HWND *)(view + 224), MHMSG_VIEW_HEARTBEAT, 0, 0))
            View_ForceClose(view, 1);
        SetTimer(*(HWND *)(view + 24), 0xFA2, 1000, 0);  /* 1 秒后重试 */
        return 0;
    }

    /* 创建新视图 */
    my_sprintf(g_main_title_buf, "%s %d", off_14004E4E0, 0);
    TabCtrlApp_ViewNew((_DWORD *)self, view, NULL, NULL);
    return 1;
}

/*
 * @0x1400142A0  TabCtrlApp::endDragging
 *
 * 处理 Tab 拖拽结束。两种情况：
 *  - 拖到空白区（创建新主框架）：MainFrame_AcquireViewContainer + SetWindowPos 到鼠标位置
 *  - 拖到另一个主框架：转发到目标主框架的 Tab
 */
__int64 __fastcall TabCtrlApp_endDragging(__int64 self, __int64 dst_mainframe_hwnd, int tab_idx)
{
    /* 强制初始化 SingletonB */
    __int64 v6 = (__int64)Sub_GetSomeSingletonB();
    __int64 result = (__int64)Sub_GetSomeSingletonB_VInit(v6);

    if (*(_DWORD *)(self + 1120) != 1) return result;     /* 不在拖拽中 */

    /* === 拖到空白区：创建新主框架 === */
    if (!dst_mainframe_hwnd) {
        result = MainFrame_AcquireViewContainer(self);
        unsigned __int64 v8;
        LODWORD(v8) = result;
        if ((DWORD)result == (DWORD)-1) return result;
        __int64 v10 = *(_QWORD *)(8LL * (int)result + *(_QWORD *)(self + 1032));

        /* 把窗口放到鼠标位置 + 工作区裁剪（10% 边距） */
        __int128 pvParam = 0;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &pvParam, 0);
        int v11 = DWORD2(pvParam) - (DWORD)pvParam;
        int v12 = HIDWORD(pvParam) - DWORD1(pvParam);
        POINT Point;
        GetCursorPos(&Point);
        int x = (DWORD)pvParam;
        if (Point.x >= (int)(DWORD)pvParam) {
            x = Point.x;
            if (Point.x > DWORD2(pvParam) - v11 / 10)
                x = DWORD2(pvParam) - v11 / 10;
        }
        int y = DWORD1(pvParam);
        if (Point.y >= SDWORD1(pvParam)) {
            y = Point.y;
            if (Point.y > HIDWORD(pvParam) - v12 / 10)
                y = HIDWORD(pvParam) - v12 / 10;
        }
        SetWindowPos(*(HWND *)(v10 + 24), 0, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        (*(void(__fastcall**)(__int64, void*))(*(_QWORD*)(v10 + 200) + 112LL))(v10 + 200, &unk_14003E4B0);
        View_ForceClose(v10, 1);
        tab_idx = 0;
        goto LABEL_18;
    }

    /* === 拖到另一主框架：在 mainframe view list 中找匹配 hwnd === */
    __int64 v15 = *(_QWORD *)(self + 1032);
    unsigned __int64 v8 = 0;
    unsigned __int64 v16 = (*(_QWORD *)(self + 1040) - v15) >> 3;
    if (v16) {
        do {
            __int64 v17 = *(_QWORD *)(v15 + 8 * v8);
            if (*(_QWORD *)(v17 + 24) == dst_mainframe_hwnd && *(_DWORD *)(v17 + 968)) break;
            ++v8;
        } while (v8 < v16);
    }
    if (v8 == v16 || (v8 & 0x80000000) != 0) {
        char v20[8];
        return Log_Stub(v20, "TabCtrlApp::endDragging error, cannot find the mainframe by hwnd = %d\n",
                        dst_mainframe_hwnd);
    }

    __int64 v10 = *(_QWORD *)(v15 + 8LL * (int)v8);
    if (tab_idx < 0)
        return SendMessageA(*(HWND *)(v10 + 224), MHMSG_VIEW_HEARTBEAT, 0, 0);

LABEL_18:
    if (tab_idx < (int)SendMessageA(*(HWND *)(v10 + 224), MHMSG_VIEW_HEARTBEAT, 0, 0)) {
        __int64 prev_idx = *(int *)(self + 1100);
        *(_DWORD *)(self + 1112) = (_DWORD)v8;
        *(_DWORD *)(self + 1116) = tab_idx;

        if ((DWORD)v8 == (DWORD)prev_idx) {
            View_HoldDragging(v10, *(unsigned int *)(self + 1104), (unsigned)tab_idx);
        } else {
            unsigned int v19 = *(_DWORD *)(self + 1104);
            *(_DWORD *)(self + 1120) = 2;     /* 跨主框架转移中 */
            if (MainFrame_PostMsg(self, *(_QWORD *)(*(_QWORD *)(self + 1032) + 8 * prev_idx),
                                  v19, 1, *(_QWORD *)(*(_QWORD *)(self + 1032) + 8LL * (int)v8))
                || *(_DWORD *)(self + 1120) != 2)
            {
                char v20[8];
                return Log_Stub(v20, "TabCtrlApp::endDragging here, mainframe = %d, tabidx = %d\n",
                                dst_mainframe_hwnd, tab_idx);
            }
            TabCtrlApp_BeginDragging(self,
                *(_QWORD *)(*(_QWORD *)(self + 1032) + 8LL * *(int *)(self + 1100)),
                *(_QWORD *)(*(_QWORD *)(self + 1032) + 8LL * *(int *)(self + 1112)),
                *(_DWORD *)(self + 1104), *(_DWORD *)(self + 1116));
        }
        *(_DWORD *)(self + 1120) = 0;
        char v20[8];
        return Log_Stub(v20, "TabCtrlApp::endDragging here, mainframe = %d, tabidx = %d\n",
                        dst_mainframe_hwnd, tab_idx);
    }
    return SendMessageA(*(HWND *)(v10 + 224), MHMSG_VIEW_HEARTBEAT, 0, 0);
}

/* ============================================================
 * 应用事件回调（注册到 TabCtrlApp_AddListener）
 * ============================================================ */

/*
 * @0x140013060  TabCtrlApp event 1: InitTabList
 *
 * 首次调用时初始化两条双向循环链表：
 *  qword_14004F7F8 - 事件队列头
 *  qword_14004F808 - Tab 项链表头
 * 之后每次调用执行 List_InsertHead 把新项插到头部
 */
__int64 TabCtrlApp_OnEvent1_InitTabList(void)
{
    if (dword_14004F820 > *(_DWORD*)(*((_QWORD*)NtCurrentTeb()->ThreadLocalStoragePointer
                                      + (unsigned)TlsIndex) + 4LL))
    {
        Init_thread_header(&dword_14004F820);
        if (dword_14004F820 == -1) {
            qword_14004F7F8 = 0;
            qword_14004F800 = 0;

            /* 链表 1 sentinel：48 字节，prev/next/data 指向自己 */
            _QWORD *v1 = (_QWORD *)operator_new(0x30u);
            v1[0] = (_QWORD)v1;
            v1[1] = (_QWORD)v1;
            v1[2] = (_QWORD)v1;
            ((_WORD *)v1)[12] = 257;
            qword_14004F7F8 = (__int64)v1;

            qword_14004F808 = 0;
            qword_14004F810 = 0;

            /* 链表 2 sentinel */
            _QWORD *v2 = (_QWORD *)operator_new(0x30u);
            v2[0] = (_QWORD)v2;
            v2[1] = (_QWORD)v2;
            v2[2] = (_QWORD)v2;
            ((_WORD *)v2)[12] = 257;
            qword_14004F808 = (__int64)v2;
            byte_14004F818 = 0;

            atexit(sub_14003B290);
            Init_thread_footer(&dword_14004F820);
        }
    }
    List_InsertHead((__int64 **)&qword_14004F7F8);
    return 1;
}

/*
 * @0x140015390  TabCtrlApp event 2: TimerPoll
 * 每个定时器节拍调用：遍历所有 active view，更新其计时器
 * 子对象 vtable[88/8] 返回 0 时取消订阅
 */
__int64 TabCtrlApp_OnEvent2_TimerPoll(void)
{
    unsigned __int64 v0 = 0;
    __int128 *Instance = TabsList_GetInstance();
    if ((__int64)(((_QWORD*)Instance)[1] - *(_QWORD*)Instance) >> 3) {
        do {
            __int128 *v2 = TabsList_GetInstance();
            if ((int)v0 < (int)((__int64)(((_QWORD*)v2)[1] - *(_QWORD*)v2) >> 3)) {
                __int64 view = *(_QWORD *)(*(_QWORD *)v2 + 8LL * (int)v0);
                if (*(_BYTE *)(view + 356) && view) {
                    TabView_PollTick(view + 288);
                    __int64 container = *(_QWORD *)(view + 8);
                    if (*(_DWORD *)(container + 1000) == *(_DWORD *)(view + 28)
                        && !(*(unsigned __int8(__fastcall **)(__int64))
                                (*(_QWORD *)(container + 1984) + 88LL))(container + 1984))
                    {
                        TabCtrlApp_RemoveListener(container, -1.0f);
                    }
                }
            }
            ++v0;
            __int128 *v5 = TabsList_GetInstance();
        } while (v0 < (__int64)(((_QWORD*)v5)[1] - *(_QWORD*)v5) >> 3);
    }
    return 1;
}

/*
 * @0x140013980  TabCtrlApp event 80: OnReady (主程序就绪)
 *
 * 在 global_ready_flag==0 时执行（首次就绪）
 *  - 触发 TabCtrlApp_endDragging 取消任何遗留拖拽
 *  - 检查所有 view 是否仍然存活（SendMessage 0x1304 心跳）
 *  - 死亡的 view 走 TryReloadView 复活流程
 *  - 复活失败 + 是最后一个 view → exit(0)
 */
__int64 TabCtrlApp_OnEvent80_OnReady(void)
{
    /* 已就绪则跳过 */
    if (((_BYTE *)MainFrameSingleton_GetInstance())[1096] == 1) return 1;

    /* 索引 280 = 偏移 1120 = drag_state */
    if (MainFrameSingleton_GetInstance()[280]) {
        /* drag_state == 1: 找一个 ready 的 view 然后取消拖拽状态 */
        __int64 v0 = ((_QWORD *)MainFrameSingleton_GetInstance())[129];
        if (v0 == ((_QWORD *)MainFrameSingleton_GetInstance())[130]) {
LABEL_6:
            TabCtrlApp_endDragging((__int64)MainFrameSingleton_GetInstance(), 0, 0);
        } else {
            while (!*(_BYTE *)(*(_QWORD *)v0 + 600LL)) {
                v0 += 8LL;
                if (v0 == ((_QWORD *)MainFrameSingleton_GetInstance())[130])
                    goto LABEL_6;
            }
        }
        return 1;
    }

    /* 配置已加载则触发 OnConfigLoaded */
    if (MainFrameSingleton_GetInstance()[287]) {
        TabCtrlApp_OnConfigLoaded(MainFrameSingleton_GetInstance());
    }

    /* === 遍历每个 view 健康检查 === */
    __int64 *v4 = (__int64 *)((_QWORD*)MainFrameSingleton_GetInstance())[129];
    _DWORD *v5 = (_DWORD *)(*((_QWORD*)NtCurrentTeb()->ThreadLocalStoragePointer
                              + (unsigned)TlsIndex) + 4LL);
    while (1) {
LABEL_11:
        /* 嵌套的某个状态对象 thread-safe 初始化 */
        if (dword_140050338 > *v5) {
            Init_thread_header(&dword_140050338);
            if (dword_140050338 == -1) {
                qword_1400502B8 = 0;
                xmmword_1400502C0 = 0;
                dword_1400502FC = 0;
                xmmword_140050300 = 0;
                dword_140050310 = 0;
                qword_140050318 = 0;
                qword_140050320 = 0;
                _QWORD *v12 = (_QWORD *)operator_new(0xA0u);
                v12[0] = (_QWORD)v12;
                v12[1] = (_QWORD)v12;
                v12[2] = (_QWORD)v12;
                ((_WORD *)v12)[12] = 257;
                qword_140050318 = (__int64)v12;
                xmmword_1400502D0 = (__int128)_mm_load_si128((const __m128i *)&xmmword_14003E6F0);
                dword_1400502E0 = 800;
                dword_1400502E4 = 600;
                dword_1400502E8 = dword_14004E038;
                byte_1400502EC = 1;
                qword_1400502F0 = 0;
                *(int*)&g_MainFrame_instance = 0;
                byte_1400502F8 = 0;
                qword_140050328 = 0;
                dword_140050330 = 10;
                memset_kr(&unk_14004FEB8, 0, 0x400);
                atexit(sub_14003B3E0);
                Init_thread_footer(&dword_140050338);
            }
        }

        /* 遍历到末端则退出循环 */
        if (v4 == (__int64 *)xmmword_1400502C0) break;

        __int64 view = *v4;
        if (*(_DWORD *)(*v4 + 968)) {
            /* 命令队列空 + 心跳无响应 = 视为子进程死亡 */
            if (((*(_QWORD *)(view + 984) - *(_QWORD *)(view + 976)) & 0xFFFFFFFFFFFFFFF8uLL) == 0
                && !(unsigned)SendMessageA(*(HWND *)(view + 224), MHMSG_VIEW_HEARTBEAT, 0, 0))
            {
                if (!(unsigned)TryReloadView((__int64)MainFrameSingleton_GetInstance(), *v4)) {
                    int *v8 = MainFrameSingleton_GetInstance();
                    __int64 *vec_end   = (__int64 *)((_QWORD *)v8)[130];
                    __int64 *vec_begin = (__int64 *)((_QWORD *)v8)[129];
                    if (vec_begin != vec_end) {
                        while (*vec_begin != *v4) {
                            if (++vec_begin == vec_end) { ++v4; goto LABEL_11; }
                        }
                        if (*(_DWORD *)(*vec_begin + 968)) {
                            __int64 *rt = RookieTipWindow_GetInstance();
                            RookieTipWindow_Hide((__int64)rt);
                            View_ForceClose(*vec_begin, 0);
                            if (--v8[265] <= 0) {
                                MainFrame_Cleanup((__int64)v8);
                                PostQuitMessage(0);
                                exit(0);
                            }
                        }
                    }
                }
            }
        }
        ++v4;
    }
    return 1;
}
