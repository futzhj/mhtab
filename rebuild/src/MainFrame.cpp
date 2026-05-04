/**
 * mhtabx - 主窗口实现 (W2)
 *
 * 在 W1 基础上添加：
 *   - TabController + ChildProcessManager 集成
 *   - 子进程握手协议 (MHX_NEW_CLIENT)
 *   - 100ms 周期定时器轮询子进程退出
 *   - 启动示例子进程的菜单/按钮（暂用 F5 快捷键）
 */

#include "MainFrame.h"
#include "TabController.h"
#include "ChildProcessManager.h"
#include "Utils.h"

namespace mhx {

constexpr UINT_PTR kPollTimerId = 1;
constexpr UINT     kPollPeriodMs = 100;
constexpr int      kTabCtrlId    = 0x1001;

/* ============================================================
 * 构造 / 析构
 * ============================================================ */
MainFrame::MainFrame() = default;

MainFrame::~MainFrame() {
    /* 析构顺序：先 child_mgr_（终止子进程），再 tab_ctrl_（还原父子关系） */
    child_mgr_.reset();
    tab_ctrl_.reset();
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
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
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

    String fp = utils::GetWorkingDirectoryFingerprint();
    if (fp.empty()) {
        MHX_LOG_ERROR(L"GetWorkingDirectoryFingerprint failed");
        return false;
    }
    instance_id_ = static_cast<u32>(wcstoul(fp.substr(0, 8).c_str(), nullptr, 16));
    class_name_  = String(kMainFrameClassPrefix) + fp;

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
        this);

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
 * 命令行转发处理
 * ============================================================ */
void MainFrame::HandleForwardedCmdLine(const String& cmd_line) {
    MHX_LOG_INFO(L"ForwardedCmdLine: %s", cmd_line.c_str());

    /* 把窗口前台化 */
    if (hwnd_) {
        if (::IsIconic(hwnd_)) ::ShowWindow(hwnd_, SW_RESTORE);
        ::SetForegroundWindow(hwnd_);
    }

    /* W2: 把命令行解析为 exe + args，启动新子进程 */
    /* TODO(W3): 真正解析命令行格式 */
    if (!cmd_line.empty() && child_mgr_) {
        /* 简单约定：第一个 token = exe，其余 = args */
        String exe;
        String args;
        size_t sp = cmd_line.find(L' ');
        if (sp == String::npos) { exe = cmd_line; }
        else { exe = cmd_line.substr(0, sp); args = cmd_line.substr(sp + 1); }
        child_mgr_->LaunchChild(exe, args, L"");
    }
}

/* ============================================================
 * 窗口过程 thunk
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
        case WM_CREATE:    return OnCreate(reinterpret_cast<LPCREATESTRUCTW>(lp));
        case WM_DESTROY:   return OnDestroy();
        case WM_PAINT:     return OnPaint();
        case WM_SIZE:      return OnSize(LOWORD(lp), HIWORD(lp));
        case WM_TIMER:     return OnTimer(static_cast<UINT_PTR>(wp));
        case WM_NOTIFY:    return OnNotify(static_cast<int>(wp),
                                            reinterpret_cast<NMHDR*>(lp));
        case WM_COPYDATA:  return OnCopyData(reinterpret_cast<HWND>(wp),
                                              reinterpret_cast<const COPYDATASTRUCT*>(lp));

        /* 跨实例 IPC */
        case INST_QUERY_INSTANCE_ID:  return static_cast<LRESULT>(instance_id_);
        case INST_QUERY_STATE:        return tab_ctrl_ ? tab_ctrl_->GetSelectedSlotId() : -1;

        /* 子进程握手 */
        case MHX_NEW_CLIENT:    return OnNewClient(wp, lp);
        case MHX_HEARTBEAT:     return OnHeartbeat(wp, lp);
        case MHX_CLEANUP_VIEW:  return OnCleanupView(wp, lp);

        /* 调试快捷键：F5 启动 demo_child */
        case WM_KEYDOWN:
            if (wp == VK_F5) { LaunchDemoChild(); return 0; }
            break;
    }
    return ::DefWindowProcW(hwnd_, msg, wp, lp);
}

/* ============================================================
 * 消息处理
 * ============================================================ */
LRESULT MainFrame::OnCreate(LPCREATESTRUCTW /*cs*/) {
    MHX_LOG_TRACE(L"OnCreate");

    /* 创建 TabController */
    tab_ctrl_ = std::make_unique<TabController>();
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    if (!tab_ctrl_->Create(hwnd_, hInstance_, rc, kTabCtrlId)) {
        MHX_LOG_ERROR(L"TabController::Create failed");
        return -1;
    }

    /* 创建 ChildProcessManager */
    child_mgr_ = std::make_unique<ChildProcessManager>(*tab_ctrl_);
    child_mgr_->SetHostInfo(hwnd_, instance_id_);

    /* 100ms 轮询子进程退出 */
    ::SetTimer(hwnd_, kPollTimerId, kPollPeriodMs, nullptr);

    /* 处理初始命令行（如果有） */
    if (!pending_cmd_line_.empty()) {
        ::PostMessageW(hwnd_, WM_USER + 999, 0, 0);
        /* 在下一个消息循环里处理，确保窗口完全初始化 */
    }
    return 0;
}

