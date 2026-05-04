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

    /* 3. 文字 */
    String title;
    if (ctx.slot && !ctx.slot->title.empty()) {
        title = ctx.slot->title;
    } else {
        title = L"(no title)";
    }

    int saved = ::SaveDC(ctx.hdc);
    ::SetBkMode(ctx.hdc, TRANSPARENT);
    ::SetTextColor(ctx.hdc, fg);

    /* 留 8px 内边距，超长省略 */
    RECT text_rc = ctx.rect;
    text_rc.left  += 8;
    text_rc.right -= 8;

    ::DrawTextW(ctx.hdc, title.c_str(), -1, &text_rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    ::RestoreDC(ctx.hdc, saved);
}

} /* namespace mhx */
