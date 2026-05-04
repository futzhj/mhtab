/**
 * mhtabx - TabController 实现
 */

#include "TabController.h"
#include "Utils.h"
#include "resource/resource.h"

#include <commctrl.h>
#pragma comment(lib, "comctl32")

namespace mhx {

constexpr int      kTabHeaderHeight = 28;
constexpr UINT_PTR kTabSubclassId   = 0xCAFE0001;

/* ============================================================
 * 构造 / 析构
 * ============================================================ */
TabController::TabController() {
    /* 默认装上现代扁平主题，可通过 SetTheme 替换 */
    theme_ = std::make_unique<FlatModernTheme>();
}

TabController::~TabController() {
    if (tab_ctrl_) {
        ::RemoveWindowSubclass(tab_ctrl_, &TabController::TabSubclassProc, kTabSubclassId);
    }
    /* 析构时还原所有 child 窗口的 parent，避免 child 进程被牵连销毁 */
    for (auto& slot_ptr : slots_) {
        if (slot_ptr && slot_ptr->child_hwnd && slot_ptr->orig_parent) {
            ::SetParent(slot_ptr->child_hwnd, slot_ptr->orig_parent);
            ::SetWindowLongPtrW(slot_ptr->child_hwnd, GWL_STYLE, slot_ptr->orig_style);
        }
    }
}

/* ============================================================
 * 创建
 * ============================================================ */
bool TabController::Create(HWND parent, HINSTANCE hInst, const RECT& rc, int ctrl_id) {
    parent_    = parent;
    hInstance_ = hInst;

    /* TCS_OWNERDRAWFIXED 允许 SysTabControl32 将每个标签以 WM_DRAWITEM 发出 */
    DWORD style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER;
    if (theme_) style |= TCS_OWNERDRAWFIXED;

    tab_ctrl_ = ::CreateWindowExW(
        0, WC_TABCONTROLW, L"",
        style,
        rc.left, rc.top,
        rc.right - rc.left, rc.bottom - rc.top,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ctrl_id)),
        hInst, nullptr);

    if (!tab_ctrl_) {
        MHX_LOG_ERROR(L"CreateWindowExW(WC_TABCONTROL) failed: %s",
                      utils::FormatSystemError(::GetLastError()).c_str());
        return false;
    }

    /* 启用主题字体 */
    HFONT font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    ::SendMessageW(tab_ctrl_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    /* 安装 subclass 拦截鼠标事件以支持 Tab 拖拽 */
    ::SetWindowSubclass(tab_ctrl_, &TabController::TabSubclassProc,
                        kTabSubclassId,
                        reinterpret_cast<DWORD_PTR>(this));

    MHX_LOG_INFO(L"TabController created: hwnd=%p", tab_ctrl_);
    return true;
}

/* ============================================================
 * 调整大小
 * ============================================================ */
void TabController::Resize(int cx, int cy) {
    if (!tab_ctrl_) return;
    ::SetWindowPos(tab_ctrl_, nullptr, 0, 0, cx, cy,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    /* 同步当前选中 slot 的子窗口 */
    if (auto* slot = FindSlot(selected_slot_id_)) {
        if (slot->child_hwnd) LayoutChildWindow(*slot);
    }
}

/* ============================================================
 * Notify 处理（TCN_SELCHANGE）
 * ============================================================ */
LRESULT TabController::HandleNotify(NMHDR* hdr) {
    if (!hdr || hdr->hwndFrom != tab_ctrl_) return 0;

    if (hdr->code == TCN_SELCHANGE) {
        int idx = TabCtrl_GetCurSel(tab_ctrl_);
        TCITEMW item = {};
        item.mask = TCIF_PARAM;
        if (TabCtrl_GetItem(tab_ctrl_, idx, &item)) {
            int slot_id = static_cast<int>(item.lParam);
            SelectSlot(slot_id);
        }
        return TRUE;
    }
    return 0;
}

/* ============================================================
 * 添加 / 嵌入 / 移除 / 选中
 * ============================================================ */
int TabController::AddSlot(ChildSlot slot) {
    /* 寻找空闲槽位（slot_id == -1） */
    int new_id = -1;
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i] || slots_[i]->slot_id == -1) {
            new_id = static_cast<int>(i);
            break;
        }
    }
    if (new_id == -1) {
        new_id = static_cast<int>(slots_.size());
        slots_.emplace_back();
    }

    slot.slot_id = new_id;
    slots_[new_id] = std::make_unique<ChildSlot>(std::move(slot));

    MHX_LOG_INFO(L"AddSlot id=%d pid=%lu cmdline=%s",
                 new_id, slots_[new_id]->pid, slots_[new_id]->cmdline.c_str());
    return new_id;
}