LRESULT MainFrame::OnDestroy() {
    MHX_LOG_TRACE(L"OnDestroy");
    ::KillTimer(hwnd_, kPollTimerId);

    /* 主动销毁，避免 ~MainFrame 在 PostQuitMessage 之后还操作子窗口 */
    child_mgr_.reset();
    tab_ctrl_.reset();

    ::PostQuitMessage(0);
    return 0;
}

LRESULT MainFrame::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = ::BeginPaint(hwnd_, &ps);

    /* TabController 已占据整个 client，但 Tab 之外仍有窗口背景需绘 */
    if (tab_ctrl_ && tab_ctrl_->GetActiveCount() == 0) {
        RECT rc;
        ::GetClientRect(hwnd_, &rc);
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, RGB(96, 96, 96));
        const wchar_t* hint =
            L"按 F5 启动一个 demo_child 子进程\n"
            L"或通过命令行: mhtabx \"demo_child.exe\"";
        ::DrawTextW(hdc, hint, -1, &rc,
                    DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    }

    ::EndPaint(hwnd_, &ps);
    return 0;
}

LRESULT MainFrame::OnSize(int cx, int cy) {
    if (tab_ctrl_) tab_ctrl_->Resize(cx, cy);
    return 0;
}

LRESULT MainFrame::OnTimer(UINT_PTR id) {
    if (id == kPollTimerId && child_mgr_) {
        int dead = child_mgr_->Poll();
        if (dead >= 0) {
            /* 触发重绘提示文字 */
            ::InvalidateRect(hwnd_, nullptr, TRUE);
        }
    }
    return 0;
}

LRESULT MainFrame::OnNotify(int /*ctrl_id*/, NMHDR* hdr) {
    if (tab_ctrl_) return tab_ctrl_->HandleNotify(hdr);
    return 0;
}

LRESULT MainFrame::OnCopyData(HWND /*from*/, const COPYDATASTRUCT* cds) {
    if (!cds || !cds->lpData) return FALSE;
    if (cds->cbData < sizeof(CmdLineForward)) return FALSE;

    const auto* cf = static_cast<const CmdLineForward*>(cds->lpData);
    if (cf->magic != kCmdLineForwardMagic) return FALSE;

    String cmd;
    size_t max_chars = sizeof(cf->cmdline) / sizeof(wchar_t);
    cmd.reserve(max_chars);
    for (size_t i = 0; i < max_chars && cf->cmdline[i] != L'\0'; ++i)
        cmd.push_back(cf->cmdline[i]);

    HandleForwardedCmdLine(cmd);
    return TRUE;
}

/* ============================================================
 * IPC 消息处理（子进程→主进程）
 * ============================================================ */

/**
 * 子进程注册握手：
 *   wParam = slot_id（子进程从命令行 --mhx-slot 收到）
 *   lParam = HWND of child main window
 */
LRESULT MainFrame::OnNewClient(WPARAM slot_id_w, LPARAM child_hwnd_l) {
    int  slot_id    = static_cast<int>(slot_id_w);
    HWND child_hwnd = reinterpret_cast<HWND>(child_hwnd_l);

    MHX_LOG_INFO(L"OnNewClient: slot=%d hwnd=%p", slot_id, child_hwnd);

    if (!tab_ctrl_) return FALSE;
    auto* slot = tab_ctrl_->FindSlot(slot_id);
    if (!slot) {
        MHX_LOG_WARN(L"OnNewClient: slot %d not found", slot_id);
        return FALSE;
    }

    if (!tab_ctrl_->EmbedChildWindow(slot_id, child_hwnd)) {
        MHX_LOG_ERROR(L"EmbedChildWindow failed for slot %d", slot_id);
        return FALSE;
    }

    /* 触发重绘移除提示文字 */
    ::InvalidateRect(hwnd_, nullptr, TRUE);
    return TRUE;
}

LRESULT MainFrame::OnHeartbeat(WPARAM /*slot_id*/, LPARAM /*lp*/) {
    return TRUE;   /* 简单返回 TRUE 表示存活 */
}

LRESULT MainFrame::OnCleanupView(WPARAM slot_id, LPARAM /*lp*/) {
    if (tab_ctrl_) tab_ctrl_->RemoveSlot(static_cast<int>(slot_id));
    ::InvalidateRect(hwnd_, nullptr, TRUE);
    return TRUE;
}

/* ============================================================
 * LaunchDemoChild
 * ============================================================ */
void MainFrame::LaunchDemoChild() {
    if (!child_mgr_) return;

    /* demo_child.exe 与 mhtabx.exe 同目录 */
    String exe = utils::GetExecutableDirectory() + L"demo_child.exe";

    if (::GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        ::MessageBoxW(hwnd_,
                      utils::Format(L"未找到 demo_child.exe:\n%s", exe.c_str()).c_str(),
                      L"mhtabx", MB_ICONWARNING);
        return;
    }

    int slot_id = child_mgr_->LaunchChild(exe, L"", L"");
    MHX_LOG_INFO(L"LaunchDemoChild: slot=%d", slot_id);
}

} /* namespace mhx */
