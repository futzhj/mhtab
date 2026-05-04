/**
 * mhtabx - HeartbeatMonitor 实现
 */

#include "HeartbeatMonitor.h"
#include "TabController.h"
#include "ChildProcessManager.h"
#include "IpcProtocol.h"
#include "Utils.h"

#include <algorithm>

namespace mhx {

HeartbeatMonitor::HeartbeatMonitor(TabController& tab_ctrl, ChildProcessManager& child_mgr)
    : tab_ctrl_(tab_ctrl), child_mgr_(child_mgr) {
}

/* ============================================================
 * W6-3: SetTimings
 *
 * 调整心跳间隔与超时阈值。值过小会让 Tab 频繁触发"假死清理"，
 * 过大又会让响应变慢。这里 clamp 到一组合理范围：
 *   interval ≥ 100ms      （避免 100Hz 以上风暴）
 *   timeout  ≥ 500ms      （留出消息泵处理时间）
 *   timeout  > interval   （否则永远超时）
 * ============================================================ */
void HeartbeatMonitor::SetTimings(ULONG interval_ms, ULONG timeout_ms) {
    if (interval_ms < 100)  interval_ms = 100;
    if (timeout_ms  < 500)  timeout_ms  = 500;
    if (timeout_ms  <= interval_ms) timeout_ms = interval_ms + 200;
    interval_ms_ = interval_ms;
    timeout_ms_  = timeout_ms;
    MHX_LOG_INFO(L"Heartbeat timings updated: interval=%lu timeout=%lu",
                 interval_ms_, timeout_ms_);
}

/* ============================================================
 * 注册 / 注销
 * ============================================================ */
void HeartbeatMonitor::RegisterSlot(int slot_id) {
    if (auto* s = Find(slot_id)) {
        /* 已存在则刷新（不应发生，但保险） */
        s->last_response = ::GetTickCount();
        s->timeout_count = 0;
        return;
    }
    SlotState st;
    st.slot_id       = slot_id;
    st.last_response = ::GetTickCount();
    st.last_send     = 0;
    slots_.push_back(st);
    MHX_LOG_TRACE(L"Heartbeat: register slot=%d", slot_id);
}

void HeartbeatMonitor::UnregisterSlot(int slot_id) {
    auto it = std::remove_if(slots_.begin(), slots_.end(),
                             [&](const SlotState& s) { return s.slot_id == slot_id; });
    if (it != slots_.end()) {
        slots_.erase(it, slots_.end());
        MHX_LOG_TRACE(L"Heartbeat: unregister slot=%d", slot_id);
    }
}

/* ============================================================
 * 收到子进程响应
 * ============================================================ */
void HeartbeatMonitor::OnReadyConfirm(int slot_id, ULONG /*tick*/) {
    if (auto* s = Find(slot_id)) {
        s->last_response = ::GetTickCount();
        s->timeout_count = 0;
    }
}

/* ============================================================
 * Tick
 * ============================================================ */
int HeartbeatMonitor::Tick() {
    ULONG now = ::GetTickCount();
    int killed = 0;

    /* 1. 周期发送心跳（W6-3: 间隔可配置） */
    bool need_broadcast = (now - last_broadcast_ms_) >= interval_ms_;
    if (need_broadcast) {
        last_broadcast_ms_ = now;

        tab_ctrl_.ForEachSlot([&](ChildSlot& slot) {
            if (slot.state != ChildState::Running) return;
            if (!slot.child_hwnd || !::IsWindow(slot.child_hwnd)) return;

            ipc::PostHeartbeat(slot.child_hwnd, slot.slot_id, now);
            if (auto* st = Find(slot.slot_id)) st->last_send = now;
        });
    }

    /* 2. 检查超时（W6-3: 阈值可配置） */
    std::vector<int> dead_ids;
    for (auto& st : slots_) {
        ULONG idle = now - st.last_response;
        if (idle > timeout_ms_) {
            ++st.timeout_count;
            MHX_LOG_WARN(L"Heartbeat timeout: slot=%d idle=%lums count=%d",
                         st.slot_id, idle, st.timeout_count);
            dead_ids.push_back(st.slot_id);
        }
    }

    /* 3. 清理超时 slot（在循环外删除避免迭代失效） */
    for (int slot_id : dead_ids) {
        child_mgr_.RequestClose(slot_id, /*force=*/true);
        UnregisterSlot(slot_id);
        ++killed;
    }

    return killed;
}

/* ============================================================
 * 调试接口
 * ============================================================ */
long HeartbeatMonitor::GetIdleMs(int slot_id) const noexcept {
    if (auto* s = Find(slot_id)) {
        return static_cast<long>(::GetTickCount() - s->last_response);
    }
    return -1;
}

/* ============================================================
 * 内部
 * ============================================================ */
HeartbeatMonitor::SlotState* HeartbeatMonitor::Find(int slot_id) noexcept {
    for (auto& s : slots_) {
        if (s.slot_id == slot_id) return &s;
    }
    return nullptr;
}

const HeartbeatMonitor::SlotState* HeartbeatMonitor::Find(int slot_id) const noexcept {
    for (auto& s : slots_) {
        if (s.slot_id == slot_id) return &s;
    }
    return nullptr;
}

} /* namespace mhx */
