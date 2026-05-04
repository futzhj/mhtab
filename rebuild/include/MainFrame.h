/**
 * mhtabx - 主窗口类
 *
 * W2 阶段：集成 TabController + ChildProcessManager，
 * 处理子进程握手协议（MHX_NEW_CLIENT），自动嵌入子窗口。
 */

#pragma once
#include "common.h"

namespace mhx {

class TabController;
class ChildProcessManager;
class HeartbeatMonitor;
class SessionStore;

class MainFrame {
public:
    MainFrame();
    ~MainFrame();

    /* 禁止拷贝 */
    MainFrame(const MainFrame&) = delete;
    MainFrame& operator=(const MainFrame&) = delete;

    /**
     * 创建主窗口。
     * @param hInstance WinMain 传入的 HINSTANCE
     * @param cmd_line  初始命令行（可能指向要启动的子进程参数）
     * @return true 创建成功
     */
    bool Create(HINSTANCE hInstance, const String& cmd_line);

    /**
     * 显示窗口（通常 nShowCmd 透传给 ShowWindow）。
     */
    void Show(int nShowCmd);

    /**
     * 接收来自其他实例的命令行转发（W2 后会路由到子进程创建流程）。
     */
    void HandleForwardedCmdLine(const String& cmd_line);

    /**
     * 查询此实例的 fingerprint（INST_QUERY_INSTANCE_ID 消息处理用）。
     */
    u32 GetInstanceId() const noexcept { return instance_id_; }

    HWND GetHwnd() const noexcept { return hwnd_; }

    /* 注册主窗口类（在 Create 前一次即可，内部已做幂等保护） */
    static bool RegisterWindowClass(HINSTANCE hInstance, const String& class_name);

private:
    /* C++ this 绑定的窗口过程 thunk */
    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    /* 实际消息分发 */
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    /* 具体消息处理 */
    LRESULT OnCreate(LPCREATESTRUCTW cs);
    LRESULT OnDestroy();
    LRESULT OnPaint();
    LRESULT OnSize(int cx, int cy);
    LRESULT OnCopyData(HWND from, const COPYDATASTRUCT* cds);
    LRESULT OnTimer(UINT_PTR id);
    LRESULT OnNotify(int ctrl_id, NMHDR* hdr);
    LRESULT OnCommand(WORD id, WORD code, HWND ctrl);

    /* IPC 消息处理（子进程发来） */
    LRESULT OnNewClient(WPARAM slot_id, LPARAM child_hwnd);
    LRESULT OnReadyConfirm(WPARAM slot_id, LPARAM tick);
    LRESULT OnUpdatePos(WPARAM slot_id, LPARAM packed_xy);
    LRESULT OnNewView(WPARAM slot_id, LPARAM hint);
    LRESULT OnCleanupView(WPARAM slot_id, LPARAM lp);

    /* 键盘输入转发到当前 active 子窗口 */
    bool ForwardKeyToActiveChild(UINT msg, WPARAM wp, LPARAM lp);

    /* 启动后延迟初始化：处理命令行 或 恢复上次 session */
    void OnPostInit();

    /* 退出前保存当前 Tab 列表 */
    void SaveSession();

    /* W5-1: 刷新状态栏 3 个分区的文本 */
    void UpdateStatusBar();

    /* W5-1/W5-2: 按当前 DPI 设置状态栏分区的像素边界 */
    void SetStatusBarPartsForDpi(UINT dpi);

    /* W5-2: 处理 WM_DPICHANGED（窗口被移到不同 DPI 显示器时） */
    LRESULT OnDpiChanged(UINT new_dpi, const RECT* suggested);

    /** 启动一个示例子进程（demo_child.exe），用于 W2 集成测试 */
    void LaunchDemoChild();

private:
    HINSTANCE hInstance_   = nullptr;
    HWND      hwnd_        = nullptr;
    u32       instance_id_ = 0;          /* MD5(workdir) 前 32 位 */
    String    class_name_;               /* 完整窗口类名 */
    String    pending_cmd_line_;         /* OnCreate 后再处理 */

    /* W2: Tab 控件 + 子进程管理 */
    std::unique_ptr<TabController>       tab_ctrl_;
    std::unique_ptr<ChildProcessManager> child_mgr_;

    /* W3: 心跳监控 */
    std::unique_ptr<HeartbeatMonitor>    heartbeat_;

    /* W4: Session 持久化 */
    std::unique_ptr<SessionStore>        session_store_;

    /* W5-1: 状态栏 */
    HWND status_bar_ = nullptr;

    /* W5-2: 当前窗口 DPI（用于按比例缩放 UI 元素） */
    UINT current_dpi_ = 96;
};

} /* namespace mhx */
