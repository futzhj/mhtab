/**
 * mhtabx - Tab 主题接口
 *
 * 把 Tab 头部的绘制策略抽象成 ITheme，可替换/扩展为多种皮肤。
 * 默认实现 FlatModernTheme 提供现代扁平风格（VS Code 类似）。
 *
 * 调用关系：
 *   1. TabController::Create 用 TCS_OWNERDRAWFIXED 样式
 *   2. SysTabControl32 把 WM_DRAWITEM 转发给 parent (MainFrame)
 *   3. MainFrame::WndProc 把 WM_DRAWITEM 路由回 TabController::OnDrawItem
 *   4. TabController::OnDrawItem 构造 TabPaintContext 调 ITheme::DrawTab
 */

#pragma once
#include "common.h"

namespace mhx {

struct ChildSlot;

/* ============================================================
 * 单次 Tab 绘制的上下文
 * ============================================================ */
struct TabPaintContext {
    HDC               hdc          = nullptr;
    RECT              rect         = {};
    const ChildSlot*  slot         = nullptr;     /* 可能为 nullptr（未关联 slot 时） */
    int               tab_index    = -1;
    bool              is_selected  = false;
    bool              is_hot       = false;       /* 鼠标悬停 */
    bool              is_pushed    = false;       /* 鼠标按下中（拖拽 candidate） */
};

/* ============================================================
 * 主题接口
 * ============================================================ */
class ITheme {
public:
    virtual ~ITheme() = default;

    /** 绘制单个 Tab 头 */
    virtual void DrawTab(const TabPaintContext& ctx) = 0;

    /** 内容区背景色（暂未使用，保留供 W4 扩展） */
    virtual COLORREF GetClientBgColor() const = 0;

    /** Tab 头高度（建议值，TabCtrl 实际用 TCM_SETITEMSIZE 才能强制） */
    virtual int GetTabHeight() const = 0;
};

/* ============================================================
 * 默认主题：扁平现代风
 *
 * 颜色取自常见 IDE 风格：
 *   - selected: #0078D4 蓝底白字
 *   - hot:      #E5F1FB 浅蓝
 *   - normal:   #F0F0F0 浅灰
 *   - text:     #202020 深灰（normal/hot），#FFFFFF（selected）
 *   - border:   #C7C7C7 浅灰
 * ============================================================ */
class FlatModernTheme : public ITheme {
public:
    void DrawTab(const TabPaintContext& ctx) override;
    COLORREF GetClientBgColor() const override { return RGB(245, 245, 248); }
    int      GetTabHeight()     const override { return 28; }
};

} /* namespace mhx */
