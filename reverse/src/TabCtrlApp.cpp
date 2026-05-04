/**
 * mhtab.exe 还原 - TabCtrlApp 应用主类
 *
 * 该类是程序的"控制器"层：
 *  - 管理事件订阅链表（10 容量预分配，链表节点）
 *  - 持有 MainFrame 引用
 *  - 负责创建/销毁/切换 view（每个 view 是一个 my.exe 子进程）
 */

#include "include/globals.h"

/* ============================================================
 * 单例
 * ============================================================ */

/*
 * @0x1400087F0  TabCtrlApp::getInstance
 * 双重检查锁定（C++11 magic statics）的 thread_safe 单例
 *
 * 单例对象 g_TabCtrlApp_instance 内部布局:
 *   +0x00 (dword) : 10        - 监听器初始容量
 *   +0x04 (qword) : 100       - 容量上限提示
 *   +0x14 (xmm)   : 双向循环链表 1 头 (sentinel)
 *   +0x2C (xmm)   : 双向循环链表 2 头 (sentinel) [Block]
 */
int* TabCtrlApp_GetInstance(void)
{
    /* TLS 优化：已初始化则快速返回 */
    if (dword_14004FE70 <= *(_DWORD*)(*((_QWORD*)NtCurrentTeb()->ThreadLocalStoragePointer
                                       + (unsigned int)TlsIndex) + 4LL))
        return &g_TabCtrlApp_instance;

    Init_thread_header(&dword_14004FE70);
    if (dword_14004FE70 != -1) return &g_TabCtrlApp_instance;

    /* 首次构造 */
    g_TabCtrlApp_instance = 10;
    qword_14004FE3C = 100LL;
    qword_14004FE44 = 0LL;
    *(_OWORD*)&xmmword_14004FE50 = 0LL;

    /* 监听器链表 1 sentinel */
    _QWORD* v1 = (_QWORD*)operator_new(0x18u);
    v1[0] = (_QWORD)v1;          /* prev = self */
    v1[1] = (_QWORD)v1;          /* next = self */
    xmmword_14004FE50 = v1;

    /* 监听器链表 2 sentinel */
    *(_OWORD*)&Block = 0LL;
    _QWORD* v2 = (_QWORD*)operator_new(0x18u);
    v2[0] = (_QWORD)v2;
    v2[1] = (_QWORD)v2;
    Block = v2;

    atexit(sub_14003B2B0);       /* 析构注册 */
    Init_thread_footer(&dword_14004FE70);
    return &g_TabCtrlApp_instance;
}

/*
 * @0x1400088C0  MainFrameSingleton::getInstance
 */
int* MainFrameSingleton_GetInstance(void)
{
    if (dword_140050338 <= *(_DWORD*)(*((_QWORD*)NtCurrentTeb()->ThreadLocalStoragePointer
                                       + (unsigned int)TlsIndex) + 4LL))
        return (int*)&g_MainFrame_instance;

    Init_thread_header(&dword_140050338);
    if (dword_140050338 != -1) return (int*)&g_MainFrame_instance;

    MainFrameSingleton_Init((__int64)&g_MainFrame_instance);
    atexit(sub_14003B3E0);
    Init_thread_footer(&dword_140050338);
    return (int*)&g_MainFrame_instance;
}

/*
 * @0x140018460  TabsList::getInstance
 *
 * g_TabsList 实际是 std::vector<ViewSlot*> 的 16 字节头：
 *   {ViewSlot** begin; ViewSlot** end_or_capacity; ...}
 */
__int128* TabsList_GetInstance(void)
{
    if (dword_1400519D0 <= *(_DWORD*)(*((_QWORD*)NtCurrentTeb()->ThreadLocalStoragePointer
                                       + (unsigned int)TlsIndex) + 4LL))
        return &g_TabsList;

    Init_thread_header(&dword_1400519D0);
    if (dword_1400519D0 != -1) return &g_TabsList;

    atexit(sub_14003B4A0);
    Init_thread_footer(&dword_1400519D0);
    return &g_TabsList;
}

/* ============================================================
 * 事件总线（订阅 / 取消）
 * ============================================================ */

/*
 * @0x140008B90  TabCtrlApp::addListener
 *
 * 节点结构 (24 字节)：
 *   +0  callback
 *   +8  arg
 *   +16 event_id
 *
 * 链表条目 (24 字节)：
 *   +0  prev
 *   +8  next
 *   +16 data_ptr
 */
