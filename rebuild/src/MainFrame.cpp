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
#include "SessionStore.h"
#include "IpcProtocol.h"
#include "Utils.h"
#include "resource/resource.h"

#include <algorithm>
#include <shellapi.h>   /* CommandLineToArgvW */

namespace mhx {

constexpr UINT_PTR kPollTimerId   = 1;
constexpr UINT     kPollPeriodMs  = 100;
constexpr int      kTabCtrlId     = 0x1001;
constexpr UINT     kMsgPostInit   = WM_USER + 1001;  /* OnPostInit 触发消息 */

/* ============================================================
 * 构造 / 析构
 * ============================================================ */
MainFrame::MainFrame() = default;

MainFrame::~MainFrame() {
    /* 析构顺序: heartbeat_ → child_mgr_ → tab_ctrl_ → session_store_ */
    heartbeat_.reset();
    child_mgr_.reset();
    tab_ctrl_.reset();
    session_store_.reset();
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
    wc.lpszMenuName  = MAKEINTRESOURCEW(IDR_MAINMENU);
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
        case WM_COMMAND:   return OnCommand(LOWORD(wp), HIWORD(wp),
                                             reinterpret_cast<HWND>(lp));
        case WM_DRAWITEM:  return tab_ctrl_
                                ? tab_ctrl_->OnDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lp))
                                : ::DefWindowProcW(hwnd_, msg, wp, lp);
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

        case kMsgPostInit:
            OnPostInit();
            return 0;

        /* 调试: F5 启动 demo_child；其余键盘事件转发 */
        case WM_KEYDOWN:
            if (wp == VK_F5) { LaunchDemoChild(); return 0; }
            if (ForwardKeyToActiveChild(msg, wp, lp)) return 0;
            break;
        case WM_KEYUP:
        case WM_CHAR:
            /* 不转发 SYSKEYDOWN/UP，避免破坏 Alt+菜单访问键 */
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

    /* W4: SessionStore - ini 路径为 <exe_dir>\mhtabx.ini */
    session_store_ = std::make_unique<SessionStore>(
        utils::GetExecutableDirectory() + L"mhtabx.ini");

    /* W5-1: 状态栏 - 3 个分区:
     *   [0] Tab 总数    ~160px
     *   [1] 当前 slot+PID+State  ~400px
     *   [2] 心跳 idle  剩余  */
    status_bar_ = ::CreateWindowExW(
        0, STATUSCLASSNAMEW, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS_BAR)),
        hInstance_, nullptr);
    if (status_bar_) {
        int parts[3] = { 160, 560, -1 };
        ::SendMessageW(status_bar_, SB_SETPARTS, 3,
                       reinterpret_cast<LPARAM>(parts));
        HFONT font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
        ::SendMessageW(status_bar_, WM_SETFONT,
                       reinterpret_cast<WPARAM>(font), TRUE);
    } else {
        MHX_LOG_WARN(L"CreateWindowExW(STATUSCLASSNAMEW) failed: %s",
                     utils::FormatSystemError(::GetLastError()).c_str());
    }

    ::SetTimer(hwnd_, kPollTimerId, kPollPeriodMs, nullptr);

    /* 延迟触发 OnPostInit，让主窗口先显示出来 */
    ::PostMessageW(hwnd_, kMsgPostInit, 0, 0);
    return 0;
}

