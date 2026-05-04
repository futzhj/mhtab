/**
 * mhtab.exe 还原 - 全局变量与函数声明
 */

#ifndef MHTAB_GLOBALS_H
#define MHTAB_GLOBALS_H

#include "types.h"

/* ============================================================
 * 全局变量
 * ============================================================ */

/* 单例实例（实际是单例对象的存储区，不是指针） */
extern MainFrame      g_MainFrame_instance;       /* 0x14004FEB0 */
extern int            g_TabCtrlApp_instance;      /* 0x14004FE38 - dword*/
extern __m128i        g_TabsList;                 /* 0x1400519B0 - vector头 16字节 */

/* 工作目录与实例 ID */
extern int            g_workdir_instance_id;      /* 0x14004FE8C */
extern int            g_workdir_hash_cache;       /* 0x140050BF8 */
extern char           g_workdir_buf[1024];        /* 0x1400515A0 */

/* 窗口类与标题缓冲 */
extern char           g_mainframe_class_name[];   /* 0x14004E120 - "MHXYMainFrame{UID}" */
extern char           g_main_title_buf[];         /* 0x140051110 - Tab 标题缓冲 */

/* 命令行参数解析用 */
extern const char*    off_14004E4D0;              /* 子进程程序名 (my.exe) */
extern const char*    off_14004E4E0;              /* 命令行类型1 标识 */
extern const char*    off_14004E4E8;              /* 命令行类型2 标识 */
extern const char*    off_14004E4F0;              /* 命令行类型3 标识 */

/* ============================================================
 * 函数声明 - 主入口
 * ============================================================ */
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nShowCmd);

/* ============================================================
 * TabCtrlApp 单例 + 事件总线
 * ============================================================ */
int*       TabCtrlApp_GetInstance(void);
_QWORD*    TabCtrlApp_AddListener(__int64 self, __int64 callback, __int64 arg, int event_id);
__int64    TabCtrlApp_RemoveListener(__int64 self, float wait_sec);
char       TabCtrlApp_Init(__int64 mainframe, __int64 hInstance, __m128i* init_ctx,
                           char init_byte, int arg5, unsigned int arg6, _BYTE* arg7);
int*       MainFrameSingleton_GetInstance(void);
__int128*  TabsList_GetInstance(void);

/* TabCtrlApp 业务方法 */
char       TabCtrlApp_ViewNew(_DWORD* mainframe, __int64 view_in);
char       TabCtrlApp_NewTabFormatted(__int64 mainframe, __int64 a2, int a3, __int64 a4, _BYTE* a5);
__int64    TabCtrlApp_WaitForCmd(__int64 mainframe);
char       TabCtrlApp_CloseAndRecycle(__int64 self, __int64 view, int new_idx, char force);
__int64    TabCtrlApp_endDragging(__int64 self, __int64 dst_mainframe_hwnd, int tab_idx);
void       TabCtrlApp_BeginDragging(__int64 self, __int64 src_view, __int64 dst_view, int from_idx, int to_idx);
__int64    TabCtrlApp_OnEvent1_InitTabList(void);
__int64    TabCtrlApp_OnEvent2_TimerPoll(void);
__int64    TabCtrlApp_OnEvent80_OnReady(void);

/* ============================================================
 * MainFrame 主窗口
 * ============================================================ */
