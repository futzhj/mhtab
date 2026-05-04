/**
 * mhtabx - TabController 实现
 */

#include "TabController.h"
#include "Utils.h"

namespace mhx {

constexpr int kTabHeaderHeight = 28;

/* ============================================================
 * 构造 / 析构
 * ============================================================ */
TabController::TabController() = default;

TabController::~TabController() {
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

    tab_ctrl_ = ::CreateWindowExW(
        0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER,
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

} /* namespace mhx */
