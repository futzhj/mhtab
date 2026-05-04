/**
 * mhtabx - 默认主题 FlatModernTheme 实现
 */

#include "Theme.h"
#include "TabController.h"

namespace mhx {

namespace {

/* === 颜色常量 === */
constexpr COLORREF kColSelectedBg   = RGB(0x00, 0x78, 0xD4);
constexpr COLORREF kColSelectedText = RGB(0xFF, 0xFF, 0xFF);

constexpr COLORREF kColHotBg        = RGB(0xE5, 0xF1, 0xFB);
constexpr COLORREF kColHotText      = RGB(0x20, 0x20, 0x20);

constexpr COLORREF kColNormalBg     = RGB(0xF0, 0xF0, 0xF0);
constexpr COLORREF kColNormalText   = RGB(0x40, 0x40, 0x40);

constexpr COLORREF kColPushedBg     = RGB(0xCC, 0xE4, 0xF7);

constexpr COLORREF kColBorder       = RGB(0xC7, 0xC7, 0xC7);
constexpr COLORREF kColAccent       = RGB(0x00, 0x78, 0xD4);

/* === 工具：填充 + 边框 === */
void FillSolid(HDC hdc, const RECT& rc, COLORREF color) {
    HBRUSH br = ::CreateSolidBrush(color);
    ::FillRect(hdc, &rc, br);
    ::DeleteObject(br);
}

/* 在矩形底部画 1px 强调线 */
void DrawAccentBar(HDC hdc, const RECT& rc, COLORREF color) {
    RECT bar = rc;
    bar.top = bar.bottom - 2;     /* 2px 厚 */
    FillSolid(hdc, bar, color);
}

} /* namespace */

/* ============================================================
 * DrawTab
 *
 * 绘制顺序：
 *   1. 背景填充（按状态选色）
 *   2. 选中时底部画 accent bar
 *   3. 标签文字居中
 * ============================================================ */
void FlatModernTheme::DrawTab(const TabPaintContext& ctx) {
    if (!ctx.hdc) return;

    /* 1. 背景 */
    COLORREF bg, fg;
    if (ctx.is_selected) {
        bg = kColSelectedBg;
        fg = kColSelectedText;
    } else if (ctx.is_pushed) {
        bg = kColPushedBg;
        fg = kColNormalText;
    } else if (ctx.is_hot) {
        bg = kColHotBg;
        fg = kColHotText;
    } else {
        bg = kColNormalBg;
        fg = kColNormalText;
    }
    FillSolid(ctx.hdc, ctx.rect, bg);

    /* 2. 选中态画底部强调条（增加可识别性） */
    if (ctx.is_selected) {
        DrawAccentBar(ctx.hdc, ctx.rect, kColAccent);
    } else {
        /* 非选中 Tab 之间画细分隔线 */
        RECT divider = ctx.rect;
        divider.left = divider.right - 1;
        FillSolid(ctx.hdc, divider, kColBorder);
    }

    /* 3. 文字 + W6-2 图标 */
    String title;
    if (ctx.slot && !ctx.slot->title.empty()) {
        title = ctx.slot->title;
    } else {
        title = L"(no title)";
    }

    int saved = ::SaveDC(ctx.hdc);
    ::SetBkMode(ctx.hdc, TRANSPARENT);
    ::SetTextColor(ctx.hdc, fg);

    /* 留 8px 内边距 */
    RECT text_rc = ctx.rect;
    text_rc.left  += 8;
    text_rc.right -= 8;

    /* W6-2: 图标在文字左侧居中 16x16，文字 rect 整体右移
     *
     * 文字对齐策略：
     *   - 无图标：DT_CENTER 居中（保持原视觉）
     *   - 有图标：DT_LEFT 让 [icon][title] 紧密贴在 tab 左侧 */
    UINT text_align = DT_CENTER;
    if (ctx.slot && ctx.slot->icon) {
        constexpr int kIconSize = 16;
        int icon_y = ctx.rect.top + ((ctx.rect.bottom - ctx.rect.top) - kIconSize) / 2;
        ::DrawIconEx(ctx.hdc, text_rc.left, icon_y, ctx.slot->icon,
                     kIconSize, kIconSize, 0, nullptr, DI_NORMAL);
        text_rc.left += kIconSize + 6;   /* 6px gap */
        text_align = DT_LEFT;
    }

    ::DrawTextW(ctx.hdc, title.c_str(), -1, &text_rc,
                text_align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    ::RestoreDC(ctx.hdc, saved);
}

} /* namespace mhx */
