/**
 * mhtabx - ChildProcessManager 实现
 */

#include "ChildProcessManager.h"
#include "TabController.h"
#include "Utils.h"

#include <algorithm>

namespace mhx {

ChildProcessManager::ChildProcessManager(TabController& tab_ctrl)
    : tab_ctrl_(tab_ctrl) {
    wait_handles_.reserve(MAXIMUM_WAIT_OBJECTS);
}

ChildProcessManager::~ChildProcessManager() {
    /* 析构时关闭所有未退出的子进程 */
    tab_ctrl_.ForEachSlot([](ChildSlot& slot) {
        if (slot.hProcess && slot.state != ChildState::Dead) {
            ::TerminateProcess(slot.hProcess, 0);
        }
    });
}

void ChildProcessManager::SetHostInfo(HWND host_hwnd, u32 instance_id) noexcept {
    host_hwnd_   = host_hwnd;
    instance_id_ = instance_id;
}

/* ============================================================
 * 命令行拼装
 * ============================================================ */
String ChildProcessManager::BuildCommandLine(const String& exe_path,
                                             const String& user_args,
                                             int slot_id) const {
    /* 格式：
     *   "<exe>" <user_args> --mhx-parent 0xHHHHHHHH --mhx-slot N --mhx-instance 0xIIIIIIII
     */
    return utils::Format(
        L"\"%s\" %s %s 0x%016llX %s %d %s 0x%08X",
        exe_path.c_str(),
        user_args.c_str(),
        kArgParentHwnd,
        reinterpret_cast<unsigned long long>(host_hwnd_),
        kArgSlotId,    slot_id,
        kArgInstanceId, instance_id_);
}

/* ============================================================
 * LaunchChild
 * ============================================================ */
int ChildProcessManager::LaunchChild(const String& exe_path,
                                     const String& user_args,
                                     const String& title) {
    /* 上限检查 */
    if (static_cast<int>(GetActiveCount()) >= max_children_) {
        MHX_LOG_WARN(L"LaunchChild rejected: reached max_children=%d", max_children_);
        return -1;
    }

    if (wait_handles_.size() >= MAXIMUM_WAIT_OBJECTS) {
        MHX_LOG_ERROR(L"LaunchChild: wait_handles_ full");
        return -1;
    }

    /* 1. 先分配 slot 占位（用于命令行注入） */
    ChildSlot slot;
    slot.title    = title.empty() ? utils::Format(L"Tab %zu", tab_ctrl_.GetActiveCount() + 1)
                                   : title;
    slot.exe_path = exe_path;
    slot.cmdline  = user_args;
    slot.state    = ChildState::Starting;
    int slot_id   = tab_ctrl_.AddSlot(std::move(slot));
    if (slot_id < 0) return -1;

    /* 2. 构造命令行 */
    String cmd_line = BuildCommandLine(exe_path, user_args, slot_id);
    MHX_LOG_INFO(L"LaunchChild: %s", cmd_line.c_str());

    /* CreateProcessW 会修改 lpCommandLine 的内容，必须传 mutable 指针 */
    std::vector<wchar_t> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back(L'\0');

    /* 3. CreateProcess */
    STARTUPINFOW        si = {};
    PROCESS_INFORMATION pi = {};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;        /* 子进程窗口先隐藏，握手成功后再显示 */

    BOOL ok = ::CreateProcessW(
        exe_path.c_str(),
        cmd_buf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr, nullptr,
        &si, &pi);

    if (!ok) {
        DWORD err = ::GetLastError();
        MHX_LOG_ERROR(L"CreateProcessW failed: %s",
                      utils::FormatSystemError(err).c_str());
        tab_ctrl_.RemoveSlot(slot_id);
        return -1;
    }

    /* 4. 写回 slot 中的进程信息 */
    if (auto* live_slot = tab_ctrl_.FindSlot(slot_id)) {
        live_slot->hProcess = pi.hProcess;
        live_slot->pid      = pi.dwProcessId;
        AddWaitHandle(pi.hProcess);
    }
    ::CloseHandle(pi.hThread);

    MHX_LOG_INFO(L"LaunchChild OK: slot=%d pid=%lu hProcess=%p",
                 slot_id, pi.dwProcessId, pi.hProcess);
    return slot_id;
}

/* ============================================================
 * Poll
 * ============================================================ */
int ChildProcessManager::Poll() {
    if (wait_handles_.empty()) return -1;

    DWORD ret = ::WaitForMultipleObjects(
        static_cast<DWORD>(wait_handles_.size()),
        wait_handles_.data(),
        FALSE,    /* 任一就返回 */
        0);       /* 非阻塞 */

    if (ret == WAIT_TIMEOUT) return -1;
    if (ret == WAIT_FAILED) {
        MHX_LOG_ERROR(L"WaitForMultipleObjects failed: %s",
                      utils::FormatSystemError(::GetLastError()).c_str());
        return -1;
    }

    /* WAIT_OBJECT_0 + N */
    if (ret >= WAIT_OBJECT_0 + wait_handles_.size()) return -1;
    DWORD idx     = ret - WAIT_OBJECT_0;
    HANDLE hProc  = wait_handles_[idx];

    /* 通过 hProcess 反查 slot */
    ChildSlot* slot = nullptr;
    tab_ctrl_.ForEachSlot([&](ChildSlot& s) {
        if (s.hProcess == hProc) slot = &s;
    });

    if (!slot) {
        /* 没找到 slot，但句柄已 signaled，移除避免死循环 */
        RemoveWaitHandle(hProc);
        return -1;
    }

    int slot_id = slot->slot_id;
    DWORD exit_code = 0;
    ::GetExitCodeProcess(hProc, &exit_code);
    MHX_LOG_INFO(L"Child exited: slot=%d pid=%lu exit_code=%lu",
                 slot_id, slot->pid, exit_code);

    /* 移除 wait + slot */
    RemoveWaitHandle(hProc);
    tab_ctrl_.RemoveSlot(slot_id);

    return slot_id;
}

/* ============================================================
 * RequestClose
 * ============================================================ */
bool ChildProcessManager::RequestClose(int slot_id, bool force) {
    auto* slot = tab_ctrl_.FindSlot(slot_id);
    if (!slot) return false;

    slot->state = ChildState::Closing;

    if (force) {
        if (slot->hProcess) ::TerminateProcess(slot->hProcess, 0);
        return true;
    }

    /* 优雅请求：先 PostMessage(WM_CLOSE) 给子窗口 */
    if (slot->child_hwnd && ::IsWindow(slot->child_hwnd)) {
        ::PostMessageW(slot->child_hwnd, WM_CLOSE, 0, 0);
        return true;
    }

    /* 兜底：直接 terminate */
    if (slot->hProcess) ::TerminateProcess(slot->hProcess, 0);
    return true;
}

/* ============================================================
 * W5-4: DetachSlot
 *
 * 与 RequestClose 不同：不终止子进程，只从主进程的 wait 列表中
 * 摘除 hProcess 并 CloseHandle。子进程此后独立运行，主进程放弃跟踪。
 * ============================================================ */
bool ChildProcessManager::DetachSlot(int slot_id) {
    auto* slot = tab_ctrl_.FindSlot(slot_id);
    if (!slot) return false;

    if (slot->hProcess) {
        RemoveWaitHandle(slot->hProcess);
        ::CloseHandle(slot->hProcess);
        slot->hProcess = nullptr;
    }
    MHX_LOG_INFO(L"DetachSlot: slot=%d pid=%lu (wait handle released)",
                 slot_id, slot->pid);
    return true;
}

/* ============================================================
 * P1: SpawnDetachedInstance
 *
 * 启动一个 mhtabx.exe 新进程，命令行带 --mhx-adopt-hwnd 0xHWND。
 * 新进程在 main.cpp 中识别该参数，绕过单实例 mutex，启动后通过
 * MainFrame::OnPostInit 调用 AdoptExternalWindow 接管 child_hwnd。
 *
 * 注意：必须用 GetModuleFileNameW(nullptr, ...) 拿自身路径，
 * 不能假设 PATH 里有 mhtabx.exe（开发机/绿色版常见情况）。
 * ============================================================ */
bool ChildProcessManager::SpawnDetachedInstance(HWND child_hwnd, const POINT* spawn_at) {
    if (!child_hwnd || !::IsWindow(child_hwnd)) return false;

    /* 1. 取自身可执行路径 */
    wchar_t exe_path[MAX_PATH] = {};
    DWORD got = ::GetModuleFileNameW(nullptr, exe_path, _countof(exe_path));
    if (got == 0 || got >= _countof(exe_path)) {
        MHX_LOG_ERROR(L"GetModuleFileNameW failed: %s",
                      utils::FormatSystemError(::GetLastError()).c_str());
        return false;
    }

    /* 2. 拼命令行：
     *    "<exe>" --mhx-adopt-hwnd 0xHHHHHHHHHHHHHHHH [--mhx-spawn-at X,Y]
     *    HWND 在 64-bit 上是 8 字节指针，用 %llX 输出 */
    String cmd_line = utils::Format(
        L"\"%s\" %s 0x%016llX",
        exe_path,
        kArgAdoptHwnd,
        reinterpret_cast<unsigned long long>(child_hwnd));

    /* 可选位置参数：新进程读取后 SetWindowPos 到目标坐标 */
    if (spawn_at) {
        cmd_line += utils::Format(L" %s %d,%d",
                                  kArgSpawnAt,
                                  static_cast<int>(spawn_at->x),
                                  static_cast<int>(spawn_at->y));
    }
    MHX_LOG_INFO(L"SpawnDetachedInstance: %s", cmd_line.c_str());

    /* CreateProcess 要 mutable buffer */
    std::vector<wchar_t> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back(L'\0');

    STARTUPINFOW        si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);

