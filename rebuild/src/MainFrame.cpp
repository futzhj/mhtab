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
#include "SettingsDialog.h"
#include "Theme.h"
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
        case WM_DPICHANGED: return OnDpiChanged(LOWORD(wp),
                                                 reinterpret_cast<const RECT*>(lp));
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
        case MHX_SET_TAB_ICON:  return OnSetTabIcon(wp, lp);   /* W6-2 */
        case MHX_REMBED_REQUEST: return OnRembedRequest(wp, lp); /* W6-bugfix */
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
    String ini_path = utils::GetExecutableDirectory() + L"mhtabx.ini";
    session_store_ = std::make_unique<SessionStore>(ini_path);

    /* W6-3: 从 ini 读取运行时配置并应用到 HeartbeatMonitor */
    settings_ini_path_ = ini_path;
    SettingsValues sv = LoadSettings(ini_path);
    heartbeat_->SetTimings(sv.interval_ms, sv.timeout_ms);
    current_theme_name_ = sv.theme_name;

    /* W6-4: 按 ini 里的主题名安装主题（FlatModern / DarkModern / ...）
     * TabController 构造时已装了默认 FlatModernTheme，这里显式覆盖保证
     * ini 指定的主题生效。CreateTheme 未识别的名字会回退到 FlatModern，
     * 所以不会因为 ini 拼写错误而黑屏。 */
    tab_ctrl_->SetTheme(CreateTheme(sv.theme_name));

    /* P2 + P4: 注入"拖拽出主窗口"的回调，让 MainFrame 决定 spawn 还是跨实例合并 */
    tab_ctrl_->SetDragOutCallback(
        [this](int slot_id, POINT screen_pt) { OnTabDragOut(slot_id, screen_pt); });

    /* W5-3: 工具栏 (图标 + 文字) - 复用菜单 ID 走 WM_COMMAND */
    toolbar_ = ::CreateWindowExW(
        0, TOOLBARCLASSNAMEW, L"",
        WS_CHILD | WS_VISIBLE | CCS_TOP | CCS_NODIVIDER |
            TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST,
        0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TOOLBAR)),
        hInstance_, nullptr);
    if (toolbar_) {
        ::SendMessageW(toolbar_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        ::SendMessageW(toolbar_, TB_SETEXTENDEDSTYLE, 0,
                       TBSTYLE_EX_MIXEDBUTTONS);

        /* 使用 Common Controls 标准小图标集 (16x16)，免去单独的位图资源 */
        TBADDBITMAP tbab = { HINST_COMMCTRL, IDB_STD_SMALL_COLOR };
        int std_idx = static_cast<int>(::SendMessageW(
            toolbar_, TB_ADDBITMAP, 0, reinterpret_cast<LPARAM>(&tbab)));

        TBBUTTON btns[] = {
            { std_idx + STD_FILENEW, ID_FILE_NEW,
              TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE,
              {0}, 0, reinterpret_cast<INT_PTR>(L"新建 Tab") },
            { std_idx + STD_DELETE, ID_FILE_CLOSE_TAB,
              TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE,
              {0}, 0, reinterpret_cast<INT_PTR>(L"关闭") },
            { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
            { std_idx + STD_UNDO, ID_TAB_PREV,
              TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE,
              {0}, 0, reinterpret_cast<INT_PTR>(L"上一个") },
            { std_idx + STD_REDOW, ID_TAB_NEXT,
              TBSTATE_ENABLED, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE,
              {0}, 0, reinterpret_cast<INT_PTR>(L"下一个") },
        };
        ::SendMessageW(toolbar_, TB_ADDBUTTONS,
                       static_cast<WPARAM>(_countof(btns)),
                       reinterpret_cast<LPARAM>(btns));
        ::SendMessageW(toolbar_, TB_AUTOSIZE, 0, 0);
    } else {
        MHX_LOG_WARN(L"CreateWindowExW(TOOLBAR) failed: %s",
                     utils::FormatSystemError(::GetLastError()).c_str());
    }

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
        /* 记录窗口 DPI 并按比例设置 parts */
        current_dpi_ = utils::GetDpiForHwnd(hwnd_);
        SetStatusBarPartsForDpi(current_dpi_);
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
    /* W5-3: 工具栏 TB_AUTOSIZE 自动贴顶 */
    int tb_h = 0;
    if (toolbar_) {
        ::SendMessageW(toolbar_, TB_AUTOSIZE, 0, 0);
        RECT tb_rc = {};
        ::GetWindowRect(toolbar_, &tb_rc);
        tb_h = tb_rc.bottom - tb_rc.top;
    }

    /* W5-1: StatusBar 自动贴底并重排分区 */
    int sb_h = 0;
    if (status_bar_) {
        ::SendMessageW(status_bar_, WM_SIZE, 0, 0);
        RECT sb_rc = {};
        ::GetWindowRect(status_bar_, &sb_rc);
        sb_h = sb_rc.bottom - sb_rc.top;
    }

    /* TabController 在 toolbar 和 status bar 之间
     * 先 SetWindowPos 改位置（TabController::Resize 用 SWP_NOMOVE 不会覆盖 y） */
    int tab_h = std::max(0, cy - tb_h - sb_h);
    if (tab_ctrl_ && tab_ctrl_->GetHwnd()) {
        ::SetWindowPos(tab_ctrl_->GetHwnd(), nullptr,
                       0, tb_h, cx, tab_h,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        tab_ctrl_->Resize(cx, tab_h);
    }
    return 0;
}

LRESULT MainFrame::OnTimer(UINT_PTR id) {
    if (id != kPollTimerId) return 0;

    /* 1. 子进程退出探测 */
    bool any_dead = false;
    if (child_mgr_) {
        int dead = child_mgr_->Poll();
        if (dead >= 0) {
            if (heartbeat_) heartbeat_->UnregisterSlot(dead);
            ::InvalidateRect(hwnd_, nullptr, TRUE);
            any_dead = true;
        }
    }

    /* 2. 心跳轮询 */
    if (heartbeat_) heartbeat_->Tick();

    /* 3. 刷新状态栏 */
    UpdateStatusBar();

    /* 4. P5: 维护 has_ever_had_tab_ + 自动退出检查
     *    放在 timer 里集中维护，比在每个 AddSlot/RemoveSlot 调用方都 hook 更稳。 */
    if (tab_ctrl_) {
        if (tab_ctrl_->GetActiveCount() > 0) {
            has_ever_had_tab_ = true;
        } else if (any_dead) {
            /* 仅在本轮检测到 child 退出时才检查，避免空闲 timer 反复尝试 */
            CheckAutoExit();
        }
    }
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

    /* P5: 子进程主动通知清理后，可能这是最后一个 Tab，立即检查退出 */
    CheckAutoExit();
    return TRUE;
}

/* ============================================================
 * W6-2: OnSetTabIcon
 *
 * 子进程通过 SendMessage(MHX_SET_TAB_ICON, slot_id, HICON) 上报图标。
 *
 * 关键设计：跨进程 HICON 安全策略
 *   - 子进程传过来的 HICON 是它自己上下文中的句柄
 *   - 用 CopyIcon 在主进程里立即复制一份独立副本，归我们所有
 *   - 旧的 slot->icon（如果有）DestroyIcon 释放
 *   - 子进程退出时它的 HICON 被系统回收，主进程的副本不受影响
 *
 * 失败容忍：CopyIcon 返回 NULL 时直接清空 icon，不影响其它功能。
 * ============================================================ */
LRESULT MainFrame::OnSetTabIcon(WPARAM slot_id_w, LPARAM icon_l) {
    if (!tab_ctrl_) return FALSE;
    int sid = static_cast<int>(slot_id_w);
    auto* slot = tab_ctrl_->FindSlot(sid);
    if (!slot) {
        MHX_LOG_WARN(L"OnSetTabIcon: slot %d not found", sid);
        return FALSE;
    }

    HICON src = reinterpret_cast<HICON>(icon_l);

    /* 释放旧 icon */
    if (slot->icon) {
        ::DestroyIcon(slot->icon);
        slot->icon = nullptr;
    }

    /* 复制新 icon（src 可能为 NULL，表示清除图标） */
    if (src) {
        slot->icon = ::CopyIcon(src);
        if (!slot->icon) {
            MHX_LOG_WARN(L"CopyIcon failed: %s",
                utils::FormatSystemError(::GetLastError()).c_str());
        }
    }

    /* 重绘对应 Tab 头 */
    if (slot->tab_index >= 0 && tab_ctrl_->GetHwnd()) {
        RECT rc;
        if (TabCtrl_GetItemRect(tab_ctrl_->GetHwnd(), slot->tab_index, &rc))
            ::InvalidateRect(tab_ctrl_->GetHwnd(), &rc, FALSE);
    }

    MHX_LOG_INFO(L"OnSetTabIcon: slot=%d icon=%p (copied=%p)",
                 sid, src, slot->icon);
    return TRUE;
}

/* ============================================================
 * W6-bugfix: OnRembedRequest
 *
 * 子进程（之前 detach 过的独立窗口）请求合并回 Tab。
 *
 * 流程：
 *   1. 把 child_hwnd 恢复为 child 风格（去 WS_OVERLAPPEDWINDOW、加 WS_CHILD）
 *      — 这步在 TabController::EmbedChildWindow 里已经做过，会覆盖 orig_style
 *   2. child_mgr_->AdoptExternalWindow 分配新 slot + OpenProcess + EmbedChildWindow
 *   3. 注册到 heartbeat
 *   4. 返回新 slot_id 给子进程
 *
 * 子进程收到返回值后要把自己本地的 state.slot_id 更新为新值。
 * ============================================================ */
LRESULT MainFrame::OnRembedRequest(WPARAM child_hwnd_w, LPARAM /*lp*/) {
    HWND child_hwnd = reinterpret_cast<HWND>(child_hwnd_w);
    if (!child_mgr_ || !tab_ctrl_) return -1;

    /* 用窗口标题作为 Tab 初始 title（如果 child 有意义的 caption） */
    wchar_t title_buf[128] = {};
    ::GetWindowTextW(child_hwnd, title_buf, _countof(title_buf));
    String title = title_buf;

    int new_slot_id = child_mgr_->AdoptExternalWindow(child_hwnd, title);
    if (new_slot_id < 0) {
        MHX_LOG_WARN(L"OnRembedRequest: AdoptExternalWindow failed hwnd=%p",
                     child_hwnd);
        return -1;
    }

    /* 心跳重新跟踪 */
    if (heartbeat_) heartbeat_->RegisterSlot(new_slot_id);

    /* 立即发 ACTIVATE_VIEW 让 child 进入 active 态 */
    ipc::PostActivateView(child_hwnd, new_slot_id);

    ::InvalidateRect(hwnd_, nullptr, TRUE);
    MHX_LOG_INFO(L"OnRembedRequest: hwnd=%p -> new slot_id=%d",
                 child_hwnd, new_slot_id);
    return static_cast<LRESULT>(new_slot_id);
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

        case ID_FILE_SETTINGS: {
            /* W6-3: 弹出模态设置对话框，OK 后立即应用新值 */
            SettingsValues sv;
            if (ShowSettingsDialog(hwnd_, settings_ini_path_, sv)) {
                if (heartbeat_) heartbeat_->SetTimings(sv.interval_ms, sv.timeout_ms);

                /* W6-4: 主题变更时重新构造 ITheme 并安装到 TabController。
                 * 仅在名字变化时才换（避免每次 OK 都重建对象，即便用户没改主题）。*/
                if (sv.theme_name != current_theme_name_) {
                    current_theme_name_ = sv.theme_name;
                    if (tab_ctrl_) tab_ctrl_->SetTheme(CreateTheme(sv.theme_name));
                }
                MHX_LOG_INFO(L"Settings applied: interval=%lu timeout=%lu theme=%s",
                             sv.interval_ms, sv.timeout_ms, sv.theme_name.c_str());
            }
            return 0;
        }

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

        case ID_TAB_DETACH: {
            /* P1: 把当前 Tab 分离到一个新 mhtabx 实例
             *
             * 流程：
             *   1. tab_ctrl_->DetachSlot 让 child 变成顶层 WS_OVERLAPPEDWINDOW
             *   2. child_mgr_->DetachSlot 释放 wait handle（不再 wait pid）
             *   3. heartbeat_ 注销 slot
             *   4. SpawnDetachedInstance 启动新 mhtabx 进程接管 child
             *
             * 注意 1 和 2 的顺序：先 UI 改顶层，再放手 wait handle，
             * 这样新进程 OpenProcess 时不会与本进程的 hProcess 冲突。 */
            if (!tab_ctrl_ || !child_mgr_) return 0;
            int sid = tab_ctrl_->GetSelectedSlotId();
            if (sid < 0) return 0;

            auto* slot = tab_ctrl_->FindSlot(sid);
            if (!slot) return 0;
            HWND child_hwnd = slot->child_hwnd;

            child_mgr_->DetachSlot(sid);
            if (heartbeat_) heartbeat_->UnregisterSlot(sid);
            tab_ctrl_->DetachSlot(sid);

            if (child_hwnd && ::IsWindow(child_hwnd)) {
                /* 键盘菜单触发的 detach：让新窗口出现在鼠标当前位置
                 * 比 CW_USEDEFAULT 更直观 */
                POINT cur;
                ::GetCursorPos(&cur);
                if (!child_mgr_->SpawnDetachedInstance(child_hwnd, &cur)) {
                    /* 启动新进程失败：child 已经独立顶层显示，至少不会卡死。
                     * 用户可以手动 F6 合并回任意 mhtabx 实例。 */
                    MHX_LOG_WARN(L"SpawnDetachedInstance failed; child remains top-level");
                }
            }

            ::InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }

        case ID_TAB_RENAME: {
            /* 简单实现：给当前 Tab 追加一个 '*' 后缀 */
            if (!tab_ctrl_) return 0;
            int sid = tab_ctrl_->GetSelectedSlotId();
            auto* slot = tab_ctrl_->FindSlot(sid);
            if (slot) {
                String new_title = slot->title + L" *";
                tab_ctrl_->RenameSlot(sid, new_title);
            }
            return 0;
        }

        case ID_TAB_ACTIVATE: {
            /* W6-1: 右键菜单"激活" - ShowContextMenu 已经先 SelectSlot 过，
             * 这里再做一次保险：把焦点交给当前 child 窗口 */
            if (!tab_ctrl_) return 0;
            int sid = tab_ctrl_->GetSelectedSlotId();
            if (auto* slot = tab_ctrl_->FindSlot(sid)) {
                if (slot->child_hwnd && ::IsWindow(slot->child_hwnd))
                    ::SetFocus(slot->child_hwnd);
            }
            return 0;
        }

        case ID_TAB_COPY_PATH: {
            /* W6-1: 复制 "exe_path [args]" 到剪贴板，方便 cmd 重新拉起 */
            if (!tab_ctrl_) return 0;
            int sid = tab_ctrl_->GetSelectedSlotId();
            auto* slot = tab_ctrl_->FindSlot(sid);
            if (!slot || slot->exe_path.empty()) return 0;

            /* 路径含空格时手动加引号；参数原样追加 */
            String text;
            bool need_quote = slot->exe_path.find(L' ') != String::npos;
            if (need_quote) text.push_back(L'"');
            text += slot->exe_path;
            if (need_quote) text.push_back(L'"');
            if (!slot->cmdline.empty()) {
                text.push_back(L' ');
                text += slot->cmdline;
            }

            /* 标准 Win32 剪贴板写入：
             *   1. OpenClipboard(hwnd) 锁定剪贴板到本进程
             *   2. EmptyClipboard 清空旧数据
             *   3. GMEM_MOVEABLE 分配 + memcpy + SetClipboardData
             *   4. CloseClipboard 后剪贴板"接管"句柄，禁止 GlobalFree */
            if (::OpenClipboard(hwnd_)) {
                ::EmptyClipboard();
                size_t bytes = (text.size() + 1) * sizeof(wchar_t);
                HGLOBAL hmem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (hmem) {
                    if (void* p = ::GlobalLock(hmem)) {
                        memcpy(p, text.c_str(), bytes);
                        ::GlobalUnlock(hmem);
                        if (!::SetClipboardData(CF_UNICODETEXT, hmem)) {
                            /* SetClipboardData 失败时句柄归我们，需释放 */
                            ::GlobalFree(hmem);
                            MHX_LOG_ERROR(L"SetClipboardData failed: %s",
                                utils::FormatSystemError(::GetLastError()).c_str());
                        }
                    } else {
                        ::GlobalFree(hmem);
                    }
                }
                ::CloseClipboard();
                MHX_LOG_INFO(L"Copied to clipboard: %s", text.c_str());
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
 * P2 + P4: OnTabDragOut
 *
 * 用户在 Tab Bar 拖拽某个 tab，松手时鼠标位置在主窗口外。
 * TabController 已剥离自己的 drag 状态，把决策完全交给这里。
 *
 * 决策树：
 *   1. WindowFromPoint(screen_pt) 找出落点最深的窗口
 *   2. 顺着 GetAncestor(GA_ROOT) 找到顶层窗口
 *   3. 顶层窗口的 class name 以 kMainFrameClassPrefix 开头 → mhtabx 主窗口
 *      a. 不是自己（避免自合并 → 死循环）→ 跨实例合并 (MHX_REMBED_REQUEST)
 *      b. 是自己 → 视作 spawn（鼠标抖动恰好回到自己上方）
 *   4. 否则 → SpawnDetachedInstance 启动新 mhtabx 接管 child
 * ============================================================ */
void MainFrame::OnTabDragOut(int slot_id, POINT screen_pt) {
    if (!tab_ctrl_ || !child_mgr_) return;

    auto* slot = tab_ctrl_->FindSlot(slot_id);
    if (!slot) return;
    HWND child_hwnd = slot->child_hwnd;

    /* 1. 先做完 detach，让 child 变独立顶层（与 ID_TAB_DETACH 同节奏） */
    child_mgr_->DetachSlot(slot_id);
    if (heartbeat_) heartbeat_->UnregisterSlot(slot_id);
    tab_ctrl_->DetachSlot(slot_id);

    if (!child_hwnd || !::IsWindow(child_hwnd)) {
        ::InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }

    /* 2. 落点检测 */
    HWND drop = ::WindowFromPoint(screen_pt);
    HWND drop_top = drop ? ::GetAncestor(drop, GA_ROOT) : nullptr;

    bool cross_merge = false;
    if (drop_top && drop_top != hwnd_) {
        wchar_t class_buf[128] = {};
        ::GetClassNameW(drop_top, class_buf, _countof(class_buf));
        /* 主窗口类名以 mhtabx_MainFrame_ 开头 */
        if (wcsncmp(class_buf, kMainFrameClassPrefix,
                    wcslen(kMainFrameClassPrefix)) == 0) {
            cross_merge = true;
            /* 跨进程 SendMessageW 同步发送 reembed 请求。SMTO_ABORTIFHUNG
             * 避免目标实例无响应时阻塞本进程；同时设置 3s 超时。 */
            DWORD_PTR result = 0;
            LRESULT r = ::SendMessageTimeoutW(
                drop_top, MHX_REMBED_REQUEST,
                reinterpret_cast<WPARAM>(child_hwnd), 0,
                SMTO_ABORTIFHUNG, 3000, &result);
            if (!r || static_cast<intptr_t>(result) < 0) {
                MHX_LOG_WARN(L"Cross-merge failed (result=%lld), falling back to spawn",
                             (long long)(intptr_t)result);
                cross_merge = false;
            } else {
                MHX_LOG_INFO(L"Cross-merge OK: hwnd=%p moved to instance %p slot=%lld",
                             child_hwnd, drop_top, (long long)(intptr_t)result);
            }
        }
    }

    /* 3. 不是跨合并 → 启动新 mhtabx 接管
     *
     * 把鼠标松手点作为新窗口期望左上角。稍微向左上偏移让鼠标仍能
     * 落在新主窗口 Tab Bar 附近而不是标题栏里，类似 Chrome 行为。
     * 偏移值走 DPI 无关的保守常量（32px 左、8px 上），主窗口本身会
     * 在 OnPostInit 里做 work-area 裁剪，防止跑到屏幕外。 */
    if (!cross_merge) {
        POINT spawn = { screen_pt.x - 32, screen_pt.y - 8 };
        if (!child_mgr_->SpawnDetachedInstance(child_hwnd, &spawn)) {
            MHX_LOG_WARN(L"SpawnDetachedInstance failed; child remains top-level");
        }
    }

    ::InvalidateRect(hwnd_, nullptr, TRUE);

    /* 4. P5: 拖出后本实例可能已经空了，触发自动退出检查 */
    CheckAutoExit();
}

/* ============================================================
 * P5: CheckAutoExit
 *
 * 任何 mhtabx 实例最后 Tab 关闭后自动退出。
 * 防止启动初期空状态触发的 has_ever_had_tab_ 守卫：必须有过 Tab 才退。
 *
 * 调用点：
 *   - OnTabDragOut 之后（tab 移走）
 *   - OnCleanupView 之后（child 自然退出）
 *   - ChildProcessManager::Poll 之后（异常退出）
 *
 * 用 PostMessage 而不是 SendMessage(WM_CLOSE)，避免在消息处理中重入。
 * ============================================================ */
void MainFrame::CheckAutoExit() {
    if (!tab_ctrl_ || !has_ever_had_tab_) return;
    if (tab_ctrl_->GetActiveCount() == 0) {
        MHX_LOG_INFO(L"Last tab gone, posting WM_CLOSE for auto-exit");
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
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

    /* 0. P1: detach 出来的实例：识别 --mhx-adopt-hwnd 0xHHHH，接管那个 child 窗口
     *
     * 命令行格式：mhtabx.exe --mhx-adopt-hwnd 0x000000007FFE1234 [--mhx-spawn-at X,Y]
     * 解析后调用 AdoptExternalWindow，child 立即变成本实例的首 Tab。
     * 此路径与 LaunchChild / SessionStore 互斥（detach 实例不该再恢复 session）。 */
    if (!pending_cmd_line_.empty()) {
        size_t pos = pending_cmd_line_.find(kArgAdoptHwnd);
        if (pos != String::npos) {
            /* 跳过参数名，找紧跟的 0x... */
            size_t value_pos = pos + wcslen(kArgAdoptHwnd);
            while (value_pos < pending_cmd_line_.size() &&
                   ::iswspace(pending_cmd_line_[value_pos])) ++value_pos;

            /* wcstoull 接受 0x 前缀（base=0） */
            wchar_t* end = nullptr;
            unsigned long long val = ::wcstoull(
                pending_cmd_line_.c_str() + value_pos, &end, 0);

            /* 可选：--mhx-spawn-at X,Y → 把主窗口移到屏幕指定位置
             * 必须在 adopt 之前做，因为 adopt 会 SetParent child → tab_ctrl_，
             * child 的位置会跟随主窗口。 */
            size_t sp_pos = pending_cmd_line_.find(kArgSpawnAt);
            if (sp_pos != String::npos) {
                size_t sv_pos = sp_pos + wcslen(kArgSpawnAt);
                while (sv_pos < pending_cmd_line_.size() &&
                       ::iswspace(pending_cmd_line_[sv_pos])) ++sv_pos;
                wchar_t* cursor = const_cast<wchar_t*>(pending_cmd_line_.c_str() + sv_pos);
                long sx = ::wcstol(cursor, &cursor, 10);
                if (cursor && *cursor == L',') {
                    long sy = ::wcstol(cursor + 1, nullptr, 10);
                    /* 裁剪到最近监视器 work area，防止跑到屏幕外 */
                    POINT pt = { sx, sy };
                    HMONITOR mon = ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi = { sizeof(mi) };
                    if (mon && ::GetMonitorInfoW(mon, &mi)) {
                        RECT wa = mi.rcWork;
                        RECT wr; ::GetWindowRect(hwnd_, &wr);
                        int w = wr.right  - wr.left;
                        int h = wr.bottom - wr.top;
                        if (sx + w > wa.right)  sx = wa.right  - w;
                        if (sy + h > wa.bottom) sy = wa.bottom - h;
                        if (sx < wa.left) sx = wa.left;
                        if (sy < wa.top)  sy = wa.top;
                    }
                    ::SetWindowPos(hwnd_, nullptr,
                                   static_cast<int>(sx), static_cast<int>(sy),
                                   0, 0,
                                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    MHX_LOG_INFO(L"OnPostInit spawn_at: (%ld, %ld)", sx, sy);
                }
            }

            HWND target = reinterpret_cast<HWND>(static_cast<uintptr_t>(val));
            if (target && ::IsWindow(target)) {
                wchar_t title_buf[128] = {};
                ::GetWindowTextW(target, title_buf, _countof(title_buf));
                int new_slot = child_mgr_->AdoptExternalWindow(target, title_buf);
                if (new_slot >= 0) {
                    if (heartbeat_) heartbeat_->RegisterSlot(new_slot);
                    ipc::PostActivateView(target, new_slot);
                    MHX_LOG_INFO(L"OnPostInit adopt OK: hwnd=%p slot=%d", target, new_slot);
                } else {
                    MHX_LOG_ERROR(L"OnPostInit adopt failed for hwnd=%p", target);
                }
            } else {
                MHX_LOG_ERROR(L"OnPostInit: --mhx-adopt-hwnd invalid 0x%llX", val);
            }
            pending_cmd_line_.clear();
            return;     /* 不再走 LaunchChild / session 恢复 */
        }
    }

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
 * W5-1/W5-2: SetStatusBarPartsForDpi
 *
 * 基础分区宽度按 96 DPI 设计：160 / 400 / 剩余
 * 按当前 DPI 用 MulDiv 缩放，保证在 125/150/200% 下视觉一致。
 * ============================================================ */
void MainFrame::SetStatusBarPartsForDpi(UINT dpi) {
    if (!status_bar_) return;
    int p0 = ::MulDiv(160, static_cast<int>(dpi), 96);
    int p1 = ::MulDiv(560, static_cast<int>(dpi), 96);
    int parts[3] = { p0, p1, -1 };
    ::SendMessageW(status_bar_, SB_SETPARTS, 3,
                   reinterpret_cast<LPARAM>(parts));
}

/* ============================================================
 * W5-2: OnDpiChanged
 *
 * 窗口从一个显示器拖到另一个 DPI 不同的显示器时，Windows 会发送
 * 此消息并在 lParam 里给出建议的新窗口 RECT。我们必须：
 *   1. SetWindowPos 到建议 RECT（尺寸已按新 DPI 预放大/缩小）
 *   2. 记录新的 DPI 给 UI 元素后续使用
 *   3. 重算像素尺寸相关 UI（如 StatusBar parts）
 *
 * 返回 0 表示已处理。
 * ============================================================ */
LRESULT MainFrame::OnDpiChanged(UINT new_dpi, const RECT* suggested) {
    MHX_LOG_INFO(L"WM_DPICHANGED: %u -> %u", current_dpi_, new_dpi);
    current_dpi_ = new_dpi ? new_dpi : 96;

    if (suggested) {
        ::SetWindowPos(hwnd_, nullptr,
                       suggested->left, suggested->top,
                       suggested->right  - suggested->left,
                       suggested->bottom - suggested->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* StatusBar parts 按新 DPI 重算 */
    SetStatusBarPartsForDpi(current_dpi_);

    /* 主题重绘 TabController 的 owner-draw 部分 */
    if (tab_ctrl_ && tab_ctrl_->GetHwnd()) {
        ::InvalidateRect(tab_ctrl_->GetHwnd(), nullptr, TRUE);
    }
    return 0;
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
        const wchar_t* state = ToString(slot->state);   /* 来自 common.h */
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
