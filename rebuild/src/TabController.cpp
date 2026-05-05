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
    /* P3: safety net - 万一拖拽中程序退出，也别留下幽灵预览窗口 */
    DestroyDragPreview();
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

    /* 1. 恢复独立顶层窗口
     *
     * BUG 历史：曾经直接 SetWindowLongPtr(child, GWL_STYLE, orig_style)
     *  子进程以 embedded=true 启动时 orig_style 通常是 WS_POPUP，
     *  原样恢复会让分离后的顶层窗口没有标题栏和最小/最大/关闭按钮。
     *
     * 修复：分离时强制赋予 WS_OVERLAPPEDWINDOW（标题栏+边框+系统菜单+
     *  最大化+最小化+调整大小），保留 orig_style 里的自定义 bit，但
     *  清掉 WS_CHILD / WS_POPUP 这两个与"顶层窗口"语义冲突的样式。
     *
     *  EX_STYLE 也要剥离 WS_EX_NOACTIVATE 之类会让窗口无法激活的标志，
     *  但大多数子进程不会设置这些，默认保留即可。 */
    ::SetParent(child, orig_parent);

    LONG_PTR detach_style = (orig_style & ~(WS_CHILD | WS_POPUP))
                          | WS_OVERLAPPEDWINDOW
                          | WS_VISIBLE;
    ::SetWindowLongPtrW(child, GWL_STYLE, detach_style);

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

/* P2: 把 TabCtrl 的 tab index 反查为 slot_id */
int TabController::TabIdxToSlotId(int tab_idx) const noexcept {
    if (tab_idx < 0) return -1;
    for (const auto& s : slots_) {
        if (s && s->slot_id >= 0 && s->tab_index == tab_idx) {
            return s->slot_id;
        }
    }
    return -1;
}

/* ============================================================
 * P3: 拖拽迷你预览窗口
 *
 * 实现要点：
 *   - 自定义 WindowClass "mhx_DragPreview"，避免与系统 class 冲突
 *   - WS_POPUP + 无边框；EX 标志加 LAYERED + TOOLWINDOW + TOPMOST + NOACTIVATE
 *     - LAYERED   做半透明
 *     - TOOLWINDOW 不在任务栏出现
 *     - NOACTIVATE 鼠标移到 preview 上不夺焦点
 *   - SetLayeredWindowAttributes(LWA_ALPHA, 0xC0) 75% 不透明
 *   - WM_PAINT 中读 preview_tab_idx_ 对应的 slot.title 绘制
 *
 * 实例方法 vs 静态：WindowClass 注册和 thunk 是 static，但
 * WM_PAINT 需要拿 TabController 实例，通过 GWLP_USERDATA 传递 this。
 * ============================================================ */

namespace {
constexpr const wchar_t* kDragPreviewClass = L"mhx_DragPreview";
/* 预览窗口布局：
 *   - 顶部 kPreviewHeaderH 高度的 header 区（始终绘制：icon + title 文本）
 *   - 若成功拍到 child 缩略图 → 扩展高度到 kPreviewHFull，下方为缩略图区
 *   - 拍摄失败（child 是 OpenGL/D3D 等无法 PrintWindow 的窗口）
 *     → 仅保留 header，高度退回 kPreviewHHeader，和以前一样 */
constexpr int  kPreviewW          = 240;
constexpr int  kPreviewHHeader    = 28;
constexpr int  kPreviewHFull      = 140;
constexpr int  kPreviewThumbMargin = 4;
constexpr BYTE kPreviewAlpha      = 0xC0;    /* ~75% */
} /* namespace */

void TabController::EnsurePreviewClassRegistered(HINSTANCE hInst) {
    /* 通过 GetClassInfoExW 检测是否已注册（idempotent） */
    WNDCLASSEXW probe = {};
    probe.cbSize = sizeof(probe);
    if (::GetClassInfoExW(hInst, kDragPreviewClass, &probe)) return;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = TabController::PreviewWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;          /* 自己 paint，不让系统填 */
    wc.lpszClassName = kDragPreviewClass;
    ::RegisterClassExW(&wc);
}