    BOOL ok = ::CreateProcessW(
        exe_path,
        cmd_buf.data(),
        nullptr, nullptr, FALSE,
        0,           /* 默认 flags（不需要新 console） */
        nullptr, nullptr,
        &si, &pi);

    if (!ok) {
        MHX_LOG_ERROR(L"SpawnDetachedInstance CreateProcessW failed: %s",
                      utils::FormatSystemError(::GetLastError()).c_str());
        return false;
    }

    /* 我们不跟踪新进程；它启动后会自我管理。立即关闭句柄。 */
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    MHX_LOG_INFO(L"SpawnDetachedInstance OK: pid=%lu adopted_hwnd=%p",
                 pi.dwProcessId, child_hwnd);
    return true;
}

/* ============================================================
 * W6-bugfix: AdoptExternalWindow
 *
 * 重新"领养"一个已脱离的 child 窗口，让它再次被 Tab 容纳。
 *
 * 关键点：
 *   - GetWindowThreadProcessId 从 HWND 反查 pid
 *   - OpenProcess 必须包含 SYNCHRONIZE 权限，否则 wait handle 等不到 signal
 *     （Win7+ 起普通进程也不会拒绝 SYNCHRONIZE；同 integrity level 一般都能）
 *   - 拿不到 hProcess 也不算致命错误：tab 还是能嵌入，只是 Poll 探测不到进程退出
 *     必须在 child_hwnd 自然销毁时由 IPC 清理（MHX_CLEANUP_VIEW）
 * ============================================================ */