_QWORD* TabCtrlApp_AddListener(__int64 self, __int64 callback, __int64 arg, int event_id)
{
    _QWORD* v8 = (_QWORD*)operator_new(0x18u);
    _QWORD* v9 = v8;
    if (v8) {
        v8[0] = callback;
        v8[1] = arg;
        ((_DWORD*)v8)[4] = event_id;
    } else {
        v9 = 0;
    }

    __int64 list_head = *(_QWORD*)(self + 40);
    if (*(_QWORD*)(self + 48) == 0xAAAAAAAAAAAAAAALL)
        sub_140019684("list too long");

    _QWORD* result = (_QWORD*)operator_new(0x18u);
    result[2] = (_QWORD)v9;
    ++*(_QWORD*)(self + 48);

    /* 插入到 list_head 之前（双向循环链表） */
    _QWORD* tail = *(_QWORD**)(list_head + 8);
    result[0] = list_head;
    result[1] = (_QWORD)tail;
    *(_QWORD*)(list_head + 8) = (_QWORD)result;
    *tail = (_QWORD)result;
    return result;
}

/*
 * @0x140008AE0  TabCtrlApp::removeListener
 * 通过 view 索引 + wait_sec 取消订阅
 */
__int64 TabCtrlApp_RemoveListener(__int64 self, float wait_sec)
{
    __int64 v2 = *(int*)(self + 1000);
    __int64 v3 = self;
    float    v4 = 0.0f;

    if (wait_sec >= 0.0f) {
        v4 = wait_sec;
        if ((int)v2 < 0) goto LABEL_7;
    } else {
        if ((int)v2 < 0) goto LABEL_7;
        __int64 v5 = *(_QWORD*)(*(_QWORD*)(self + 976) + 8 * v2);
        LODWORD(v2) = *(_DWORD*)(v3 + 1000);
        v4 = *(float*)(v5 + 288);
    }

    if (!*(_BYTE*)(*(_QWORD*)(*(_QWORD*)(v3 + 976) + 8LL * (unsigned)v2) + 296LL)) {
        __int64 v6 = v3 + 1984;
        return (*(__int64(__fastcall**)(__int64, __int64))(*(_QWORD*)v6 + 104LL))(v6, 1);
    }

LABEL_7: ;
    __int64 v8 = v3 + 1984;
    __int64 result = (*(__int64(__fastcall**)(__int64))(*(_QWORD*)(v3 + 1984) + 88LL))(v3 + 1984);
    if (!(_BYTE)result)
        return sub_14000A880(v8, (unsigned)(int)(float)(v4 * 1000.0f));
    return result;
}

/* ============================================================
 * 应用初始化
 * ============================================================ */

/*
 * @0x140014E50  TabCtrlApp::init
 *
 * 在 WinMain 中由首启分支调用。完成：
 *  1. MainFrame 对象初始化（保存 hInstance、init 上下文）
 *  2. 订阅 3 个应用事件
 *  3. 注册主窗口类（MHXYMainFrame + MD5(workdir)hex）
 *  4. 注册 13 个窗口消息处理器
 *  5. TabCtrlApp_NewTabFormatted 创建第一个 Tab（首页）
 *  6. RookieTipWindow 初始化 + RookieGuide 加载
 */
