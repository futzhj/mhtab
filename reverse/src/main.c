/**
 * mhtab.exe 还原 - 主入口
 *
 * 来源: WinMain @0x140017340 (1719 字节, 65 个基本块)
 * 业务: 梦幻西游多开 Tab 管理器主入口
 */

#include "include/globals.h"

/*
 * WinMain - 程序主入口
 *
 * 1. strlwr 命令行 + 检查 .war/.rec/.mh 录像扩展名
 *    - 如是录像文件，CreateProcessA 启动子进程播放录像后退出
 * 2. 解析非录像命令行：可能含 off_14004E4E0/E4E8/E4F0 三种类型标识 + 数字 + 字符串
 * 3. 构造命名互斥锁 'MHXYWinMgr'+UID 实现单实例
 * 4. 若已有实例：FindWindowExA 找 MHXYMainFrame 窗口，验证同 workdir 后转发命令
 *    - SendMessageTimeoutA(WM_COPYDATA=0x4A) 传递命令行字符串
 *    - 或 SendMessageA(0x8029) 通知激活
 * 5. 否则：创建 MainFrame 单例，调用 TabCtrlApp::init
 * 6. SetTimer + GetMessage/Translate/Dispatch 主消息循环
 * 7. 每次循环调用 TabCtrlApp_WaitForCmd（监控子进程退出）
 */
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    unsigned int v5;
    char *v6;
    int v7;
    HWND Window;
    int WorkdirInstanceID;
    int v11, v12, v13;
    LRESULT v14;
    const CHAR *v15, *v16;
    int *Instance;
    UINT_PTR v18;
    BOOL MessageA;
    __int64 v20;
    int *v21;
    unsigned __int64 v23;
    size_t v24;
    __int64 v25;
    LPSTR *v26;
    __int64 v27;
    LPSTR *v28;
    char *v29;
    CHAR *v30;
    DWORD LastError;
    LPSTR v32;
    unsigned __int8 v33;
    int v34;
    struct _PROCESS_INFORMATION lParam;
    HINSTANCE v36;
    tagMSG Msg;
    struct _STARTUPINFOA StartupInfo;
    LPSTR lpCommandLine[2];
    __int64 v40;
    unsigned __int64 v41;
    __int128 v42;
    int v43;
    char v44;
    char Destination[523];
    char v46;
    CHAR Text[128];
    CHAR Name[256];
    char v49[512];

    v36 = hInstance;

    /* === 阶段 1: 命令行有效性检查 === */
    if (!lpCmdLine || !*lpCmdLine) {
        v15 = &byte_14003E360;       /* 标题 */
        v16 = &byte_14003E710;       /* 错误内容 */
        goto LABEL_69;               /* MessageBoxA 报错退出 */
    }
    strlwr(lpCmdLine);

    /* === 阶段 2: 录像文件转发模式（.war/.rec/.mh）=== */
    if (strstr(lpCmdLine, ".war") || strstr(lpCmdLine, ".rec") || strstr(lpCmdLine, ".mh")) {
        /* 构造命令行: "{my.exe} {origcmd}" */
        lpCommandLine[0] = 0LL;
        v40 = 0LL;
        v41 = 15LL;
        v23 = -1LL;
        v24 = -1LL;
        do ++v24; while (((_BYTE *)off_14004E4D0)[v24]);
        stl_string_assign((void**)lpCommandLine, off_14004E4D0, v24);

        /* 拼接空格 */
        v25 = v40;
        if (v41 == v40) {
            stl_string_grow(lpCommandLine, 1uLL, v33, " ", 1uLL);
        } else {
            ++v40;
            v26 = lpCommandLine;
            if (v41 >= 0x10) v26 = (LPSTR*)lpCommandLine[0];
            *(_WORD*)((char*)v26 + v25) = 32;
        }

        /* 拼接原命令行 */
        do ++v23; while (lpCmdLine[v23]);
        v27 = v40;
        if (v23 > v41 - v40) {
            stl_string_grow(lpCommandLine, v23, v33, lpCmdLine, v23);
        } else {
            v40 += v23;
            v28 = lpCommandLine;
            if (v41 >= 0x10) v28 = (LPSTR*)lpCommandLine[0];
            v29 = (char*)v28 + v27;
            memmove_kr(v29, lpCmdLine, v23);
            v29[v23] = 0;
        }

        /* CreateProcessA 启动子进程播放录像 */
        memset(&StartupInfo.cb + 1, 0, 100);
        StartupInfo.cb = 104;
        v30 = (CHAR*)lpCommandLine;
        if (v41 >= 0x10) v30 = lpCommandLine[0];

        if (CreateProcessA(0, v30, 0, 0, 0, 0, 0, 0, &StartupInfo, &lParam)) {
            CloseHandle(lParam.hProcess);
            CloseHandle(lParam.hThread);
        } else {
            LastError = GetLastError();
            my_sprintf(Text, (char*)&byte_14003E348, LastError);
            MessageBoxA(0, Text, &byte_14003E360, MB_ICONERROR);
        }
        /* 释放 STL string */
        if (v41 < 0x10) return 0;
        v32 = lpCommandLine[0];
        if (v41 + 1 >= 0x1000) {
            v32 = (LPSTR)*((_QWORD*)lpCommandLine[0] - 1);
            if ((unsigned __int64)(lpCommandLine[0] - v32 - 8) > 0x1F)
                _invalid_parameter_noinfo_noreturn();
        }
        j_j_free(v32);
        return 0;
    }

    /* === 阶段 3: 解析普通命令行参数（type + num + str）=== */
    v5 = 0;
    v34 = 0;
    v49[0] = 0;
    v6 = strstr(lpCmdLine, off_14004E4E0);
    if (v6) { v5 = 1; goto LABEL_12; }
    v6 = strstr(lpCmdLine, off_14004E4E8);
    if (v6) { v5 = 2; goto LABEL_12; }
    v6 = strstr(lpCmdLine, off_14004E4F0);
    if (v6) {
        v5 = 3;
LABEL_12:
        if (my_sscanf(v6, "%s %d %s", &v46, &v34, v49) != 3)
            my_sscanf(v6, "%s %d", &v46, &v34);
    }

    /* === 阶段 4: 单实例检查 + 已有实例转发 === */
    SetLastError(0);
    MakeMHXYWinMgrClassName(Name, 256);
    CreateMutexA(0, 0, Name);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        v7 = 0;
        while (1) {
            ++v7;
            GetForegroundWindow();
            Window = FindWindowExA(0, 0, "MHXYMainFrame", 0);
            WorkdirInstanceID = GetWorkdirInstanceID();

            /* 遍历所有 MHXYMainFrame 窗口，找到同 workdir 的那个 */
            for (BOOL i = (Window == 0); ; i = (Window == 0)) {
                if (i) { Window = 0; goto LABEL_27; }
                /* 0x8102: 查询目标窗口的 workdir hash */
                if ((unsigned)SendMessageA(Window, MHMSG_QUERY_INSTANCE_ID, 0, 0) == WorkdirInstanceID)
                    break;
                Window = FindWindowExA(0, Window, "MHXYMainFrame", 0);
            }

            /* 0x8101: 查询状态（如可用 view 索引）*/
            v11 = SendMessageA(Window, MHMSG_QUERY_STATE, 0, 0);
            v12 = v11;
            if (Window && v11 >= 0) {
                v13 = SW_SHOW;
                if (IsIconic(Window)) v13 = SW_RESTORE;
                ShowWindow(Window, v13);
                SetForegroundWindow(Window);

                if (v49[0]) {
                    /* 通过 WM_COPYDATA 传递命令行字符串 */
                    v44 = 1;
                    v43 = v12;
                    strncpy(Destination, lpCmdLine, 0x200);
                    lParam.hProcess     = (HANDLE)16;       /* COPYDATASTRUCT.dwData */
                    *(_QWORD*)&lParam.dwProcessId = &v43;   /* COPYDATASTRUCT.lpData */
                    LODWORD(lParam.hThread) = 520;          /* COPYDATASTRUCT.cbData */
                    v14 = SendMessageTimeoutA(Window, MHMSG_WM_COPYDATA, 0xFFFFu, (LPARAM)&lParam,
                                              SMTO_ABORTIFHUNG, 100, 0);
                } else {
                    /* 无字符串参数，直接发激活通知 */
                    v14 = SendMessageA(Window, MHMSG_GENERIC_NOTIFY, v5, v34);
                }
                goto LABEL_29;
            }
LABEL_27:
            Sleep(100);
            if (v7 >= 10) break;
        }
        v14 = 0;