LRESULT MainFrame::OnDestroy() {
    MHX_LOG_TRACE(L"OnDestroy");
    ::KillTimer(hwnd_, kPollTimerId);

    /* W4: 在 tab_ctrl_/child_mgr_ 销毁前保存 session，
     * SaveSession 内部会读这些对象。 */
    SaveSession();

    heartbeat_.reset();
    child_mgr_.reset();
    tab_ctrl_.reset();
    session_store_.reset();

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
    /* StatusBar 先自动贴底并重排分区 */
    int sb_h = 0;
    if (status_bar_) {
        ::SendMessageW(status_bar_, WM_SIZE, 0, 0);
        RECT sb_rc = {};
        ::GetWindowRect(status_bar_, &sb_rc);
        sb_h = sb_rc.bottom - sb_rc.top;
    }
    /* 剩余空间给 TabController */
    if (tab_ctrl_) tab_ctrl_->Resize(cx, std::max(0, cy - sb_h));
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

    /* 3. 刷新状态栏 */
    UpdateStatusBar();
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
 * Tab \u5207\u6362\u540e\u540c\u6b65 ACTIVATE/HIDE \u7ed9\u5b50\u8fdb\u7a0b
 *
 * \u4ec5\u5728 prev != curr \u65f6\u5dee\u5f02\u53d1\u9001\uff0c\u907f\u514d\u91cd\u590d ACTIVATE \u5e72\u6270\u5b50\u8fdb\u7a0b\u72b6\u6001\u3002
 * ============================================================ */
static void SyncActivateHide(TabController& tab_ctrl, int prev_slot, int curr_slot) {
    if (prev_slot == curr_slot) return;
    if (auto* p = tab_ctrl.FindSlot(prev_slot)) {
        if (p->child_hwnd && ::IsWindow(p->child_hwnd))
            ipc::PostHideView(p->child_hwnd, prev_slot);
    }
    if (auto* c = tab_ctrl.FindSlot(curr_slot)) {
        if (c->child_hwnd && ::IsWindow(c->child_hwnd))
            ipc::PostActivateView(c->child_hwnd, curr_slot);
    }
}

/* ============================================================
 * OnCommand - \u83dc\u5355 / \u52a0\u901f\u952e\u547d\u4ee4
 * ============================================================ */
LRESULT MainFrame::OnCommand(WORD id, WORD /*code*/, HWND /*ctrl*/) {
    switch (id) {
        case ID_FILE_NEW:
            LaunchDemoChild();
            return 0;

        case ID_FILE_CLOSE_TAB: {
            if (!tab_ctrl_ || !child_mgr_) return 0;
            int sid = tab_ctrl_->GetSelectedSlotId();
            if (sid >= 0) {
                child_mgr_->RequestClose(sid, /*force=*/false);
            }
            return 0;
        }

        case ID_FILE_EXIT:
            ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return 0;

        case ID_TAB_NEXT: {
            if (!tab_ctrl_) return 0;
            int prev = tab_ctrl_->GetSelectedSlotId();
            tab_ctrl_->SelectNext();
            int curr = tab_ctrl_->GetSelectedSlotId();
            SyncActivateHide(*tab_ctrl_, prev, curr);
            return 0;
        }

        case ID_TAB_PREV: {
            if (!tab_ctrl_) return 0;
            int prev = tab_ctrl_->GetSelectedSlotId();
            tab_ctrl_->SelectPrev();
            int curr = tab_ctrl_->GetSelectedSlotId();
            SyncActivateHide(*tab_ctrl_, prev, curr);
            return 0;
        }

        case ID_TAB_RENAME: {
            /* \u7b80\u5355\u5b9e\u73b0\uff1a\u7ed9\u5f53\u524d Tab \u8ffd\u52a0\u4e00\u4e2a '*' \u540e\u7f00 */
            if (!tab_ctrl_) return 0;
            int sid = tab_ctrl_->GetSelectedSlotId();
            auto* slot = tab_ctrl_->FindSlot(sid);
            if (slot) {
                String new_title = slot->title + L" *";
                tab_ctrl_->RenameSlot(sid, new_title);
            }
            return 0;
        }

        case ID_HELP_ABOUT:
            ::MessageBoxW(hwnd_,
                L"mhtabx - \u591a\u8fdb\u7a0b Tab \u5bb9\u5668\n"
                L"\n"
                L"mhtab.exe \u5f00\u6e90\u590d\u523b\uff08W1-W4 \u9636\u6bb5\uff09\n"
                L"\n"
                L"\u6e90\u4ee3\u7801\uff1ahttps://github.com/futzhj/mhtab",
                L"\u5173\u4e8e mhtabx", MB_ICONINFORMATION);
            return 0;
    }
    return 0;
}

/* ============================================================
 * W4: OnPostInit
 *
 * 在主窗口创建完成后由 PostMessage 触发，处理两种情况：
 *   - pending_cmd_line_ 非空 → 启动指定程序
 *   - 否则 → 从 SessionStore 恢复上次的 Tab 列表
 *
 * 注意：CommandLineToArgvW 要求传入的字符串符合 Win32 命令行语法，
 * 第一个 token 视为 exe，剩余为 args。
 * ============================================================ */
void MainFrame::OnPostInit() {
    if (!child_mgr_) return;

    /* 1. 命令行优先 */
    if (!pending_cmd_line_.empty()) {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(pending_cmd_line_.c_str(), &argc);
        if (argv && argc >= 1) {
            String exe = argv[0];
            String args;
            for (int i = 1; i < argc; ++i) {
                if (!args.empty()) args.push_back(L' ');
                /* 简单还原：含空格的参数补回引号 */
                bool has_space = wcschr(argv[i], L' ') != nullptr;
                if (has_space) args.push_back(L'"');
                args += argv[i];
                if (has_space) args.push_back(L'"');
            }
            ::LocalFree(argv);
            child_mgr_->LaunchChild(exe, args, L"");
            pending_cmd_line_.clear();
        } else if (argv) {
            ::LocalFree(argv);
        }
        return;
    }

    /* 2. 否则恢复 session */
    if (!session_store_) return;
    auto entries = session_store_->Load();
    if (entries.empty()) {
        MHX_LOG_INFO(L"OnPostInit: no session to restore");
        return;
    }

    int restored = 0;
    for (const auto& e : entries) {
        if (::GetFileAttributesW(e.exe_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            MHX_LOG_WARN(L"Skip missing exe: %s", e.exe_path.c_str());
            continue;
        }
        if (child_mgr_->LaunchChild(e.exe_path, e.args, e.title) >= 0) {
            ++restored;
        }
    }
    MHX_LOG_INFO(L"OnPostInit: restored %d/%zu tabs", restored, entries.size());
}

/* ============================================================
 * W5-1: UpdateStatusBar
 *
 * 从 OnTimer 每 100ms 调一次。3 个分区：
 *   Part 0: "Tabs: N/32"
 *   Part 1: "[Slot X] PID Y  Starting|Running|Dead"
 *   Part 2: "Idle: Zms"  或  "Heartbeat: --"
 *
 * SB_SETTEXT 内部会去重：相同文本不会重绘，所以高频调用成本很低。
 * ============================================================ */
void MainFrame::UpdateStatusBar() {
    if (!status_bar_ || !tab_ctrl_) return;

    /* Part 0: Tab 数 / 上限 */
    {
        int max_n = child_mgr_ ? child_mgr_->GetMaxChildren() : 0;
        String s = utils::Format(L"Tabs: %zu / %d",
                                  tab_ctrl_->GetActiveCount(), max_n);
        ::SendMessageW(status_bar_, SB_SETTEXTW, 0,
                       reinterpret_cast<LPARAM>(s.c_str()));
    }

    /* Part 1/2 依赖当前 selected slot */
    int sid = tab_ctrl_->GetSelectedSlotId();
    auto* slot = tab_ctrl_->FindSlot(sid);

    if (slot) {
        const wchar_t* state = L"?";
        switch (slot->state) {
            case ChildState::Starting: state = L"Starting"; break;
            case ChildState::Ready:    state = L"Running";  break;
            case ChildState::Dead:     state = L"Dead";     break;
        }
        String s1 = utils::Format(L"[Slot %d] PID %lu  %s",
                                   slot->slot_id, slot->pid, state);
        ::SendMessageW(status_bar_, SB_SETTEXTW, 1,
                       reinterpret_cast<LPARAM>(s1.c_str()));

        long idle_ms = heartbeat_ ? heartbeat_->GetIdleMs(sid) : -1;
        String s2 = (idle_ms < 0)
            ? String(L"Heartbeat: --")
            : utils::Format(L"Idle: %ldms", idle_ms);
        ::SendMessageW(status_bar_, SB_SETTEXTW, 2,
                       reinterpret_cast<LPARAM>(s2.c_str()));
    } else {
        ::SendMessageW(status_bar_, SB_SETTEXTW, 1,
                       reinterpret_cast<LPARAM>(L""));
        ::SendMessageW(status_bar_, SB_SETTEXTW, 2,
                       reinterpret_cast<LPARAM>(L""));
    }
}

/* ============================================================
 * W4: SaveSession
 *
 * 遍历所有 active slot，按 tab_index 顺序写入 ini。
 * 会过滤 Dead 状态以及没有 exe_path 的 slot（避免存进无效条目）。
 * ============================================================ */
void MainFrame::SaveSession() {
    if (!session_store_ || !tab_ctrl_) return;

    /* 1. 收集 (tab_index, entry) 对 */
    std::vector<std::pair<int, SessionEntry>> indexed;
    tab_ctrl_->ForEachSlot([&](ChildSlot& slot) {
        if (slot.state == ChildState::Dead) return;
        if (slot.exe_path.empty()) return;
        SessionEntry e;
        e.exe_path = slot.exe_path;
        e.args     = slot.cmdline;
        e.title    = slot.title;
        indexed.emplace_back(slot.tab_index, std::move(e));
    });

    /* 2. 按 tab_index 排序，使保存顺序与 UI 显示一致 */
    std::sort(indexed.begin(), indexed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<SessionEntry> entries;
    entries.reserve(indexed.size());
    for (auto& p : indexed) entries.push_back(std::move(p.second));

    session_store_->Save(entries);
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