LRESULT CALLBACK TabController::PreviewWndProc(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {

    /* 实例指针通过 WM_NCCREATE 时的 CREATESTRUCT.lpCreateParams 传入 */
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }

    auto* self = reinterpret_cast<TabController*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->HandlePreviewMessage(hwnd, msg, wp, lp);
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TabController::HandlePreviewMessage(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = ::BeginPaint(hwnd, &ps);

            RECT rc = {};
            ::GetClientRect(hwnd, &rc);

            COLORREF bg = RGB(0x00, 0x78, 0xD4);     /* fallback 蓝 */
            COLORREF fg = RGB(0xFF, 0xFF, 0xFF);

            /* 计算 header / thumbnail 两个子区域 */
            const bool has_thumb = (preview_snapshot_ != nullptr);
            RECT header_rc = rc;
            RECT thumb_rc  = {};
            if (has_thumb) {
                header_rc.bottom = rc.top + kPreviewHHeader;
                thumb_rc = rc;
                thumb_rc.top = header_rc.bottom;
            }

            /* 1. header 背景：蓝色 */
            HBRUSH br = ::CreateSolidBrush(bg);
            ::FillRect(hdc, &header_rc, br);
            ::DeleteObject(br);

            /* 2. thumbnail 背景 + 缩略图（若有） */
            if (has_thumb) {
                HBRUSH thumb_bg = ::CreateSolidBrush(RGB(0x1E, 0x1E, 0x1E));
                ::FillRect(hdc, &thumb_rc, thumb_bg);
                ::DeleteObject(thumb_bg);

                /* BitBlt 快照到 thumb 区域，居中 kPreviewThumbMargin 边距 */
                HDC mem_dc = ::CreateCompatibleDC(hdc);
                HGDIOBJ prev = ::SelectObject(mem_dc, preview_snapshot_);
                BITMAP bm = {};
                ::GetObjectW(preview_snapshot_, sizeof(bm), &bm);
                int x = thumb_rc.left + kPreviewThumbMargin;
                int y = thumb_rc.top  + kPreviewThumbMargin;
                ::BitBlt(hdc, x, y, bm.bmWidth, bm.bmHeight,
                         mem_dc, 0, 0, SRCCOPY);
                ::SelectObject(mem_dc, prev);
                ::DeleteDC(mem_dc);
            }

            /* 3. 整体 1px 白色外边框 */
            ::FrameRect(hdc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));

            /* 4. header：icon + title 文字（与 Tab 绘制布局一致） */
            String title = L"Tab";
            HICON  icon  = nullptr;
            int sid = TabIdxToSlotId(preview_tab_idx_);
            if (sid >= 0) {
                if (auto* s = FindSlot(sid)) {
                    if (!s->title.empty()) title = s->title;
                    icon = s->icon;
                }
            }

            ::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, fg);
            RECT text_rc = header_rc;
            text_rc.left  += 8;
            text_rc.right -= 8;

            if (icon) {
                constexpr int kIconSize = 16;
                int icon_y = header_rc.top +
                             ((header_rc.bottom - header_rc.top) - kIconSize) / 2;
                ::DrawIconEx(hdc, text_rc.left, icon_y, icon,
                             kIconSize, kIconSize, 0, nullptr, DI_NORMAL);
                text_rc.left += kIconSize + 6;
            }

            ::DrawTextW(hdc, title.c_str(), -1, &text_rc,
                        DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            ::EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCHITTEST:
            /* 让鼠标透过预览窗口去命中下面的窗口 */
            return HTTRANSPARENT;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

void TabController::CreateDragPreview(int tab_idx) {
    if (preview_hwnd_) return;        /* 已存在 */
    if (!hInstance_) return;

    EnsurePreviewClassRegistered(hInstance_);
    preview_tab_idx_ = tab_idx;

    /* 打磨：先尝试给对应 child 拍个缩略图。
     * 拍摄成功 → 预览窗口拉大到 kPreviewHFull（带缩略图）
     * 拍摄失败 → 退回 header-only 尺寸 kPreviewHHeader（兼容 OpenGL/D3D 等 child） */
    HWND child_for_snap = nullptr;
    int  sid = TabIdxToSlotId(tab_idx);
    if (sid >= 0) {
        if (auto* s = FindSlot(sid)) child_for_snap = s->child_hwnd;
    }
    if (child_for_snap && ::IsWindow(child_for_snap)) {
        CapturePreviewSnapshot(child_for_snap);
    }
    int height = preview_snapshot_ ? kPreviewHFull : kPreviewHHeader;

    DWORD style    = WS_POPUP;
    DWORD ex_style = WS_EX_LAYERED | WS_EX_TOOLWINDOW
                   | WS_EX_TOPMOST | WS_EX_NOACTIVATE
                   | WS_EX_TRANSPARENT;

    preview_hwnd_ = ::CreateWindowExW(
        ex_style, kDragPreviewClass, L"",
        style,
        0, 0, kPreviewW, height,
        nullptr, nullptr, hInstance_, this);
    if (!preview_hwnd_) {
        MHX_LOG_WARN(L"CreateDragPreview: CreateWindowExW failed: %lu",
                     ::GetLastError());
        /* 回滚缩略图 */
        if (preview_snapshot_) {
            ::DeleteObject(preview_snapshot_);
            preview_snapshot_ = nullptr;
        }
        return;
    }

    ::SetLayeredWindowAttributes(preview_hwnd_, 0, kPreviewAlpha, LWA_ALPHA);

    POINT cur;
    ::GetCursorPos(&cur);
    MoveDragPreview(cur);
    ::ShowWindow(preview_hwnd_, SW_SHOWNOACTIVATE);
}

void TabController::MoveDragPreview(POINT screen_pt) {
    if (!preview_hwnd_) return;
    /* 预览窗口偏移到鼠标右下，避免遮住光标 */
    ::SetWindowPos(preview_hwnd_, HWND_TOPMOST,
                   screen_pt.x + 12, screen_pt.y + 12, 0, 0,
                   SWP_NOSIZE | SWP_NOACTIVATE);
}

void TabController::DestroyDragPreview() {
    if (preview_hwnd_) {
        ::DestroyWindow(preview_hwnd_);
        preview_hwnd_ = nullptr;
    }
    preview_tab_idx_ = -1;
    if (preview_snapshot_) {
        ::DeleteObject(preview_snapshot_);
        preview_snapshot_ = nullptr;
    }
}

/* ============================================================
 * 打磨：CapturePreviewSnapshot
 *
 * 给 child_hwnd 拍一个 kPreviewW - 2*margin 宽的缩略图。
 * 优先用 PrintWindow(PW_RENDERFULLCONTENT, Win10 1803+)，
 * fallback 到 PW_CLIENTONLY（老系统或某些 child 不支持 full content）。
 *
 * 失败场景（正常容错）：
 *   - child_hwnd 未绘制过（IsWindowVisible==FALSE）
 *   - child 是 OpenGL / D3D 等用特殊表面绘制的窗口 → 拍到黑块
 *   - child 进程已退出
 * 任何失败都安全：保持 preview_snapshot_ = nullptr，上层回退到 header-only。
 * ============================================================ */
/* ============================================================
 * 打磨：Drop Indicator
 *
 * 拖拽中根据鼠标客户坐标 (mouse_x, mouse_y) 决定"若现在松手会
 * 插到第 N 条缝"。取值：
 *   0    → 所有 tabs 最左侧（插到首位）
 *   1..N-1 → 第 (idx-1) 和 idx 号 tab 之间
 *   N    → 最后一个 tab 的右侧（追加到尾）
 *   -1   → 不显示（鼠标不在 tab bar 上）
 *
 * 判断规则：鼠标 x 过被命中 tab 的中线则 insert = hit+1，否则 hit。
 * 不在任何 tab 上但在 tab bar 水平范围内 → 按 x 归类到就近缝隙。
 * ============================================================ */
bool TabController::ComputeDropIndicator(int mouse_x, int mouse_y) {
    if (!tab_ctrl_) return false;

    int tab_count = TabCtrl_GetItemCount(tab_ctrl_);
    if (tab_count <= 0) {
        if (drop_indicator_idx_ == -1) return false;
        drop_indicator_idx_ = -1;
        return true;
    }

    RECT bar_rc = {};
    ::GetClientRect(tab_ctrl_, &bar_rc);

    /* Tab bar 只有 TabCtrl 顶部 head 区；鼠标 y 超出 item0 的 rect 底部则隐藏 */
    RECT first_rc = {};
    TabCtrl_GetItemRect(tab_ctrl_, 0, &first_rc);
    if (mouse_y < first_rc.top || mouse_y > first_rc.bottom) {
        if (drop_indicator_idx_ == -1) return false;
        drop_indicator_idx_ = -1;
        return true;
    }

    int new_idx = -1;
    int hit = HitTestTab(mouse_x, mouse_y);
    if (hit >= 0) {
        RECT r = {};
        if (TabCtrl_GetItemRect(tab_ctrl_, hit, &r)) {
            int mid = (r.left + r.right) / 2;
            new_idx = (mouse_x < mid) ? hit : hit + 1;
        }
    } else {
        /* 鼠标在 tab bar 水平范围但没命中任何 tab：
         * - 在最左 tab 左侧 → idx 0
         * - 在最右 tab 右侧 → idx N
         * - 落在两个 tab 的水平间隙 → 取前者 idx+1 */
        RECT last_rc = {};
        TabCtrl_GetItemRect(tab_ctrl_, tab_count - 1, &last_rc);
        if (mouse_x <= first_rc.left)       new_idx = 0;
        else if (mouse_x >= last_rc.right)  new_idx = tab_count;
        else {
            for (int i = 0; i + 1 < tab_count; ++i) {
                RECT a{}, b{};
                TabCtrl_GetItemRect(tab_ctrl_, i,     &a);
                TabCtrl_GetItemRect(tab_ctrl_, i + 1, &b);
                if (mouse_x >= a.right && mouse_x <= b.left) {
                    new_idx = i + 1;
                    break;
                }
            }
        }
    }

    if (new_idx == drop_indicator_idx_) return false;
    drop_indicator_idx_ = new_idx;
    return true;
}

/* ============================================================
 * 在 tab_ctrl_ 上叠画一条 3px 宽的垂直蓝线。
 * 调用时机：HandleTabMessage WM_PAINT 路径，DefSubclassProc 之后。
 * 不走 BeginPaint/EndPaint（避免清空刚画完的 tabs）；直接 GetDC + ReleaseDC。
 * ============================================================ */
void TabController::DrawDropIndicator() {
    if (!tab_ctrl_ || drop_indicator_idx_ < 0) return;
    int tab_count = TabCtrl_GetItemCount(tab_ctrl_);
    if (tab_count <= 0) return;

    /* 定位画线 x 坐标：
     *   idx == 0          → 第一个 tab 的 left
     *   idx in [1, N-1]   → 第 idx-1 和 idx 号 tab 的缝隙中点
     *   idx == N          → 最后一个 tab 的 right */
    int x = 0;
    int top, bottom;
    if (drop_indicator_idx_ == 0) {
        RECT r = {};
        TabCtrl_GetItemRect(tab_ctrl_, 0, &r);
        x = r.left;
        top = r.top;
        bottom = r.bottom;
    } else if (drop_indicator_idx_ >= tab_count) {
        RECT r = {};
        TabCtrl_GetItemRect(tab_ctrl_, tab_count - 1, &r);
        x = r.right;
        top = r.top;
        bottom = r.bottom;
    } else {
        RECT a{}, b{};
        TabCtrl_GetItemRect(tab_ctrl_, drop_indicator_idx_ - 1, &a);
        TabCtrl_GetItemRect(tab_ctrl_, drop_indicator_idx_,     &b);
        x = (a.right + b.left) / 2;
        top    = (a.top    < b.top)    ? a.top    : b.top;
        bottom = (a.bottom > b.bottom) ? a.bottom : b.bottom;
    }

    /* 3px 蓝色竖线：HDC 直接 FillRect 不触发 invalidation */
    HDC hdc = ::GetDC(tab_ctrl_);
    if (!hdc) return;
    RECT line = { x - 1, top, x + 2, bottom };
    HBRUSH br = ::CreateSolidBrush(RGB(0x00, 0x78, 0xD4));
    ::FillRect(hdc, &line, br);
    ::DeleteObject(br);
    ::ReleaseDC(tab_ctrl_, hdc);
}

void TabController::CapturePreviewSnapshot(HWND child_hwnd) {
    if (preview_snapshot_) {
        ::DeleteObject(preview_snapshot_);
        preview_snapshot_ = nullptr;
    }
    if (!child_hwnd || !::IsWindow(child_hwnd)) return;

    /* 缩略图目标尺寸：填满 preview 窗口缩略图区域 */
    int dst_w = kPreviewW - 2 * kPreviewThumbMargin;
    int dst_h = kPreviewHFull - kPreviewHHeader - 2 * kPreviewThumbMargin;
    if (dst_w <= 0 || dst_h <= 0) return;

    /* 取 child 原始客户区尺寸，用于计算 letter-box 比例 */
    RECT child_rc = {};
    ::GetClientRect(child_hwnd, &child_rc);
    int src_w = child_rc.right  - child_rc.left;
    int src_h = child_rc.bottom - child_rc.top;
    if (src_w <= 0 || src_h <= 0) return;

    /* 先把 child 绘制到一个和 child 客户区同尺寸的临时位图，
     * 然后 StretchBlt 到最终缩略图，保持等比 + letter-box 背景 */
    HDC screen_dc = ::GetDC(nullptr);
    if (!screen_dc) return;
    HDC src_dc = ::CreateCompatibleDC(screen_dc);
    HDC dst_dc = ::CreateCompatibleDC(screen_dc);
    HBITMAP src_bmp = ::CreateCompatibleBitmap(screen_dc, src_w, src_h);
    HBITMAP dst_bmp = ::CreateCompatibleBitmap(screen_dc, dst_w, dst_h);
    ::ReleaseDC(nullptr, screen_dc);

    bool ok = false;
    if (src_dc && dst_dc && src_bmp && dst_bmp) {
        HGDIOBJ prev_src = ::SelectObject(src_dc, src_bmp);
        HGDIOBJ prev_dst = ::SelectObject(dst_dc, dst_bmp);

        /* 背景预填充一次深色，避免 StretchBlt letter-box 区域露出未初始化像素 */
        RECT dst_rect = { 0, 0, dst_w, dst_h };
        HBRUSH bg = ::CreateSolidBrush(RGB(0x20, 0x20, 0x20));
        ::FillRect(dst_dc, &dst_rect, bg);
        ::DeleteObject(bg);

        /* PrintWindow：PW_RENDERFULLCONTENT (0x2) 在 Win10 1803+ 才定义。
         * 若不支持，GDI 会忽略该位 → 相当于 PW_CLIENTONLY（第 0 位）。 */
        constexpr UINT kPwFullContent = 0x00000002;
        BOOL printed = ::PrintWindow(child_hwnd, src_dc, kPwFullContent);

        if (printed) {
            /* 等比缩放到 dst_w x dst_h 并居中 */
            /* 等比缩放：取两方向缩放系数的小值，避免越界 */
            double sx = (double)dst_w / (double)src_w;
            double sy = (double)dst_h / (double)src_h;
            double scale = (sx < sy) ? sx : sy;
            int draw_w = (int)(src_w * scale);
            int draw_h = (int)(src_h * scale);
            int off_x  = (dst_w - draw_w) / 2;
            int off_y  = (dst_h - draw_h) / 2;

            ::SetStretchBltMode(dst_dc, HALFTONE);
            ::SetBrushOrgEx(dst_dc, 0, 0, nullptr);
            ::StretchBlt(dst_dc, off_x, off_y, draw_w, draw_h,
                         src_dc, 0, 0, src_w, src_h, SRCCOPY);
            ok = true;
        }

        ::SelectObject(src_dc, prev_src);
        ::SelectObject(dst_dc, prev_dst);
    }

    if (src_dc)  ::DeleteDC(src_dc);
    if (dst_dc)  ::DeleteDC(dst_dc);
    if (src_bmp) ::DeleteObject(src_bmp);

    if (ok) {
        preview_snapshot_ = dst_bmp;
    } else if (dst_bmp) {
        ::DeleteObject(dst_bmp);
    }
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
    LRESULT def_ret = ::DefSubclassProc(hwnd, msg, wp, lp);

    /* 打磨：WM_PAINT 后做一次 post-paint 叠画 drop indicator。
     * 必须放在 DefSubclassProc 之后，让 OnDrawItem 先把所有 tab 画完，
     * 否则 indicator 会被 tab 自己的绘制覆盖掉。 */
    if (self && msg == WM_PAINT &&
        self->drag_active_ && self->drop_indicator_idx_ >= 0) {
        self->DrawDropIndicator();
    }
    return def_ret;
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
                    /* P3: 进入 active drag 时创建预览窗口 */
                    CreateDragPreview(drag_src_idx_);
                }
                if (drag_active_) {
                    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
                    /* P3: 跟随鼠标更新预览位置（屏幕坐标） */
                    POINT screen_pt;
                    ::GetCursorPos(&screen_pt);
                    MoveDragPreview(screen_pt);

                    /* 打磨：更新 drop indicator。变化才 invalidate 以减少闪烁 */
                    if (ComputeDropIndicator(x, y)) {
                        ::InvalidateRect(tab_ctrl_, nullptr, FALSE);
                    }
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (drag_active_) {
                /* P3: 先销毁预览窗口，无论后面走重排还是 detach 都不再显示 */
                DestroyDragPreview();

                /* 打磨：缓存 indicator 并清除，后面的 ReorderTabs 可以复用更精确的位置
                 * （比仅靠 HitTestTab 得到的命中 tab 更直观，Chrome 行为一致） */
                int indicator = drop_indicator_idx_;
                drop_indicator_idx_ = -1;
                ::InvalidateRect(tab_ctrl_, nullptr, FALSE);

                /* P2: 屏幕级判定 - 鼠标松开点是否在主窗口 GetWindowRect 内
                 *   - 在内 → 现有重排逻辑
                 *   - 在外 → 触发 on_drag_out_ 让 MainFrame 决定 spawn / cross-merge */
                POINT screen_pt;
                ::GetCursorPos(&screen_pt);
                ::ReleaseCapture();

                RECT main_rc = {};
                bool inside = false;
                if (parent_ && ::GetWindowRect(parent_, &main_rc)) {
                    inside = ::PtInRect(&main_rc, screen_pt) != 0;
                }

                if (inside) {
                    /* 重排：优先用 drop_indicator_idx_（更精确），
                     * 回退到 HitTestTab（老路径，保持兼容） */
                    int dst = -1;
                    if (indicator >= 0) {
                        /* indicator 是"缝隙索引"，0..N。要把 src 插入到第 indicator 条缝：
                         *   - indicator <= src  → 目标 tab_index 就是 indicator
                         *   - indicator >  src  → 减 1，因为移除 src 后缝隙整体左移 */
                        int n = TabCtrl_GetItemCount(tab_ctrl_);
                        int target = indicator;
                        if (target > drag_src_idx_) target -= 1;
                        if (target < 0) target = 0;
                        if (target >= n) target = n - 1;
                        dst = target;
                    } else {
                        POINT local = screen_pt;
                        ::ScreenToClient(tab_ctrl_, &local);
                        dst = HitTestTab(local.x, local.y);
                    }
                    if (dst >= 0 && dst != drag_src_idx_) {
                        ReorderTabs(drag_src_idx_, dst);
                    }
                } else {
                    int sid = TabIdxToSlotId(drag_src_idx_);
                    if (sid >= 0 && on_drag_out_) {
                        /* 注意：on_drag_out_ 内部会调用 DetachSlot 等会清理
                         * 当前 slot 的接口，状态机里的 drag_* 必须先重置，
                         * 不然回调里再触发 SetCapture 会出乱 */
                        drag_armed_   = false;
                        drag_active_  = false;
                        int dragged   = drag_src_idx_;
                        drag_src_idx_ = -1;
                        (void)dragged;
                        on_drag_out_(sid, screen_pt);
                        return 0;
                    }
                }
            }
            drag_armed_   = false;
            drag_active_  = false;
            drag_src_idx_ = -1;
            return 0;
        }

        case WM_CAPTURECHANGED:
            /* P3: 异常打断（如 Alt+Tab 切走焦点）也要清掉预览 */
            DestroyDragPreview();
            if (drop_indicator_idx_ >= 0) {
                drop_indicator_idx_ = -1;
                ::InvalidateRect(tab_ctrl_, nullptr, FALSE);
            }
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
