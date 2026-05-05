/**
 * mhtabx - 子进程生命周期管理
 *
 * 职责：
 *   1. CreateProcess 启动子进程（注入 mhx 协议参数）
 *   2. 在主消息循环每次迭代调用 Poll()，用 WaitForMultipleObjects(0 timeout)
 *      探测哪个子进程退出
 *   3. 子进程退出后通知 TabController 移除对应 slot
 *
 * 不开后台线程，所有交互都在主线程上完成（与原 mhtab.exe 设计一致）。
 */

#pragma once
#include "common.h"

namespace mhx {

class TabController;

/* 命令行注入 key：传递给子进程让它知道自己的 slot_id 与 host hwnd */
constexpr const wchar_t* kArgParentHwnd = L"--mhx-parent";
constexpr const wchar_t* kArgSlotId     = L"--mhx-slot";
constexpr const wchar_t* kArgInstanceId = L"--mhx-instance";

class ChildProcessManager {
public:
    explicit ChildProcessManager(TabController& tab_ctrl);
    ~ChildProcessManager();

    ChildProcessManager(const ChildProcessManager&) = delete;
    ChildProcessManager& operator=(const ChildProcessManager&) = delete;

    /**
     * 设置 host 信息，命令行注入用。
     * @param host_hwnd  主窗口 HWND（子进程通过它发 IPC 消息）
     * @param instance_id workdir 指纹（u32）
     */
    void SetHostInfo(HWND host_hwnd, u32 instance_id) noexcept;

    /**
     * 启动一个新子进程：
     *   1. 分配新 ChildSlot（先 state=Starting）
     *   2. 拼命令行：`<exe> <user_args> --mhx-parent 0xHWND --mhx-slot N --mhx-instance 0xID`
     *   3. CreateProcess
     *   4. 把 hProcess 加入 wait_handles_ 数组
     *
     * 子进程预期：启动后调用 FindWindow 找到 host，发 MHX_NEW_CLIENT。
     *
     * @param exe_path  子进程可执行文件
     * @param user_args 用户附加参数（透传）
     * @param title     Tab 显示标题
     * @return 分配到的 slot_id；失败 -1
     */
    int LaunchChild(const String& exe_path, const String& user_args, const String& title);

    /**
     * 在主消息循环每次迭代调用：检查是否有子进程退出。
     * 用 WaitForMultipleObjects(timeout=0) 非阻塞探测。
     *
     * 返回退出的 slot_id（-1 表示无退出）。
     */
    int Poll();

    /**
     * 主动请求关闭某 slot 对应的子进程：
     *   1. 标记 state=Closing
     *   2. PostMessage(WM_CLOSE) 或 SendMessage(MHX_HEARTBEAT) 优雅请求
     *   3. 若 force=true，TerminateProcess
     */
    bool RequestClose(int slot_id, bool force = false);

    /**
     * W5-4: 放弃对子进程的 wait 跟踪（不终止子进程）。
     * 主进程不再接收 Poll 退出事件，child 进程从此独立运行。
     *
     * 必须在 TabController::DetachSlot 清理 slot 之前调用。
     */
    bool DetachSlot(int slot_id);

    /**
     * W6-bugfix: 领养一个已存在的外部子进程窗口（通常是之前 DetachSlot 的回流）。
     *
     * 步骤：
     *   1. GetWindowThreadProcessId(child_hwnd) 反查 pid
     *   2. OpenProcess 获取 SYNCHRONIZE 权限的 hProcess，加入 wait handles
     *   3. 构造 ChildSlot(hProcess, pid, child_hwnd, title) 交给 TabController
     *   4. 调用 TabController::EmbedChildWindow 嵌入到 Tab
     *
     * @return 成功时新分配的 slot_id；失败 -1
     *         （child_hwnd 无效 / 已达 max_children / OpenProcess 权限不足 等）
     */
    int AdoptExternalWindow(HWND child_hwnd, const String& title);

    /** 返回当前活跃子进程数 */
    size_t GetActiveCount() const noexcept;

    /** 子进程数量上限（默认 8） */
    void SetMaxChildren(int n) noexcept { max_children_ = n; }
    int  GetMaxChildren() const noexcept { return max_children_; }

private:
    /** 注入 mhx 参数到命令行 */
    String BuildCommandLine(const String& exe_path,
                            const String& user_args,
                            int slot_id) const;

    /** 把 hProcess 加入 wait 数组 */
    void AddWaitHandle(HANDLE hProc);

    /** 从 wait 数组中移除某个 hProcess（数组左移） */
    void RemoveWaitHandle(HANDLE hProc);

private:
    TabController& tab_ctrl_;
    HWND           host_hwnd_     = nullptr;
    u32            instance_id_   = 0;
    int            max_children_  = kMaxChildren;

    /* WaitForMultipleObjects 的句柄数组（最多 64 个） */
    std::vector<HANDLE> wait_handles_;
};

} /* namespace mhx */
