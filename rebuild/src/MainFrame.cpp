/**
 * mhtabx - 主窗口实现 (W1 阶段)
 *
 * 当前版本功能：
 *  - 注册窗口类
 *  - 创建主窗口
 *  - WndProc thunk (WM_NCCREATE 时绑定 this 指针)
 *  - 基础消息处理 (CREATE / DESTROY / PAINT / SIZE / COPYDATA)
 *  - 接收来自其他实例的命令行转发
 *
 * W2 会扩展：添加 TabController 成员、Tab 控件创建、
 *            ChildProcessManager 集成。
 */

#include "MainFrame.h"
#include "Utils.h"

namespace mhx {

/* ============================================================
 * 构造 / 析构
 * ============================================================ */
MainFrame::MainFrame() = default;

MainFrame::~MainFrame() {
    if (hwnd_ && ::IsWindow(hwnd_)) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

/* ============================================================
 * 窗口类注册
 * ============================================================ */
bool MainFrame::RegisterWindowClass(HINSTANCE hInstance, const String& class_name) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &MainFrame::WndProcStatic;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = class_name.c_str();
    wc.hIcon         = ::LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm       = ::LoadIcon(nullptr, IDI_APPLICATION);

    ATOM atom = ::RegisterClassExW(&wc);
    if (atom == 0) {
        DWORD err = ::GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            MHX_LOG_ERROR(L"RegisterClassExW failed: %s",
                          utils::FormatSystemError(err).c_str());
            return false;
        }
    }
    return true;
}

/* ============================================================
 * 创建
 * ============================================================ */
bool MainFrame::Create(HINSTANCE hInstance, const String& cmd_line) {
    hInstance_       = hInstance;
    pending_cmd_line_ = cmd_line;

    /* 计算实例指纹 - 基于 MD5(workdir) 前 4 字节 */
    String fp = utils::GetWorkingDirectoryFingerprint();
    if (fp.empty()) {
        MHX_LOG_ERROR(L"GetWorkingDirectoryFingerprint failed");
        return false;
    }
    /* 从 hex 字符串解析前 8 字符为 u32 */
    wchar_t* end = nullptr;
    instance_id_ = (u32)wcstoul(fp.substr(0, 8).c_str(), &end, 16);

    class_name_ = String(kMainFrameClassPrefix) + fp;

    if (!RegisterWindowClass(hInstance, class_name_)) return false;

    hwnd_ = ::CreateWindowExW(
        0,
        class_name_.c_str(),
        L"mhtabx - 多进程 Tab 容器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        960, 640,
        nullptr, nullptr,
        hInstance,
        this);     /* ← 传 this 给 WM_NCCREATE */

    if (!hwnd_) {
        MHX_LOG_ERROR(L"CreateWindowExW failed: %s",
                      utils::FormatSystemError(::GetLastError()).c_str());
        return false;
    }

    MHX_LOG_INFO(L"MainFrame created: hwnd=%p, instance_id=0x%08X",
                 hwnd_, instance_id_);
    return true;
}

void MainFrame::Show(int nShowCmd) {
    if (hwnd_) {
        ::ShowWindow(hwnd_, nShowCmd);
        ::UpdateWindow(hwnd_);
    }
}

/* ============================================================
 * 接收其他实例转发的命令行
 * ============================================================ */
void MainFrame::HandleForwardedCmdLine(const String& cmd_line) {
    MHX_LOG_INFO(L"ForwardedCmdLine: %s", cmd_line.c_str());
    /* W2: 传给 ChildProcessManager 创建新子进程 */
    /* 当前只把窗口恢复到前台提示用户 */
    if (hwnd_) {
        if (::IsIconic(hwnd_)) ::ShowWindow(hwnd_, SW_RESTORE);
        ::SetForegroundWindow(hwnd_);
    }
}

/* ============================================================
 * 窗口过程 (静态 thunk)
 *
 * WM_NCCREATE 之前 GWLP_USERDATA 还未设置，所以必须从 CREATESTRUCT 取 this。
 * 其余消息从 GWLP_USERDATA 读 this 后转发。
 * ============================================================ */
LRESULT CALLBACK MainFrame::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainFrame* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<LPCREATESTRUCTW>(lp);
        self = reinterpret_cast<MainFrame*>(cs->lpCreateParams);
        if (self) {
            self->hwnd_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    } else {
        self = reinterpret_cast<MainFrame*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) return self->WndProc(msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

/* ============================================================
 * 真正的 WndProc
 * ============================================================ */
LRESULT MainFrame::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            return OnCreate(reinterpret_cast<LPCREATESTRUCTW>(lp));

        case WM_DESTROY:
            return OnDestroy();

        case WM_PAINT:
            return OnPaint();

        case WM_SIZE:
            return OnSize(LOWORD(lp), HIWORD(lp));

        case WM_COPYDATA:
            return OnCopyData(reinterpret_cast<HWND>(wp),
                              reinterpret_cast<const COPYDATASTRUCT*>(lp));

        /* === 跨实例 IPC === */
        case INST_QUERY_INSTANCE_ID:
            return static_cast<LRESULT>(instance_id_);

        case INST_QUERY_STATE:
            return 0;     /* W2 返回 active view 索引 */
    }

    return ::DefWindowProcW(hwnd_, msg, wp, lp);
}

/* ============================================================
 * 消息处理
 * ============================================================ */
LRESULT MainFrame::OnCreate(LPCREATESTRUCTW /*cs*/) {
    MHX_LOG_TRACE(L"OnCreate");
    /* W2: 这里创建 Tab 控件、工具栏、状态栏 */

    /* 启动时如有初始命令行，延迟到窗口显示后处理 */
    if (!pending_cmd_line_.empty()) {
        ::PostMessageW(hwnd_, WM_APP + 999, 0, 0);
    }
    return 0;
}

LRESULT MainFrame::OnDestroy() {
    MHX_LOG_TRACE(L"OnDestroy");
    ::PostQuitMessage(0);
    return 0;
}

LRESULT MainFrame::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = ::BeginPaint(hwnd_, &ps);

    RECT rc;
    ::GetClientRect(hwnd_, &rc);

    /* 占位提示：W1 空窗口 */
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, RGB(96, 96, 96));

    const wchar_t* hint =
        L"mhtabx W1 骨架 - 此窗口在 W2 会被替换为 Tab 容器";
    ::DrawTextW(hdc, hint, -1, &rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    ::EndPaint(hwnd_, &ps);
    return 0;
}

LRESULT MainFrame::OnSize(int cx, int cy) {
    MHX_LOG_TRACE(L"OnSize: %dx%d", cx, cy);
    /* W2: 调整 Tab 控件大小 */
    return 0;
}

/**
 * 接收命令行转发 (其他实例 SendMessage WM_COPYDATA 过来)
 */
LRESULT MainFrame::OnCopyData(HWND /*from*/, const COPYDATASTRUCT* cds) {
    if (!cds || !cds->lpData) return FALSE;
    if (cds->cbData < sizeof(CmdLineForward)) return FALSE;

    const auto* cf = static_cast<const CmdLineForward*>(cds->lpData);
    if (cf->magic != kCmdLineForwardMagic) return FALSE;

    /* 保证字符串以 NUL 结尾 */
    size_t max_chars = sizeof(cf->cmdline) / sizeof(wchar_t);
    String cmd;
    cmd.reserve(max_chars);
    for (size_t i = 0; i < max_chars && cf->cmdline[i] != L'\0'; ++i)
        cmd.push_back(cf->cmdline[i]);

    HandleForwardedCmdLine(cmd);
    return TRUE;
}

} /* namespace mhx */