bool TabController::EmbedChildWindow(int slot_id, HWND child_hwnd) {
    auto* slot = FindSlot(slot_id);
    if (!slot || !child_hwnd || !::IsWindow(child_hwnd)) return false;

    /* 保存原 parent / style，析构或 RemoveSlot 时复原 */
    slot->orig_parent = ::GetParent(child_hwnd);
    slot->orig_style  = ::GetWindowLongPtrW(child_hwnd, GWL_STYLE);
    slot->child_hwnd  = child_hwnd;

    /* 切换为 child 风格 */
    LONG_PTR new_style = (slot->orig_style & ~(WS_POPUP | WS_OVERLAPPEDWINDOW))
                       | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
    ::SetWindowLongPtrW(child_hwnd, GWL_STYLE, new_style);

    if (!::SetParent(child_hwnd, tab_ctrl_)) {
        MHX_LOG_ERROR(L"SetParent failed for slot=%d: %s",
                      slot_id, utils::FormatSystemError(::GetLastError()).c_str());
        ::SetWindowLongPtrW(child_hwnd, GWL_STYLE, slot->orig_style);
        slot->child_hwnd = nullptr;
        return false;
    }

    /* 插入 Tab 项 */
    TCITEMW item = {};
    item.mask    = TCIF_TEXT | TCIF_PARAM;
    item.pszText = const_cast<LPWSTR>(slot->title.c_str());
    item.lParam  = slot_id;

    int tab_idx = TabCtrl_InsertItem(tab_ctrl_,
                                     TabCtrl_GetItemCount(tab_ctrl_), &item);
    if (tab_idx < 0) {
        MHX_LOG_ERROR(L"TabCtrl_InsertItem failed for slot=%d", slot_id);
        return false;
    }
    slot->tab_index = tab_idx;
    slot->state     = ChildState::Running;

    LayoutChildWindow(*slot);
    SelectSlot(slot_id);

    MHX_LOG_INFO(L"EmbedChildWindow slot=%d hwnd=%p tab_idx=%d",
                 slot_id, child_hwnd, tab_idx);
    return true;
}

bool TabController::RemoveSlot(int slot_id) {
    auto* slot = FindSlot(slot_id);
    if (!slot) return false;

    /* 还原 child 窗口 parent + style，让子进程能独立继续运行（或自然退出） */
    if (slot->child_hwnd && ::IsWindow(slot->child_hwnd)) {
        ::SetParent(slot->child_hwnd, slot->orig_parent);
        ::SetWindowLongPtrW(slot->child_hwnd, GWL_STYLE, slot->orig_style);
        ::ShowWindow(slot->child_hwnd, SW_HIDE);
    }

    /* 删除 Tab 项 */
    if (slot->tab_index >= 0 && tab_ctrl_) {
        TabCtrl_DeleteItem(tab_ctrl_, slot->tab_index);
        /* 删除后所有比它大的 tab_index 都会下移 */
        for (auto& p : slots_) {
            if (p && p.get() != slot && p->tab_index > slot->tab_index)
                --p->tab_index;
        }
    }

    /* 标记空闲（保持 slots_ 中位置以维持 id 稳定） */
    slots_[slot_id]->slot_id = -1;
    slots_[slot_id]->state   = ChildState::Dead;
    slots_[slot_id].reset();

    /* 如果删除的是当前选中，切到第一个有效 slot */
    if (selected_slot_id_ == slot_id) {
        selected_slot_id_ = -1;
        for (auto& p : slots_) {
            if (p && p->slot_id >= 0) { SelectSlot(p->slot_id); break; }
        }
    }

    MHX_LOG_INFO(L"RemoveSlot id=%d", slot_id);
    return true;
}

/* ============================================================
 * W5-4: DetachSlot
 *
 * 类似 RemoveSlot，但：
 *   - 恢复 child 后调用 ShowWindow(SW_SHOWNORMAL) 而非 SW_HIDE
 *   - 把 child 移到主窗口右下方一个可见位置
 *   - 置为 foreground 让用户知道窗口"飞出来"了
 * ============================================================ */