char TabCtrlApp_Init(__int64 mainframe, __int64 hInstance, __int128* init_ctx,
                     char init_byte, int arg5, unsigned int arg6, _BYTE* arg7)
{
    __int128 v7 = *init_ctx;

    /* this+1064/1084/1088/1096 字段初始化 */
    *(_QWORD*)(mainframe + 1088) = hInstance;
    *(_OWORD*)(mainframe + 1064) = v7;
    *(_BYTE*)(mainframe + 1084)  = init_byte;
    *(_BYTE*)(mainframe + 1096)  = 0;            /* global_ready_flag = false */

    /* 静态局部对象的双重检查锁定（线程安全初始化）*/
    _QWORD* tlsPtr = (_QWORD*)NtCurrentTeb()->ThreadLocalStoragePointer;
    __int64 tlsIdx = (unsigned int)TlsIndex;
    if (dword_14004FEA0 > *(_DWORD*)(tlsPtr[tlsIdx] + 4LL)) {
        Init_thread_header(&dword_14004FEA0);
        if (dword_14004FEA0 == -1) {
            qword_14004FE98 = 0;
            atexit(sub_14003B3F0);
            Init_thread_footer(&dword_14004FEA0);
        }
    }
    sub_140017110(&qword_14004FE98);

    /* 取得 TabCtrlApp 单例并预分配监听器列表 */
    int* app = TabCtrlApp_GetInstance();
    sub_1400054B0(app, 40LL, 1);

    /* === 订阅 3 个应用事件 === */
    TabCtrlApp_AddListener((__int64)TabCtrlApp_GetInstance(),
                           (__int64)TabCtrlApp_OnEvent1_InitTabList, 0, APP_EVENT_INIT_TAB_LIST);
    TabCtrlApp_AddListener((__int64)TabCtrlApp_GetInstance(),
                           (__int64)TabCtrlApp_OnEvent2_TimerPoll,    0, APP_EVENT_TIMER_POLL);
    TabCtrlApp_AddListener((__int64)TabCtrlApp_GetInstance(),
                           (__int64)TabCtrlApp_OnEvent80_OnReady,     0, APP_EVENT_ON_READY);

    /* 调用 SingletonB.vtable[1]() 触发某些子系统的二次初始化 */
    __int64 sgB = (__int64)Sub_GetSomeSingletonB();
    (*(void(__fastcall**)(__int64, __int64, _QWORD))(*(_QWORD*)sgB + 8LL))(sgB, hInstance, 0);

    /* === 构造主窗口类名 "MHXYMainFrame" + UID(MD5 hex) === */
    char Source[48];
    char Destination[128];
    if (GetUniqueIdSuffix(Source, 48)) {
        strncpy(Destination, "MHXYMainFrame", 0x80);
        int len = lstrlenA("MHXYMainFrame");
        if (len < 128)
            strncpy(&Destination[len], Source, 128LL - len);
    }
    MainFrame_SaveClassName(Destination);
    g_workdir_instance_id = GetWorkdirInstanceID();

    /* 主窗口对象内部 1024 字节 buffer 清零 */
    memset_kr((void*)(mainframe + 8), 0, 0x400);

    /* === 注册 13 个 Win32 自定义消息处理器 === */
    MainFrame_RegisterMsgHandler(MHMSG_NEW_CLIENT,         (__int64)Dispatch_NewClient_msg1);
    MainFrame_RegisterMsgHandler(MHMSG_READY_CONFIRM,      (__int64)Dispatch_OnReadyConfirm_msg3);
    MainFrame_RegisterMsgHandler(MHMSG_FORWARD_INPUT,      (__int64)MainFrame_OnMsg4_ForwardInput);
    MainFrame_RegisterMsgHandler(MHMSG_UPDATE_POS,         (__int64)Dispatch_UpdatePos_msg6);
    MainFrame_RegisterMsgHandler(MHMSG_ACTIVATE_VIEW,      (__int64)Dispatch_ActivateView_msg7);
    MainFrame_RegisterMsgHandler(MHMSG_NEW_VIEW,           (__int64)MainFrame_OnMsg16_NewView);
    MainFrame_RegisterMsgHandler(MHMSG_HIDE_VIEW,          (__int64)Dispatch_HideView_msg8);
    MainFrame_RegisterMsgHandler(MHMSG_CLEANUP_VIEW,       (__int64)Dispatch_CleanupView_msg25);
    MainFrame_RegisterMsgHandler(MHMSG_SHOW_WINDOW,        (__int64)Dispatch_ShowWindow_msg26);
    MainFrame_RegisterMsgHandler(MHMSG_GET_BY_PARENT_HWND, (__int64)Dispatch_GetByParentHwnd_msg17);
    MainFrame_RegisterMsgHandler(MHMSG_FIND_BY_HWND,       (__int64)Dispatch_FindByHwnd_msg4104);
    MainFrame_RegisterMsgHandler(MHMSG_ACTIVATE,           (__int64)Dispatch_Activate_msg4102);
    MainFrame_RegisterMsgHandler(MHMSG_BRING_TO_FRONT,     (__int64)Dispatch_BringToFront_msg4103);

    /* === 创建首页 Tab + 启动子进程 === */
    if (TabCtrlApp_NewTabFormatted(mainframe, 0, arg5, arg6, arg7)) {
        /* 创建新手提示窗口 */
        __int64* rt = RookieTipWindow_GetInstance();
        RookieTipWindow_Init((LONG_PTR)rt, hInstance,
                             *(_QWORD*)(**(_QWORD**)(mainframe + 1032) + 24LL));

        /* 加载新手引导持久化状态 */
        int* sgA = (int*)Sub_GetSomeSingletonA();
        RookieGuide_loadState((_DWORD*)sgA);
        return 1;
    }

    MessageBoxA(0, &byte_14003D958, &byte_14003D948, 0x40000u);
    return 0;
}
