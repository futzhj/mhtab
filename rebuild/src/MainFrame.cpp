/**
 * mhtabx - 主窗口实现 (W3)
 *
 * W3 新增：
 *   - HeartbeatMonitor 1 秒心跳，3 秒超时强制清理
 *   - 完整 IPC 消息处理（READY_CONFIRM / UPDATE_POS / NEW_VIEW）
 *   - Tab 切换时给子进程发 ACTIVATE_VIEW / HIDE_VIEW
 *   - 主窗口键盘输入转发到当前 active 子窗口
 */

#include "MainFrame.h"
#include "TabController.h"
#include "ChildProcessManager.h"
#include "HeartbeatMonitor.h"
#include "IpcProtocol.h"
#include "Utils.h"

namespace mhx {

constexpr UINT_PTR kPollTimerId  = 1;
constexpr UINT     kPollPeriodMs = 100;
constexpr int      kTabCtrlId    = 0x1001;

/* ============================================================
 * 构造 / 析构
 * ============================================================ */
MainFrame::MainFrame() = default;

MainFrame::~MainFrame() {
    /* 析构顺序: heartbeat_ → child_mgr_ → tab_ctrl_ */
    heartbeat_.reset();
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
        0, class_name_.c_str(),
        L"mhtabx - 多进程 Tab 容器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 640,
        nullptr, nullptr, hInstance, this);

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

    if (hwnd_) {
        if (::IsIconic(hwnd_)) ::ShowWindow(hwnd_, SW_RESTORE);
        ::SetForegroundWindow(hwnd_);
    }

    if (!cmd_line.empty() && child_mgr_) {
        String exe, args;
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

        /* 子进程发来的消息 */
        case MHX_NEW_CLIENT:    return OnNewClient(wp, lp);
        case MHX_READY_CONFIRM: return OnReadyConfirm(wp, lp);
        case MHX_UPDATE_POS:    return OnUpdatePos(wp, lp);
        case MHX_NEW_VIEW:      return OnNewView(wp, lp);
        case MHX_CLEANUP_VIEW:  return OnCleanupView(wp, lp);
        case MHX_HEARTBEAT:     return TRUE;   /* 子进程发的兼容情况 */

        /* 调试: F5 启动 demo_child；其余键盘事件转发 */
        case WM_KEYDOWN:
            if (wp == VK_F5) { LaunchDemoChild(); return 0; }
            if (ForwardKeyToActiveChild(msg, wp, lp)) return 0;
            break;
        case WM_KEYUP:
        case WM_CHAR:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if (ForwardKeyToActiveChild(msg, wp, lp)) return 0;
            break;
    }
    return ::DefWindowProcW(hwnd_, msg, wp, lp);
}

/* ============================================================
 * 消息处理
 * ============================================================ */
LRESULT MainFrame::OnCreate(LPCREATESTRUCTW /*cs*/) {
    MHX_LOG_TRACE(L"OnCreate");

    tab_ctrl_ = std::make_unique<TabController>();
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    if (!tab_ctrl_->Create(hwnd_, hInstance_, rc, kTabCtrlId)) {
        MHX_LOG_ERROR(L"TabController::Create failed");
        return -1;
    }

    child_mgr_ = std::make_unique<ChildProcessManager>(*tab_ctrl_);
    child_mgr_->SetHostInfo(hwnd_, instance_id_);

    heartbeat_ = std::make_unique<HeartbeatMonitor>(*tab_ctrl_, *child_mgr_);

    ::SetTimer(hwnd_, kPollTimerId, kPollPeriodMs, nullptr);

    if (!pending_cmd_line_.empty()) {
        ::PostMessageW(hwnd_, WM_USER + 999, 0, 0);
    }
    return 0;
}

LRESULT MainFrame::OnDestroy() {
    MHX_LOG_TRACE(L"OnDestroy");
    ::KillTimer(hwnd_, kPollTimerId);

    heartbeat_.reset();
    child_mgr_.reset();
    tab_ctrl_.reset();

    ::PostQuitMessage(0);
    return 0;
}

