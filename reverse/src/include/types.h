/**
 * mhtab.exe 还原 - 类型定义
 *
 * 由于原程序是 C++ 编译产物，这些"结构体"是通过观察偏移使用模式
 * 反推出来的，并非原作者的真实定义；字段名是逆向命名。
 */

#ifndef MHTAB_TYPES_H
#define MHTAB_TYPES_H

#include <windows.h>
#include <stdint.h>

/* IDA 类型别名 */
typedef int64_t  __int64;
typedef int32_t  __int32;
typedef int16_t  __int16;
typedef int8_t   __int8;
typedef uint64_t _QWORD;
typedef uint32_t _DWORD;
typedef uint16_t _WORD;
typedef uint8_t  _BYTE;

/* ============================================================
 * View slot 结构 (0x278 = 632 字节)
 *
 * 由 TabsList_AllocSlot @0x1400181F0 创建，存放在 g_TabsList vector 中
 * ============================================================ */
typedef struct ViewSlot {
    /* +0x000 */ int32_t  view_id;            /* -1 = 空闲 */
    /* +0x004 */ uint8_t  pad0[4];
    /* +0x008 */ struct ViewContainer* container;  /* 指向 0xB50 字节大对象 */
    /* +0x010 */ HWND     container_hwnd;
    /* +0x018 */ int32_t  tab_index;          /* -1 = 未分配到 Tab 控件 */
    /* +0x01C */ int32_t  tab_id;
    /* +0x020 */ uint8_t  mouse_down_flag;
    /* +0x021 */ uint8_t  pad1[3];
    /* +0x024 */ int32_t  pad2;
    /* +0x100 */ int32_t  mouse_y;            /* 偏移 +260 */
    /* +0x104 */ int32_t  mouse_x;            /* 偏移 +264 */
    /* +0x108 */ int32_t  mouse_z;            /* 偏移 +268 */
    /* +0x110 */ int32_t  pad3;               /* +272 */
    /* +0x120 */ uint8_t  poll_timer_obj[48]; /* +288 起 - View_TimerExpired 使用 */
    /* +0x150 */ HWND     game_hwnd;          /* +336 - 子进程游戏主窗口 */
    /* +0x158 */ HANDLE   hProcess;           /* +344 - 子进程 hProcess */
    /* +0x160 */ int32_t  serial;             /* +352 */
    /* +0x164 */ uint8_t  active_flag;        /* +356 - 核心 active 标志 */
    /* +0x258 */ uint8_t  ready_flag;         /* +600 */
    /* +0x265 */ uint8_t  on_ready_confirmed; /* +613 */
    /* +0x268 */ int32_t  state_machine;      /* +616: 1=closing, 4=cleanup */
    /* +0x26C */ int32_t  bucket_30s;         /* +620: Calc_30s_Bucket 结果 */
    /* +0x270 */ DWORD    pid;                /* +624 - 子进程 PID */
} ViewSlot;

/* ============================================================
 * ViewContainer 结构 (0xB50 = 2896 字节)
 *
 * 真正承载 Tab 显示内容的对象，每个 view 一个
 * 由 MainFrame_AcquireViewContainer @0x140015910 创建
 * ============================================================ */
typedef struct ViewContainer {
    /* +0x000 */ uint8_t  data_head[0xC8];    /* 内部状态 */
    /* +0x018 */ HWND     hwnd_inner;         /* +24 */
    /* +0x0E0 */ HWND     hwnd_owner;         /* +224 - 接收 0x1304 心跳消息的窗口 */
    /* +0x3C8 */ uint32_t poll_id;            /* +968 - reload 标识，0=失效 */
    /* +0x3E8 */ uint8_t* cmd_queue_begin;    /* +976 */
    /* +0x3F0 */ uint8_t* cmd_queue_end;      /* +984 */
    /* +0x435 */ uint8_t  flag_1077;          /* +1077 */
    /* +0x436 */ uint8_t  flag_1078;          /* +1078 - 不处理 SW_MINIMIZE */
    /* +0x7C0 */ void*    vtable_subobj;      /* +1984 - 子对象 vtable */
    /* ... 其余字段 ... */
} ViewContainer;

/* ============================================================
 * MainFrame 单例 (≥1148 字节)
 *
 * 由 MainFrameSingleton_Init @0x1400128B0 创建
 * ============================================================ */
typedef struct MainFrame {
    /* +0x000 */ uint8_t       reserved[8];
    /* +0x008 */ uint8_t       buf1024[0x400];     /* TabCtrlApp_Init 中清零 */
    /* +0x408 */ ViewSlot**    view_vec_begin;     /* +1032 */
    /* +0x410 */ ViewSlot**    view_vec_end;       /* +1040 */
    /* +0x418 */ ViewSlot**    view_vec_capacity;  /* +1048 */
    /* +0x420 */ int32_t       last_view_idx;      /* +1056 */
    /* +0x424 */ int32_t       active_view_count;  /* +1060 - 归零则退出 */
    /* +0x428 */ __m128i       init_xmm0;          /* +1064 - WinMain 传入 */
    /* +0x438 */ uint8_t       init_byte;          /* +1084 */
    /* +0x440 */ HINSTANCE     hInstance;          /* +1088 */
    /* +0x448 */ uint8_t       global_ready_flag;  /* +1096 */
    /* +0x460 */ int32_t       drag_state;         /* +1120: 0=none, 1=dragging, 2=transferring */
    /* +0x47C */ int32_t       config_loaded_flag; /* +1148 - 索引 287*4 */
} MainFrame;

/* ============================================================
 * 自定义消息号（mhtab.exe 内部协议）
 * ============================================================ */
#define MHMSG_NEW_CLIENT          0x01   /* my.exe 子进程注册 */
#define MHMSG_READY_CONFIRM       0x03
#define MHMSG_FORWARD_INPUT       0x04
#define MHMSG_UPDATE_POS          0x06
#define MHMSG_ACTIVATE_VIEW       0x07
#define MHMSG_HIDE_VIEW           0x08
#define MHMSG_NEW_VIEW            0x10   /* 16 */
#define MHMSG_GET_BY_PARENT_HWND  0x11   /* 17 */
#define MHMSG_CLEANUP_VIEW        0x19   /* 25 */
#define MHMSG_SHOW_WINDOW         0x1A   /* 26 */
#define MHMSG_ACTIVATE            0x1006 /* 4102 */
#define MHMSG_BRING_TO_FRONT      0x1007 /* 4103 */
#define MHMSG_FIND_BY_HWND        0x1008 /* 4104 */

/* ViewContainer 心跳消息 */
#define MHMSG_VIEW_HEARTBEAT      0x1304

/* 主进程间 IPC（同目录的另一启动） */
#define MHMSG_QUERY_INSTANCE_ID   0x8102 /* 返回 g_workdir_instance_id */
#define MHMSG_QUERY_STATE         0x8101
#define MHMSG_GENERIC_NOTIFY      0x8029
#define MHMSG_WM_COPYDATA         0x004A /* WM_COPYDATA - 命令行/录像参数转递 */

/* ============================================================
 * 应用事件 ID（TabCtrlApp_AddListener 用）
 * ============================================================ */
#define APP_EVENT_INIT_TAB_LIST   1
#define APP_EVENT_TIMER_POLL      2
#define APP_EVENT_ON_READY        80

#endif /* MHTAB_TYPES_H */