LABEL_29:
        /* 二次检查：转发后再次确认互斥锁 */
        SetLastError(0);
        CreateMutexA(0, 0, Name);
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (!v14) {
                v15 = (const CHAR*)&unk_14003E74C;
                v16 = (const CHAR*)&unk_14003E758;
                goto LABEL_69;
            }
            if (!Window) {
                v15 = (const CHAR*)&unk_14003E74C;
                v16 = (const CHAR*)&unk_14003E770;
                goto LABEL_69;
            }
            return 0;
        }
    }

    /* === 阶段 5: 首启 - 创建主窗口 === */
    *(_QWORD*)&v42 = 0;
    *((_QWORD*)&v42 + 1) = 0x1E000000280LL;   /* 默认窗口尺寸 (480, 30, ...) */
    Instance = MainFrameSingleton_GetInstance();
    if (!TabCtrlApp_Init((__int64)Instance, (__int64)v36, &v42, 1, v5, v34, (__int64)v49))
        return 0;

    /* === 阶段 6: 主消息循环 === */
    memset(&Msg, 0, sizeof(Msg));
    do {
        /* 1ms 超时定时器 - 让 GetMessage 不阻塞过久，以便驱动 WaitForCmd */
        v18 = SetTimer(0, 0, 1u, 0);
        MessageA = GetMessageA(&Msg, 0, 0, 0);
        KillTimer(0, v18);

        /* WM_TIMER（275）且来自我们设的 1ms 定时器则跳过分发 */
        if (MessageA && (Msg.message != WM_TIMER || Msg.hwnd || Msg.wParam != v18)) {
            TranslateMessage(&Msg);
            DispatchMessageA(&Msg);
        }

        /* 周期性 housekeeping */
        v20 = sub_140018CB0();
        sub_140018DB0(v20);

        /* 监控子进程退出 */
        v21 = MainFrameSingleton_GetInstance();
        TabCtrlApp_WaitForCmd((__int64)v21);
    } while (Msg.message != WM_QUIT);   /* WM_QUIT == 18 */

    return Msg.wParam;

LABEL_69:
    MessageBoxA(0, v16, v15, MB_ICONERROR);
    return 0;
}