LRESULT __fastcall MainFrame_WndProc_thunk(HWND hwnd, UINT msg, WPARAM wp, LONG_PTR* lp);
LRESULT __fastcall MainFrame_WndProc(__int64 self, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void               MainFrame_RegisterMsgHandler(unsigned msg, __int64 handler);
__int64            MainFrame_PostMsg(__int64 self, _QWORD a, _DWORD b, char c, _QWORD d);
char*              MainFrame_SaveClassName(char* src);
__int64            MainFrame_AcquireViewContainer(__int64 self);
__int64            MainFrame_Cleanup(__int64 self);

/* ============================================================
 * 13 个 Dispatch 消息处理器（参数: __int64 mainframe, __int64 msg_arg）
 * ============================================================ */
__int64 Dispatch_NewClient_msg1     (__int64 mf, __int64 arg);  /* 子进程注册 */
__int64 Dispatch_OnReadyConfirm_msg3(__int64 mf, __int64 arg);
__int64 MainFrame_OnMsg4_ForwardInput(__int64 mf, __int64 arg);  /* 转发键鼠 */
__int64 Dispatch_UpdatePos_msg6     (__int64 mf, __int64 arg);
__int64 Dispatch_ActivateView_msg7  (__int64 mf, __int64 arg);
__int64 Dispatch_HideView_msg8      (__int64 mf, __int64 arg);
__int64 MainFrame_OnMsg16_NewView   (__int64 mf, __int64 arg);
__int64 Dispatch_GetByParentHwnd_msg17(__int64 mf, __int64 arg);
__int64 Dispatch_CleanupView_msg25  (__int64 mf, __int64 arg);
__int64 Dispatch_ShowWindow_msg26   (__int64 mf, __int64 arg);
__int64 Dispatch_Activate_msg4102   (__int64 mf, __int64 arg);
__int64 Dispatch_BringToFront_msg4103(__int64 mf, __int64 arg);  /* AttachThreadInput */
__int64 Dispatch_FindByHwnd_msg4104 (__int64 mf, __int64 arg);

/* ============================================================
 * View 操作
 * ============================================================ */
__int64 View_Activate     (__int64 container, int view_id, __int64 a3);
void    View_Deactivate   (__int64 view);
void    View_ForceClose   (__int64 view, __int64 mode);
void    View_RemoveTab    (__int64 container, unsigned view_id);
void    View_HoldDragging (__int64 view, unsigned a2, unsigned a3);
__int64 View_TimerExpired (__int64 timer_obj);
__int64 TabView_PollTick  (__int64 timer_obj);
__int64 TabCtrl_SwitchTab (__int64 ctx, int view_id, __int64 a3);
__int64 TryReloadView     (__int64 mainframe, __int64 view);
__int64 ForwardInputToChildWnd(__int64 container, ...);
__int64 Sub_OnViewLost    (__int64 singleton, __int64 a2, __int64 container);

/* ============================================================
 * TabsList 槽位管理
 * ============================================================ */
_QWORD*  TabsList_AllocSlot       (__int64* tabs_list, _DWORD* out_idx);
__int64  TabsList_FreeSlot        (__int64 tabs_list, unsigned idx);
__int64  TabsList_FindByProcHandle(__int64 tabs_list, HANDLE hProc);

/* ViewContainer 初始化 */
__int64  View_Container_Init      (__int64 obj, HINSTANCE hInst, _QWORD a3, unsigned idx, __int64 ctx);
__int64  View_Container_Setup     (void* obj);

/* ============================================================
 * RookieTip / RookieGuide
 * ============================================================ */
__int64* RookieTipWindow_GetInstance(void);
LRESULT  RookieTipWindow_Init       (LONG_PTR self, __int64 hInst, __int64 parent_hwnd);
LRESULT  RookieTipWindow_NewWndProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void     RookieTipWindow_Hide       (__int64 self);
void     RookieGuide_loadState      (_DWORD* dst);

/* ============================================================
 * Hash / 类名构造
 * ============================================================ */
unsigned   Hash_StringToU32        (uint8_t* str, uint8_t* unused);
int        GetWorkdirInstanceID   (void);
char       GetUniqueIdSuffix      (char* dst, int dst_size);
char       MakeMHXYWinMgrClassName(char* dst, int dst_size);

/* ============================================================
 * INI / 字符串工具
 * ============================================================ */
__m128i*   get_ini      (__m128i* result, _QWORD* workdir, const char* section,
                         const char* key, const char* defaultVal);
int        my_sprintf   (char* buf, const char* fmt, ...);
int        my_sscanf    (char* buf, char* fmt, ...);
int        Calc_30s_Bucket(int x);
__int64    Log_Stub     (_QWORD a, const char* fmt, ...);
__int64    GetActiveTabCount(__int64 tabs_list);

/* ============================================================
 * UI 渲染
 * ============================================================ */
void TabBarPlus_doDragging(__int64* self);
char Skin_DrawBitmapEx    (__int64 obj, HDC hdc, void** src, uint64_t* a4, int a5, UINT* a6);

#endif /* MHTAB_GLOBALS_H */
