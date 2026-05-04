/**
 * mhtabx - 主窗口类
 *
 * W1 阶段：仅空壳（创建窗口、处理 WM_DESTROY、显示一个居中提示文字）
 * W2 阶段：会扩展为包含 TabController 成员
 */

#pragma once
#include "common.h"

namespace mhx {

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

private:
    HINSTANCE hInstance_   = nullptr;
    HWND      hwnd_        = nullptr;
    u32       instance_id_ = 0;          /* MD5(workdir) 前 32 位 */
    String    class_name_;               /* 完整窗口类名 */
    String    pending_cmd_line_;         /* OnCreate 后再处理 */
};

} /* namespace mhx */
