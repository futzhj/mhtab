/**
 * mhtabx - Session 持久化
 *
 * 把当前 Tab 列表保存到 mhtabx.ini，下次启动恢复。
 *
 * 触发条件：
 *   - 保存：MainFrame 退出前（OnDestroy）
 *   - 恢复：本次启动 cmdline 为空，且 ini 中 count > 0
 *
 * INI 格式（每个 Tab 一段，从 0 开始编号）：
 *   [Session]
 *   count=2
 *
 *   [Tab0]
 *   exe=C:\full\path\demo_child.exe
 *   args=
 *   title=demo_child [PID=12345]
 *
 *   [Tab1]
 *   exe=...
 *   args=--debug
 *   title=...
 */

#pragma once
#include "common.h"

namespace mhx {

struct SessionEntry {
    String exe_path;
    String args;
    String title;
};

class SessionStore {
public:
    explicit SessionStore(const String& ini_path);

    /** 读取所有 Tab 条目（按 Tab0..TabN-1 顺序） */
    std::vector<SessionEntry> Load() const;

    /** 保存 Tab 条目；调用方应保证顺序就是当前 UI 顺序 */
    bool Save(const std::vector<SessionEntry>& entries) const;

    /** 清空 session（写 count=0 并删除 TabN 段，简单实现：只设 count=0） */
    void Clear() const;

    const String& Path() const noexcept { return ini_path_; }

private:
    String ini_path_;
};

} /* namespace mhx */
