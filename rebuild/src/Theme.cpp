/**
 * mhtabx - Tab 主题实现
 *
 * W4: FlatModernTheme（明色）
 * W6-4: DarkModernTheme（暗色）+ ThemeFactory CreateTheme
 *
 * 两个主题共享同一份 DrawTab 绘制逻辑，仅传入不同的 ColorScheme。
 * 这种数据驱动的方式让以后再加主题（如 Solarized）只需新增一组颜色。
 */

#include "Theme.h"
#include "TabController.h"

namespace mhx {

namespace {

/* ============================================================
 * 主题颜色集合
 *
 * 一个 ColorScheme 完全决定一种主题的视觉，DrawTab 不关心是 light/dark。
 * ============================================================ */
struct ColorScheme {
    COLORREF sel_bg;
    COLORREF sel_text;
    COLORREF hot_bg;
    COLORREF hot_text;
    COLORREF normal_bg;
    COLORREF normal_text;
    COLORREF pushed_bg;
    COLORREF border;
    COLORREF accent;
    COLORREF client_bg;
};

/* === Flat Modern (light) === */
const ColorScheme& FlatColors() {
    static const ColorScheme s = {
        /* sel       */ RGB(0x00, 0x78, 0xD4), RGB(0xFF, 0xFF, 0xFF),
        /* hot       */ RGB(0xE5, 0xF1, 0xFB), RGB(0x20, 0x20, 0x20),
        /* normal    */ RGB(0xF0, 0xF0, 0xF0), RGB(0x40, 0x40, 0x40),
        /* pushed    */ RGB(0xCC, 0xE4, 0xF7),
        /* border    */ RGB(0xC7, 0xC7, 0xC7),
        /* accent    */ RGB(0x00, 0x78, 0xD4),
        /* client_bg */ RGB(0xF5, 0xF5, 0xF8),
    };
    return s;
}

/* === Dark Modern (VS Code Dark+ 风) === */
const ColorScheme& DarkColors() {
    static const ColorScheme s = {
        /* sel       */ RGB(0x00, 0x7A, 0xCC), RGB(0xFF, 0xFF, 0xFF),
        /* hot       */ RGB(0x2C, 0x2D, 0x33), RGB(0xE0, 0xE0, 0xE0),
        /* normal    */ RGB(0x1E, 0x1E, 0x1E), RGB(0xCC, 0xCC, 0xCC),
        /* pushed    */ RGB(0x33, 0x44, 0x55),
        /* border    */ RGB(0x33, 0x33, 0x33),
        /* accent    */ RGB(0x00, 0x7A, 0xCC),
        /* client_bg */ RGB(0x25, 0x25, 0x26),
    };
    return s;
}

/* === 工具：填充 + 边框 === */
void FillSolid(HDC hdc, const RECT& rc, COLORREF color) {
    HBRUSH br = ::CreateSolidBrush(color);
    ::FillRect(hdc, &rc, br);
    ::DeleteObject(br);
}

/* 在矩形底部画 2px 强调条 */
void DrawAccentBar(HDC hdc, const RECT& rc, COLORREF color) {
    RECT bar = rc;
    bar.top = bar.bottom - 2;
    FillSolid(hdc, bar, color);
}

/* ============================================================
 * 通用绘制：用给定 ColorScheme 绘制单个 tab
 *
 * 步骤：
 *   1. 状态选色 → 背景填充
 *   2. selected: 底部 accent bar；非 selected: 右侧 1px 分隔线
 *   3. 文字 + 可选 16x16 图标
 * ============================================================ */
void DrawTabWithScheme(const TabPaintContext& ctx, const ColorScheme& s) {
    if (!ctx.hdc) return;

    /* 1. 背景 */
    COLORREF bg, fg;
    if (ctx.is_selected) {
        bg = s.sel_bg;     fg = s.sel_text;
    } else if (ctx.is_pushed) {
        bg = s.pushed_bg;  fg = s.normal_text;
    } else if (ctx.is_hot) {
        bg = s.hot_bg;     fg = s.hot_text;
    } else {
        bg = s.normal_bg;  fg = s.normal_text;
    }
    FillSolid(ctx.hdc, ctx.rect, bg);

    /* 2. 选中态 accent bar，否则画细分隔线 */
    if (ctx.is_selected) {
        DrawAccentBar(ctx.hdc, ctx.rect, s.accent);
    } else {
        RECT divider = ctx.rect;
        divider.left = divider.right - 1;
        FillSolid(ctx.hdc, divider, s.border);
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

    RECT text_rc = ctx.rect;
    text_rc.left  += 8;
    text_rc.right -= 8;

    /* 图标存在时改用 LEFT 对齐让 [icon][title] 紧凑贴左 */
    UINT text_align = DT_CENTER;
    if (ctx.slot && ctx.slot->icon) {
        constexpr int kIconSize = 16;
        int icon_y = ctx.rect.top + ((ctx.rect.bottom - ctx.rect.top) - kIconSize) / 2;
        ::DrawIconEx(ctx.hdc, text_rc.left, icon_y, ctx.slot->icon,
                     kIconSize, kIconSize, 0, nullptr, DI_NORMAL);
        text_rc.left += kIconSize + 6;
        text_align = DT_LEFT;
    }

    ::DrawTextW(ctx.hdc, title.c_str(), -1, &text_rc,
                text_align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    ::RestoreDC(ctx.hdc, saved);
}

} /* namespace */

/* ============================================================
 * FlatModernTheme
 * ============================================================ */
void FlatModernTheme::DrawTab(const TabPaintContext& ctx) {
    DrawTabWithScheme(ctx, FlatColors());
}
COLORREF FlatModernTheme::GetClientBgColor() const {
    return FlatColors().client_bg;
}

/* ============================================================
 * DarkModernTheme (W6-4)
 * ============================================================ */
void DarkModernTheme::DrawTab(const TabPaintContext& ctx) {
    DrawTabWithScheme(ctx, DarkColors());
}
COLORREF DarkModernTheme::GetClientBgColor() const {
    return DarkColors().client_bg;
}

/* ============================================================
 * W6-4 ThemeFactory
 *
 * 字符串到 ITheme 的简单查表。未知名称回退到 FlatModern，避免
 * 用户在 ini 里手填错时整个程序起不来。
 * ============================================================ */
std::unique_ptr<ITheme> CreateTheme(const String& name) {
    if (name == L"DarkModern") {
        return std::make_unique<DarkModernTheme>();
    }
    /* 默认 / 兜底 */
    return std::make_unique<FlatModernTheme>();
}

} /* namespace mhx */