bool TabController::DetachSlot(int slot_id) {
    auto* slot = FindSlot(slot_id);
    if (!slot) return false;

    HWND     child       = slot->child_hwnd;
    HWND     orig_parent = slot->orig_parent;     /* 通常为 nullptr（顶层） */
    LONG_PTR orig_style  = slot->orig_style;
    int      tab_index   = slot->tab_index;

    if (!child || !::IsWindow(child)) {
        MHX_LOG_WARN(L"DetachSlot: invalid child hwnd for slot %d", slot_id);
        return false;
    }

    /* 1. 恢复独立顶层窗口 */
    ::SetParent(child, orig_parent);
    ::SetWindowLongPtrW(child, GWL_STYLE, orig_style);

    /* 2. 从 Tab 移除（同 RemoveSlot 逻辑） */
    if (tab_index >= 0 && tab_ctrl_) {
        TabCtrl_DeleteItem(tab_ctrl_, tab_index);
        for (auto& p : slots_) {
            if (p && p.get() != slot && p->tab_index > tab_index) --p->tab_index;
        }
    }

    /* 3. 清除 slot 在 slots_ 中的条目（避免析构时再 SetParent 回 orig_parent） */
    slot->child_hwnd = nullptr;
    slot->orig_parent = nullptr;
    slots_[slot_id]->slot_id = -1;
    slots_[slot_id]->state   = ChildState::Dead;
    slots_[slot_id].reset();

    /* 4. 定位独立窗口到主窗口右下偏移，保证可见 */
    RECT main_rc = {};
    if (parent_) ::GetWindowRect(parent_, &main_rc);
    int x = main_rc.left + 60;
    int y = main_rc.top  + 60;
    ::SetWindowPos(child, nullptr, x, y, 800, 600,
                   SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    ::SetForegroundWindow(child);

    /* 5. 如果 detach 的是 selected slot，切到下一个有效 slot */
    if (selected_slot_id_ == slot_id) {
        selected_slot_id_ = -1;
        for (auto& p : slots_) {
            if (p && p->slot_id >= 0) { SelectSlot(p->slot_id); break; }
        }
    }

    MHX_LOG_INFO(L"DetachSlot id=%d child=%p", slot_id, child);
    return true;
}

void TabController::SelectSlot(int slot_id) {
    /* 隐藏其他所有子窗口 */
    for (auto& p : slots_) {
        if (p && p->child_hwnd && p->slot_id != slot_id)
            ::ShowWindow(p->child_hwnd, SW_HIDE);
    }

    auto* slot = FindSlot(slot_id);
    if (slot) {
        if (slot->tab_index >= 0)
            TabCtrl_SetCurSel(tab_ctrl_, slot->tab_index);
        if (slot->child_hwnd) {
            LayoutChildWindow(*slot);
            ::ShowWindow(slot->child_hwnd, SW_SHOWNA);
            ::SetFocus(slot->child_hwnd);
        }
    }
    selected_slot_id_ = slot_id;
}

/* ============================================================
 * 查找
 * ============================================================ */
size_t TabController::GetActiveCount() const noexcept {
    size_t n = 0;
    for (auto& p : slots_) {
        if (p && p->slot_id >= 0 && p->state != ChildState::Dead) ++n;
    }
    return n;
}

ChildSlot* TabController::FindSlot(int slot_id) noexcept {
    if (slot_id < 0 || slot_id >= static_cast<int>(slots_.size())) return nullptr;
    auto& p = slots_[slot_id];
    if (!p || p->slot_id != slot_id) return nullptr;
    return p.get();
}

ChildSlot* TabController::FindSlotByPid(DWORD pid) noexcept {
    for (auto& p : slots_)
        if (p && p->slot_id >= 0 && p->pid == pid) return p.get();
    return nullptr;
}

ChildSlot* TabController::FindSlotByHwnd(HWND hwnd) noexcept {
    for (auto& p : slots_)
        if (p && p->slot_id >= 0 && p->child_hwnd == hwnd) return p.get();
    return nullptr;
}

/* ============================================================
 * 内部
 * ============================================================ */
void TabController::LayoutChildWindow(const ChildSlot& slot) const {
    if (!tab_ctrl_ || !slot.child_hwnd) return;

    RECT rc;
    ::GetClientRect(tab_ctrl_, &rc);
    /* TabCtrl_AdjustRect: 把 client rect 调整成 Tab 内容区 */
    TabCtrl_AdjustRect(tab_ctrl_, FALSE, &rc);

    ::SetWindowPos(slot.child_hwnd, nullptr,
                   rc.left, rc.top,
                   rc.right - rc.left, rc.bottom - rc.top,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

RECT TabController::GetDisplayArea() const {
    RECT rc{};
    if (!tab_ctrl_) return rc;
    ::GetClientRect(tab_ctrl_, &rc);
    TabCtrl_AdjustRect(tab_ctrl_, FALSE, &rc);
    return rc;
}

/* ============================================================
 * W4: 切换 Tab
 * ============================================================ */
void TabController::SelectNext() {
    if (!tab_ctrl_) return;
    int n = TabCtrl_GetItemCount(tab_ctrl_);
    if (n <= 1) return;
    int cur = TabCtrl_GetCurSel(tab_ctrl_);
    int next_idx = (cur + 1) % n;

    TCITEMW item = {};
    item.mask = TCIF_PARAM;
    if (TabCtrl_GetItem(tab_ctrl_, next_idx, &item)) {
        SelectSlot(static_cast<int>(item.lParam));
    }
}

void TabController::SelectPrev() {
    if (!tab_ctrl_) return;
    int n = TabCtrl_GetItemCount(tab_ctrl_);
    if (n <= 1) return;
    int cur = TabCtrl_GetCurSel(tab_ctrl_);
    int prev_idx = (cur - 1 + n) % n;

    TCITEMW item = {};
    item.mask = TCIF_PARAM;
    if (TabCtrl_GetItem(tab_ctrl_, prev_idx, &item)) {
        SelectSlot(static_cast<int>(item.lParam));
    }
}

/* ============================================================
 * W4: 重命名
 * ============================================================ */
bool TabController::RenameSlot(int slot_id, const String& new_title) {
    auto* slot = FindSlot(slot_id);
    if (!slot || slot->tab_index < 0 || !tab_ctrl_) return false;

    slot->title = new_title;
    TCITEMW item = {};
    item.mask    = TCIF_TEXT;
    item.pszText = const_cast<LPWSTR>(slot->title.c_str());
    BOOL ok = TabCtrl_SetItem(tab_ctrl_, slot->tab_index, &item);
    return ok != FALSE;
}

/* ============================================================
 * W4: Tab 重排
 *
 * 思路：保留当前 selected slot_id，把整个 tab 列表 DeleteAllItems，
 * 按新顺序重新 InsertItem，更新所有 slot.tab_index，然后恢复 select。
 * 这样实现简单且对任意 src/dst 都正确。
 * ============================================================ */
bool TabController::ReorderTabs(int src_idx, int dst_idx) {
    if (!tab_ctrl_) return false;
    int n = TabCtrl_GetItemCount(tab_ctrl_);
    if (src_idx < 0 || src_idx >= n) return false;
    if (dst_idx < 0 || dst_idx >= n) return false;
    if (src_idx == dst_idx) return false;

    /* 1. 收集当前顺序的 slot_id 数组 */
    std::vector<int> order;
    order.reserve(n);
    for (int i = 0; i < n; ++i) {
        TCITEMW item = {};
        item.mask = TCIF_PARAM;
        if (TabCtrl_GetItem(tab_ctrl_, i, &item)) {
            order.push_back(static_cast<int>(item.lParam));
        }
    }
    if ((int)order.size() != n) return false;

    /* 2. 在数组里挪动 */
    int moved_slot = order[src_idx];
    order.erase(order.begin() + src_idx);
    order.insert(order.begin() + dst_idx, moved_slot);

    /* 3. 记录当前 selected slot_id（重排后保持选中同一个 slot） */
    int sel_slot = selected_slot_id_;

    /* 4. 重建 tab 列表 */
    TabCtrl_DeleteAllItems(tab_ctrl_);
    for (int i = 0; i < (int)order.size(); ++i) {
        auto* slot = FindSlot(order[i]);
        if (!slot) continue;
        TCITEMW item = {};
        item.mask    = TCIF_TEXT | TCIF_PARAM;
        item.pszText = const_cast<LPWSTR>(slot->title.c_str());
        item.lParam  = slot->slot_id;
        TabCtrl_InsertItem(tab_ctrl_, i, &item);
        slot->tab_index = i;
    }

    /* 5. 恢复选中 */
    if (sel_slot >= 0) SelectSlot(sel_slot);

    MHX_LOG_INFO(L"ReorderTabs: src=%d dst=%d slot=%d",
                 src_idx, dst_idx, moved_slot);
    return true;
}

/* ============================================================
 * W4: HitTest
 * ============================================================ */
int TabController::HitTestTab(int x, int y) const {
    if (!tab_ctrl_) return -1;
    TCHITTESTINFO info = {};
    info.pt.x = x;
    info.pt.y = y;
    int idx = TabCtrl_HitTest(tab_ctrl_, &info);
    return (idx >= 0) ? idx : -1;
}

/* ============================================================
 * W4: Tab subclass thunk
 * ============================================================ */
LRESULT CALLBACK TabController::TabSubclassProc(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR /*id_subclass*/, DWORD_PTR ref_data) {

    auto* self = reinterpret_cast<TabController*>(ref_data);
    if (self) {
        LRESULT r = self->HandleTabMessage(msg, wp, lp);
        if (r != 0) return r;   /* 已处理 */
    }
    return ::DefSubclassProc(hwnd, msg, wp, lp);
}

/* ============================================================
 * W4: 拖拽消息处理
 *
 * 返回值约定：
 *   - 0  → 调用方继续默认处理（DefSubclassProc）
 *   - 非 0 → 已完全处理，跳过默认处理
 * ============================================================ */
LRESULT TabController::HandleTabMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int idx = HitTestTab(x, y);
            if (idx >= 0) {
                drag_armed_   = true;
                drag_active_  = false;
                drag_src_idx_ = idx;
                drag_start_pt_.x = x;
                drag_start_pt_.y = y;
                /* 让默认处理切换 selected tab */
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);

            /* --- Hot tracking：注册 WM_MOUSELEAVE 并刷新 hot tab --- */
            if (!tracking_) {
                TRACKMOUSEEVENT tme = {};
                tme.cbSize    = sizeof(tme);
                tme.dwFlags   = TME_LEAVE;
                tme.hwndTrack = tab_ctrl_;
                if (::TrackMouseEvent(&tme)) tracking_ = true;
            }
            int new_hot = HitTestTab(x, y);
            if (new_hot != hot_idx_) {
                RECT rc;
                if (hot_idx_ >= 0 && TabCtrl_GetItemRect(tab_ctrl_, hot_idx_, &rc))
                    ::InvalidateRect(tab_ctrl_, &rc, FALSE);
                if (new_hot >= 0 && TabCtrl_GetItemRect(tab_ctrl_, new_hot, &rc))
                    ::InvalidateRect(tab_ctrl_, &rc, FALSE);
                hot_idx_ = new_hot;
            }

            /* --- 拖拽阈值检测（仅在 LBUTTONDOWN 命中过 tab 后） --- */
            if (drag_armed_ && (wp & MK_LBUTTON) != 0) {
                int dx = std::abs(x - drag_start_pt_.x);
                int dy = std::abs(y - drag_start_pt_.y);
                if (!drag_active_ && (dx > ::GetSystemMetrics(SM_CXDRAG) ||
                                      dy > ::GetSystemMetrics(SM_CYDRAG))) {
                    drag_active_ = true;
                    ::SetCapture(tab_ctrl_);
                }
                if (drag_active_) {
                    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (drag_active_) {
                int x = GET_X_LPARAM(lp);
                int y = GET_Y_LPARAM(lp);
                int dst = HitTestTab(x, y);
                if (dst >= 0 && dst != drag_src_idx_) {
                    ReorderTabs(drag_src_idx_, dst);
                }
                ::ReleaseCapture();
            }
            drag_armed_   = false;
            drag_active_  = false;
            drag_src_idx_ = -1;
            return 0;
        }

        case WM_CAPTURECHANGED:
            drag_armed_   = false;
            drag_active_  = false;
            drag_src_idx_ = -1;
            return 0;

        case WM_MOUSELEAVE: {
            tracking_ = false;
            if (hot_idx_ >= 0) {
                RECT rc;
                if (TabCtrl_GetItemRect(tab_ctrl_, hot_idx_, &rc))
                    ::InvalidateRect(tab_ctrl_, &rc, FALSE);
                hot_idx_ = -1;
            }
            return 0;
        }

        /* W6-1: Tab 头右键弹出 context menu。
         * 在 RBUTTONUP（非 DOWN）触发更接近 Windows 习惯，
         * 命中 tab 才弹，未命中走默认处理。 */
        case WM_RBUTTONUP: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int idx = HitTestTab(x, y);
            if (idx < 0) return 0;

            POINT pt = { x, y };
            ::ClientToScreen(tab_ctrl_, &pt);
            ShowContextMenu(idx, pt.x, pt.y);
            return 1;   /* 已处理 */
        }
    }
    return 0;
}

/* ============================================================
 * W6-1: ShowContextMenu
 *
 * 加载 IDR_TAB_CONTEXT_MENU 资源，取第一个 popup 子菜单，在 (sx, sy)
 * 弹出。先把目标 tab 切到 selected，让命令作用于该 tab。
 *
 * TPM_LEFTALIGN | TPM_RIGHTBUTTON 是 Windows Explorer 风格：
 *   左对齐到光标，右键也能选中 menu item。
 *
 * 注意 LoadMenuW 返回的 root 必须 DestroyMenu，否则资源泄漏。
 * ============================================================ */
void TabController::ShowContextMenu(int tab_idx, int sx, int sy) {
    if (!tab_ctrl_ || !parent_ || tab_idx < 0) return;

    /* 切到目标 tab，让后续命令作用于它 */
    TCITEMW item = {};
    item.mask = TCIF_PARAM;
    if (!TabCtrl_GetItem(tab_ctrl_, tab_idx, &item)) return;
    int slot_id = static_cast<int>(item.lParam);
    SelectSlot(slot_id);

    HMENU root = ::LoadMenuW(hInstance_, MAKEINTRESOURCEW(IDR_TAB_CONTEXT_MENU));
    if (!root) {
        MHX_LOG_ERROR(L"LoadMenu(IDR_TAB_CONTEXT_MENU) failed: %s",
                      utils::FormatSystemError(::GetLastError()).c_str());
        return;
    }
    HMENU sub = ::GetSubMenu(root, 0);
    if (sub) {
        /* 命令通过 WM_COMMAND 自动发到 parent_，不需要 TPM_RETURNCMD */
        ::TrackPopupMenu(sub,
                         TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                         sx, sy, 0, parent_, nullptr);
    }
    ::DestroyMenu(root);
}

/* ============================================================
 * W4-3: SetTheme
 *
 * 替换主题后强制刷新 tab control 让新风格立即生效。
 * 注意：TCS_OWNERDRAWFIXED 是创建时确定的，运行时切换主题
 * 不会重新加这个样式（如需切换到默认皮肤需重建窗口，本接口够用）。
 * ============================================================ */
void TabController::SetTheme(std::unique_ptr<ITheme> theme) {
    theme_ = std::move(theme);
    if (tab_ctrl_) ::InvalidateRect(tab_ctrl_, nullptr, TRUE);
}

/* ============================================================
 * W4-3: OnDrawItem - 由 MainFrame 路由的 WM_DRAWITEM
 *
 * SysTabControl32 在 TCS_OWNERDRAWFIXED 模式下，每个 tab 头需要绘制时
 * 会向 parent 发送 WM_DRAWITEM。我们用这里的钩子委托给 ITheme。
 * ============================================================ */
LRESULT TabController::OnDrawItem(DRAWITEMSTRUCT* dis) {
    if (!dis || dis->hwndItem != tab_ctrl_ || !theme_) return FALSE;

    int tab_idx = static_cast<int>(dis->itemID);
    if (tab_idx < 0) return FALSE;

    /* 取 lParam 反查 slot */
    TCITEMW item = {};
    item.mask = TCIF_PARAM;
    const ChildSlot* slot = nullptr;
    if (TabCtrl_GetItem(tab_ctrl_, tab_idx, &item)) {
        slot = FindSlot(static_cast<int>(item.lParam));
    }

    TabPaintContext ctx;
    ctx.hdc         = dis->hDC;
    ctx.rect        = dis->rcItem;
    ctx.slot        = slot;
    ctx.tab_index   = tab_idx;
    ctx.is_selected = (TabCtrl_GetCurSel(tab_ctrl_) == tab_idx);
    ctx.is_hot      = (hot_idx_ == tab_idx);
    ctx.is_pushed   = (drag_armed_ && drag_src_idx_ == tab_idx);

    theme_->DrawTab(ctx);
    return TRUE;
}

} /* namespace mhx */