int ChildProcessManager::AdoptExternalWindow(HWND child_hwnd, const String& title) {
    if (!child_hwnd || !::IsWindow(child_hwnd)) {
        MHX_LOG_WARN(L"AdoptExternalWindow: invalid hwnd");
        return -1;
    }
    if (static_cast<int>(GetActiveCount()) >= max_children_) {
        MHX_LOG_WARN(L"AdoptExternalWindow rejected: max_children=%d reached", max_children_);
        return -1;
    }

    /* 1. 反查 pid */
    DWORD pid = 0;
    ::GetWindowThreadProcessId(child_hwnd, &pid);
    if (pid == 0) {
        MHX_LOG_WARN(L"AdoptExternalWindow: GetWindowThreadProcessId failed");
        return -1;
    }

    /* 2. OpenProcess（SYNCHRONIZE 让 wait handle 工作）
     *    失败时不回绝 embed，仅跳过 wait 跟踪 */
    HANDLE hProc = ::OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, pid);
    if (!hProc) {
        MHX_LOG_WARN(L"AdoptExternalWindow: OpenProcess(pid=%lu) failed: %s",
                     pid, utils::FormatSystemError(::GetLastError()).c_str());
        /* 继续：hProc=nullptr，不加入 wait_handles_ */
    }

    /* 3. 准备 slot 并交给 TabController */
    ChildSlot slot;
    slot.hProcess = hProc;
    slot.pid      = pid;
    slot.title    = title.empty()
                    ? utils::Format(L"Reembed %zu", tab_ctrl_.GetActiveCount() + 1)
                    : title;
    slot.state    = ChildState::Starting;
    int slot_id = tab_ctrl_.AddSlot(std::move(slot));
    if (slot_id < 0) {
        if (hProc) ::CloseHandle(hProc);
        return -1;
    }

    /* 4. SetParent + 插入 Tab */
    if (!tab_ctrl_.EmbedChildWindow(slot_id, child_hwnd)) {
        MHX_LOG_ERROR(L"AdoptExternalWindow: EmbedChildWindow failed");
        tab_ctrl_.RemoveSlot(slot_id);
        /* hProcess 归 ChildSlot 析构 CloseHandle */
        return -1;
    }

    /* 5. 注册 wait handle（hProc 可能 nullptr） */
    if (hProc) AddWaitHandle(hProc);

    MHX_LOG_INFO(L"AdoptExternalWindow OK: slot=%d pid=%lu hwnd=%p",
                 slot_id, pid, child_hwnd);
    return slot_id;
}

/* ============================================================
 * 计数 / 数组维护
 * ============================================================ */
size_t ChildProcessManager::GetActiveCount() const noexcept {
    size_t n = 0;
    /* 注意 tab_ctrl_ 上没有 const 版本 ForEachSlot，避开使用 */
    return tab_ctrl_.GetActiveCount();
}

void ChildProcessManager::AddWaitHandle(HANDLE hProc) {
    if (wait_handles_.size() < MAXIMUM_WAIT_OBJECTS) {
        wait_handles_.push_back(hProc);
    }
}

void ChildProcessManager::RemoveWaitHandle(HANDLE hProc) {
    auto it = std::find(wait_handles_.begin(), wait_handles_.end(), hProc);
    if (it != wait_handles_.end()) {
        wait_handles_.erase(it);
    }
}

} /* namespace mhx */
