/**
 * mhtabx - 程序入口 (W1)
 *
 * 职责：
 *  1. 解析命令行（保留原样字符串）
 *  2. 构造命名互斥锁 `Global\mhtabx_{MD5(workdir)}`
 *  3. 若互斥锁已存在：FindWindow 目标主窗口，SendMessage 转发命令行后退出
 *  4. 否则：初始化 MainFrame 并进入消息循环
 */

#include "MainFrame.h"
#include "Utils.h"
#include "common.h"
#include "resource/resource.h"

#include <shellapi.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32")

namespace mhx {

/* ============================================================
 * 命令行解析
 * ============================================================ */

/* Unicode WinMain 的参数已是 wchar_t*，直接 strdup 即可 */
static String BuildCmdLineFromArgv() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return {};

    /* 丢掉 argv[0]，拼接剩余参数 */
    String result;
    for (int i = 1; i < argc; ++i) {
        if (!result.empty()) result.push_back(L' ');
        /* 含空格的参数补回引号 */
        bool has_space = wcschr(argv[i], L' ') != nullptr;
        if (has_space) result.push_back(L'"');
        result.append(argv[i]);
        if (has_space) result.push_back(L'"');
    }
    ::LocalFree(argv);
    return result;
}

/* ============================================================
 * 向已有实例转发命令行
 *
 * 步骤：
 *  1. FindWindowExW 枚举所有 mhtabx 主窗口
 *  2. 发 INST_QUERY_INSTANCE_ID 验证同 workdir
 *  3. 命令行装入 CmdLineForward 结构，WM_COPYDATA 转发
 *  4. 前台化目标窗口
 * ============================================================ */
static bool TryForwardToExistingInstance(const String& class_name,
                                         u32 expected_id,
                                         const String& cmd_line) {
    HWND wnd = nullptr;
    for (;;) {
        wnd = ::FindWindowExW(nullptr, wnd, class_name.c_str(), nullptr);
        if (!wnd) break;

        LRESULT id = ::SendMessageW(wnd, INST_QUERY_INSTANCE_ID, 0, 0);
        if (static_cast<u32>(id) != expected_id) continue;

        /* 找到同 workdir 实例，转发命令行 */
        CmdLineForward cf = {};
        cf.magic = kCmdLineForwardMagic;
        cf.flags = 0;
        size_t n = std::min<size_t>(cmd_line.size(),
                                    _countof(cf.cmdline) - 1);
        std::wmemcpy(cf.cmdline, cmd_line.data(), n);
        cf.cmdline[n] = L'\0';

        COPYDATASTRUCT cds = {};
        cds.dwData = kCmdLineForwardMagic;
        cds.cbData = sizeof(cf);
        cds.lpData = &cf;

        DWORD_PTR rc = 0;
        LRESULT r = ::SendMessageTimeoutW(
            wnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
            SMTO_ABORTIFHUNG, 2000, &rc);

        if (r && rc == TRUE) {
            /* 前台化 */
            if (::IsIconic(wnd)) ::ShowWindow(wnd, SW_RESTORE);
            ::SetForegroundWindow(wnd);
            MHX_LOG_INFO(L"Forwarded cmdline to existing instance hwnd=%p", wnd);
            return true;
        }
    }
    return false;
}

/* ============================================================
 * 初始化 Common Controls
 * ============================================================ */
static bool InitCommonCtrls() {
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    return ::InitCommonControlsEx(&icc) == TRUE;
}

/* ============================================================
 * 主消息循环
 *
 * 所有路径：
 *   GetMessage → TranslateAccelerator → (未命中加速键时) TranslateMessage + DispatchMessage
 * ============================================================ */
static int RunMessageLoop(HWND main_hwnd, HACCEL hAccel) {
    MSG msg = {};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (hAccel && ::TranslateAcceleratorW(main_hwnd, hAccel, &msg))
            continue;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

/* ============================================================
 * AppMain
 * ============================================================ */
static int AppMain(HINSTANCE hInstance, int nShowCmd) {
    if (!InitCommonCtrls()) {
        MHX_LOG_ERROR(L"InitCommonControlsEx failed");
        return 1;
    }

    String cmd_line = BuildCmdLineFromArgv();
    MHX_LOG_INFO(L"cmdline: %s", cmd_line.empty() ? L"(empty)" : cmd_line.c_str());

    /* === 构造实例指纹 === */
    String fp = utils::GetWorkingDirectoryFingerprint();
    if (fp.empty()) {
        ::MessageBoxW(nullptr, L"无法获取工作目录指纹", L"mhtabx", MB_ICONERROR);
        return 1;
    }

    String mutex_name = String(kMutexNamePrefix) + fp;
    String class_name = String(kMainFrameClassPrefix) + fp;
    u32    instance_id = (u32)wcstoul(fp.substr(0, 8).c_str(), nullptr, 16);

    /* === 单实例互斥锁 === */
    ::SetLastError(0);
    UniqueHandle mutex(::CreateMutexW(nullptr, FALSE, mutex_name.c_str()));
    DWORD err = ::GetLastError();

    if (err == ERROR_ALREADY_EXISTS) {
        /* 已有实例：尝试转发 */
        MHX_LOG_INFO(L"Existing instance detected, forwarding cmdline...");

        /* 等窗口稳定：最多重试 10 次，每次 100ms */
        for (int i = 0; i < 10; ++i) {
            if (TryForwardToExistingInstance(class_name, instance_id, cmd_line))
                return 0;
            ::Sleep(100);
        }

        ::MessageBoxW(nullptr,
                      L"已有 mhtabx 实例运行但无法转发命令行（窗口可能已挂起）",
                      L"mhtabx", MB_ICONWARNING);
        return 2;
    }

    if (err != 0 && err != ERROR_SUCCESS) {
        String msg = utils::Format(
            L"CreateMutex failed: %s",
            utils::FormatSystemError(err).c_str());
        ::MessageBoxW(nullptr, msg.c_str(), L"mhtabx", MB_ICONERROR);
        return 3;
    }

    /* === 首启：创建主窗口 === */
    MainFrame frame;
    if (!frame.Create(hInstance, cmd_line)) {
        ::MessageBoxW(nullptr, L"主窗口创建失败", L"mhtabx", MB_ICONERROR);
        return 4;
    }
    frame.Show(nShowCmd);

    /* === 加载加速键 === */
    HACCEL hAccel = ::LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCELERATOR));
    if (!hAccel) MHX_LOG_WARN(L"LoadAccelerators failed; 加速键不可用");

    /* === 消息循环 === */
    int exit_code = RunMessageLoop(frame.GetHwnd(), hAccel);
    MHX_LOG_INFO(L"main exit: %d", exit_code);
    return exit_code;
}

} /* namespace mhx */

/* ============================================================
 * Windows 入口
 * ============================================================ */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrev*/,
                    LPWSTR /*lpCmdLine*/, int nShowCmd) {
    return mhx::AppMain(hInstance, nShowCmd);
}
