/**
 * mhtabx - Tab 控件封装
 *
 * 包装 Win32 SysTabControl32，并管理一组 ChildSlot：
 *   - 每个 slot = 一个被嵌入的子进程窗口
 *   - 切换 Tab → SetParent + ShowWindow 让对应子窗口显示
 *
 * 不包含拖拽逻辑（W4 的 DragDropManager 单独负责）。
 */

#pragma once
#include "common.h"
#include "Theme.h"

namespace mhx {

/* 单个 Tab 槽位
 *
 * 对应原 mhtab.exe 的 ViewSlot 结构（详见 reverse/include/types.h）。
 * 此处只保留复刻必要字段，舍弃逆向中无法还原原义的字段。
 */
struct ChildSlot {
    int          slot_id      = -1;          /* 在 slots_ 中的索引；-1 = 空 */
    HANDLE       hProcess     = nullptr;     /* 子进程句柄，CloseHandle 在析构 */
    DWORD        pid          = 0;
    HWND         child_hwnd   = nullptr;     /* 子进程主窗口（被 SetParent 后） */
    HWND         orig_parent  = nullptr;     /* SetParent 前的原 parent，复原用 */
    LONG_PTR     orig_style   = 0;           /* SetParent 前的窗口样式 */
    int          tab_index    = -1;          /* 在 SysTabControl32 中的 index */
    ChildState   state        = ChildState::Starting;
    String       title;                       /* Tab 显示标题 */
    String       exe_path;                    /* 子进程可执行文件路径 */
    String       cmdline;                     /* 启动参数（不含 exe，不含 --mhx-* 注入参数） */

    /* 不允许拷贝，避免 hProcess 重复关闭 */
    ChildSlot() = default;
    ChildSlot(const ChildSlot&) = delete;
    ChildSlot& operator=(const ChildSlot&) = delete;
    ChildSlot(ChildSlot&&) noexcept = default;
    ChildSlot& operator=(ChildSlot&&) noexcept = default;

    ~ChildSlot() {
        if (hProcess) ::CloseHandle(hProcess);
    }
};

class TabController {
public:
    TabController();
    ~TabController();

    TabController(const TabController&) = delete;
    TabController& operator=(const TabController&) = delete;

    /**
     * 在 parent 中创建 SysTabControl32，初始大小为 client_rect。
     * @return true 创建成功
     */
    bool Create(HWND parent, HINSTANCE hInst, const RECT& client_rect, int ctrl_id);

    /**
     * 当父窗口 WM_SIZE 时调整自身。
     */
    void Resize(int cx, int cy);

    /**
     * 父窗口收到 WM_NOTIFY 时调用，处理 TCN_SELCHANGE 等。
     * @return TRUE 已处理
     */
    LRESULT HandleNotify(NMHDR* hdr);

    /**
     * 注册一个 ChildSlot（已分配 slot_id）。
     * 通常由 ChildProcessManager 在 CreateProcess 后调用。
     * @return 分配到的 slot_id；失败返回 -1
     */
    int AddSlot(ChildSlot slot);

    /**
     * 把已注册 slot 中的子进程窗口嵌入对应 Tab 页：
     *   1. SetParent(child_hwnd, tab_ctrl_)
     *   2. 调整 child 的 style/size
     *   3. 创建 Tab 项（TCM_INSERTITEM）
     */
    bool EmbedChildWindow(int slot_id, HWND child_hwnd);

    /**
     * 移除 slot：
     *   - SetParent(child_hwnd, orig_parent)
     *   - 删除 Tab 项
     *   - 从 slots_ 中清空该 slot（不立即压缩，保留 id 稳定）
     */
    bool RemoveSlot(int slot_id);

    /** 切换到指定 slot 的 Tab（hide 其他子窗口、show 这个） */
    void SelectSlot(int slot_id);

    /** 当前选中的 slot_id；空则返回 -1 */
    int GetSelectedSlotId() const noexcept { return selected_slot_id_; }

    /** 返回有效 slot 数量（state != Dead） */
    size_t GetActiveCount() const noexcept;

    ChildSlot* FindSlot(int slot_id) noexcept;
    ChildSlot* FindSlotByPid(DWORD pid) noexcept;
    ChildSlot* FindSlotByHwnd(HWND hwnd) noexcept;

    HWND GetHwnd() const noexcept { return tab_ctrl_; }

    /** 切换到下一个/上一个 Tab（按 tab_index 顺序） */
    void SelectNext();
    void SelectPrev();

    /** 修改 slot 的 Tab 标题 */
    bool RenameSlot(int slot_id, const String& new_title);

    /** 把 src_idx 的 Tab 移到 dst_idx，更新所有 slot.tab_index */
    bool ReorderTabs(int src_idx, int dst_idx);

    /**
     * 把 slot 对应的 child 窗口从 Tab 容器中"分离"为独立顶层窗口。
     * 相当于 RemoveSlot，但恢复 child 后会 ShowWindow(SW_SHOWNORMAL)
     * 并移动到主窗口右下方可见位置。
     *
     * 调用方（MainFrame）必须先调用 ChildProcessManager::DetachSlot 解除
     * 进程 wait handle 关系，再调这个接口清理 UI 状态。
     */
    bool DetachSlot(int slot_id);

    /** 处理 WM_DRAWITEM（由 MainFrame 路由）。成功返回 TRUE。 */
    LRESULT OnDrawItem(DRAWITEMSTRUCT* dis);

    /** 设置主题（带走所有权），传 nullptr 会后退为默认 SysTabControl32 外观 */
    void SetTheme(std::unique_ptr<ITheme> theme);

    /** 遍历所有 slot（含已 Dead 的） */
    template <class F>
    void ForEachSlot(F&& fn) {
        for (auto& slot : slots_) {
            if (slot && slot->slot_id >= 0) fn(*slot);
        }
    }

private:
    /** 把 slot.child_hwnd 显示在 Tab 内容区域 */
    void LayoutChildWindow(const ChildSlot& slot) const;

    /** 计算 Tab 内容区（在 tab head 下方）相对父窗口的 RECT */
    RECT GetDisplayArea() const;

    /* === 拖拽支持 === */
    static LRESULT CALLBACK TabSubclassProc(
        HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
        UINT_PTR id_subclass, DWORD_PTR ref_data);
    LRESULT HandleTabMessage(UINT msg, WPARAM wp, LPARAM lp);

    /** 鼠标在哪个 Tab 上（-1 表示不在任何 Tab） */
    int HitTestTab(int x, int y) const;

private:
    HWND       tab_ctrl_         = nullptr;
    HWND       parent_           = nullptr;
    HINSTANCE  hInstance_        = nullptr;
    int        selected_slot_id_ = -1;

    /* === 拖拽状态 === */
    bool       drag_armed_   = false;     /* LBUTTONDOWN 命中 tab 但还未达阈值 */
    bool       drag_active_  = false;     /* 已超过阈值，进入拖拽模式 */
    int        drag_src_idx_ = -1;
    POINT      drag_start_pt_{};

    /* === 主题 + hot tracking === */
    std::unique_ptr<ITheme> theme_;
    int        hot_idx_     = -1;          /* 鼠标悬停的 tab index */
    bool       tracking_    = false;       /* TrackMouseEvent 是否已注册 */

    /* slots_ 用 unique_ptr 避免移动 vector 时 ChildSlot 地址变化 */
    std::vector<std::unique_ptr<ChildSlot>> slots_;
};

} /* namespace mhx */
