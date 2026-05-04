/**
 * mhtabx - 心跳监视器
 *
 * 主进程每隔 kIntervalMs 给所有 Running 子进程发 MHX_HEARTBEAT，
 * 子进程必须在 kTimeoutMs 内回 MHX_READY_CONFIRM。
 *
 * 超时未响应 → 视为子进程已挂起，触发 ChildProcessManager 强制清理。
 *
 * 不开后台线程，由 MainFrame 的 WM_TIMER 驱动 Tick()。
 */

#pragma once
#include "common.h"

namespace mhx {

class TabController;
class ChildProcessManager;

class HeartbeatMonitor {
public:
    /* 默认时序参数（W6-3 起可由 SetTimings 覆盖） */
    static constexpr ULONG kDefaultIntervalMs = 1000;   /* 主进程每秒发一次心跳 */
    static constexpr ULONG kDefaultTimeoutMs  = 3000;   /* 子进程 3 秒不回 → 视为挂起 */

    HeartbeatMonitor(TabController& tab_ctrl, ChildProcessManager& child_mgr);

    /**
     * W6-3: 运行时调整心跳间隔和超时时长（由 SettingsDialog 触发）。
     * 立即生效，下一轮 Tick 即采用新值。
     *
     * 参数下限保护：interval >= 100ms，timeout >= 500ms 且必须 > interval。
     * 不合规的值会自动 clamp 到最近的合法值。
     */
    void SetTimings(ULONG interval_ms, ULONG timeout_ms);

    ULONG GetIntervalMs() const noexcept { return interval_ms_; }
    ULONG GetTimeoutMs()  const noexcept { return timeout_ms_;  }

    HeartbeatMonitor(const HeartbeatMonitor&) = delete;
    HeartbeatMonitor& operator=(const HeartbeatMonitor&) = delete;

    /**
     * 由 MainFrame 在 WM_TIMER 中调用：
     *   - 每 kIntervalMs 发一轮心跳
     *   - 检查 last_response 超时的 slot
     *
     * @return 被判定挂起并已清理的 slot_id 数量
     */
    int Tick();

    /**
     * 当主进程收到子进程的 MHX_READY_CONFIRM 时调用，
     * 更新对应 slot 的 last_response 时间戳。
     */
    void OnReadyConfirm(int slot_id, ULONG tick);

    /**
     * 子进程注册时（OnNewClient）调用，登记初始时间戳。
     */
    void RegisterSlot(int slot_id);

    /**
     * 子进程清理时（OnRemoveSlot）调用，移除时间戳。
     */
    void UnregisterSlot(int slot_id);

    /**
     * 调试用：返回某 slot 距离上次响应的毫秒数（-1 = 不存在）。
     */
    long GetIdleMs(int slot_id) const noexcept;

private:
    struct SlotState {
        int   slot_id        = -1;
        ULONG last_response  = 0;     /* GetTickCount() */
        ULONG last_send      = 0;     /* 上次发心跳的时间 */
        int   timeout_count  = 0;     /* 连续超时次数（W4 可用于 reload 决策） */
    };

    SlotState* Find(int slot_id) noexcept;
    const SlotState* Find(int slot_id) const noexcept;

    TabController&       tab_ctrl_;
    ChildProcessManager& child_mgr_;
    ULONG                last_broadcast_ms_ = 0;
    ULONG                interval_ms_       = kDefaultIntervalMs;   /* W6-3 */
    ULONG                timeout_ms_        = kDefaultTimeoutMs;    /* W6-3 */
    std::vector<SlotState> slots_;
};

} /* namespace mhx */