LRESULT MainFrame::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = ::BeginPaint(hwnd_, &ps);

    if (tab_ctrl_ && tab_ctrl_->GetActiveCount() == 0) {
        RECT rc;
        ::GetClientRect(hwnd_, &rc);
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, RGB(96, 96, 96));
        const wchar_t* hint =
            L"按 F5 启动 demo_child 子进程\n"
            L"或通过命令行: mhtabx \"demo_child.exe\"\n\n"
            L"键盘输入会自动转发到当前激活的 Tab";
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
    if (id != kPollTimerId) return 0;

    /* 1. 子进程退出探测 */
    if (child_mgr_) {
        int dead = child_mgr_->Poll();
        if (dead >= 0) {
            if (heartbeat_) heartbeat_->UnregisterSlot(dead);
            ::InvalidateRect(hwnd_, nullptr, TRUE);
        }
    }

    /* 2. 心跳轮询 */
    if (heartbeat_) heartbeat_->Tick();

    return 0;
}

/* ============================================================
 * Tab 切换通知 - 同步给子进程 ACTIVATE/HIDE
 * ============================================================ */
LRESULT MainFrame::OnNotify(int /*ctrl_id*/, NMHDR* hdr) {
    if (!tab_ctrl_) return 0;

    /* 记录切换前的 slot，处理后给前后两个 slot 发对应 ipc 消息 */
    int prev_slot = tab_ctrl_->GetSelectedSlotId();
    LRESULT r = tab_ctrl_->HandleNotify(hdr);
    int curr_slot = tab_ctrl_->GetSelectedSlotId();

    if (prev_slot != curr_slot) {
        if (auto* p = tab_ctrl_->FindSlot(prev_slot)) {
            if (p->child_hwnd && ::IsWindow(p->child_hwnd))
                ipc::PostHideView(p->child_hwnd, prev_slot);
        }
        if (auto* c = tab_ctrl_->FindSlot(curr_slot)) {
            if (c->child_hwnd && ::IsWindow(c->child_hwnd))
                ipc::PostActivateView(c->child_hwnd, curr_slot);
        }
    }
    return r;
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

    /* 注册到心跳监视器 */
    if (heartbeat_) heartbeat_->RegisterSlot(slot_id);

    /* 立即发一次 ACTIVATE_VIEW 让子进程知道自己是 active */
    ipc::PostActivateView(child_hwnd, slot_id);

    ::InvalidateRect(hwnd_, nullptr, TRUE);
    return TRUE;
}

LRESULT MainFrame::OnReadyConfirm(WPARAM slot_id_w, LPARAM tick_l) {
    int slot_id = static_cast<int>(slot_id_w);
    if (heartbeat_) heartbeat_->OnReadyConfirm(slot_id, static_cast<ULONG>(tick_l));
    return TRUE;
}

LRESULT MainFrame::OnUpdatePos(WPARAM slot_id_w, LPARAM packed_xy) {
    /* 嵌入后子进程位置由 host 控制，这里只记录日志 */
    MHX_LOG_TRACE(L"OnUpdatePos slot=%d x=%d y=%d",
                  static_cast<int>(slot_id_w),
                  ipc::UnpackX(packed_xy), ipc::UnpackY(packed_xy));
    return TRUE;
}

LRESULT MainFrame::OnNewView(WPARAM /*slot_id*/, LPARAM /*hint*/) {
    /* 子进程请求新建 Tab：复用 LaunchDemoChild 启动同一个程序 */
    LaunchDemoChild();
    return TRUE;
}

LRESULT MainFrame::OnCleanupView(WPARAM slot_id, LPARAM /*lp*/) {
    int sid = static_cast<int>(slot_id);
    if (heartbeat_) heartbeat_->UnregisterSlot(sid);
    if (tab_ctrl_)  tab_ctrl_->RemoveSlot(sid);
    ::InvalidateRect(hwnd_, nullptr, TRUE);
    return TRUE;
}

/* ============================================================
 * 键盘转发：主窗口收到 → 当前 active 子窗口
 * ============================================================ */
bool MainFrame::ForwardKeyToActiveChild(UINT msg, WPARAM wp, LPARAM lp) {
    if (!tab_ctrl_) return false;
    int sid = tab_ctrl_->GetSelectedSlotId();
    if (sid < 0) return false;

    auto* slot = tab_ctrl_->FindSlot(sid);
    if (!slot || !slot->child_hwnd || !::IsWindow(slot->child_hwnd)) return false;

    ipc::SendForwardInput(slot->child_hwnd, msg, wp, lp);
    return true;
}

/* ============================================================
 * LaunchDemoChild
 * ============================================================ */
void MainFrame::LaunchDemoChild() {
    if (!child_mgr_) return;

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
