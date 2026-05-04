/**
 * mhtabx - W6-3 设置对话框实现
 *
 * 一切走 Win32 DialogBox + DialogProc。状态通过 DWLP_USER 在多次回调间
 * 共享一个 SettingsValues* 指针。
 */

#include "SettingsDialog.h"
#include "Utils.h"
#include "resource/resource.h"

namespace mhx {

namespace {

/* INI 配置 section 名 */
constexpr const wchar_t* kIniSection         = L"General";
constexpr const wchar_t* kKeyInterval        = L"HeartbeatInterval";
constexpr const wchar_t* kKeyTimeout         = L"HeartbeatTimeout";
constexpr const wchar_t* kKeyTheme           = L"Theme";

/* 主题名称下拉选项（W6-4 实施真实切换） */
constexpr const wchar_t* kThemeOptions[] = {
    L"FlatModern",       /* 默认 */
    L"DarkModern",       /* W6-4 待实现 */
};

/* ------------------------------------------------------------
 * INI 读写
 * ------------------------------------------------------------ */
SettingsValues LoadDefault() {
    return SettingsValues{};
}

/* ------------------------------------------------------------
 * 把对话框上的当前值读回 SettingsValues
 *
 * GetDlgItemInt 对未填或非数字会写 LPBOOL = FALSE，此时回退到默认。
 * ------------------------------------------------------------ */
void ReadFromDialog(HWND hDlg, SettingsValues& v) {
    BOOL ok_int = FALSE;
    UINT iv = ::GetDlgItemInt(hDlg, IDC_SET_INTERVAL_EDIT, &ok_int, FALSE);
    if (ok_int) v.interval_ms = static_cast<ULONG>(iv);

    BOOL ok_to = FALSE;
    UINT to = ::GetDlgItemInt(hDlg, IDC_SET_TIMEOUT_EDIT, &ok_to, FALSE);
    if (ok_to) v.timeout_ms = static_cast<ULONG>(to);

    /* 主题选择从 ComboBox 当前 index 反查到字符串 */
    HWND combo = ::GetDlgItem(hDlg, IDC_SET_THEME_COMBO);
    int sel = static_cast<int>(::SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < static_cast<int>(_countof(kThemeOptions))) {
        v.theme_name = kThemeOptions[sel];
    }
}

/* ------------------------------------------------------------
 * 用 SettingsValues 初始化对话框控件
 *
 * - SetDlgItemInt 写整数（不带千分号）
 * - ComboBox: 先 ResetContent，再 AddString 全部选项，再 SetCurSel 到匹配项
 * ------------------------------------------------------------ */
void WriteToDialog(HWND hDlg, const SettingsValues& v) {
    ::SetDlgItemInt(hDlg, IDC_SET_INTERVAL_EDIT,
                    static_cast<UINT>(v.interval_ms), FALSE);
    ::SetDlgItemInt(hDlg, IDC_SET_TIMEOUT_EDIT,
                    static_cast<UINT>(v.timeout_ms),  FALSE);

    HWND combo = ::GetDlgItem(hDlg, IDC_SET_THEME_COMBO);
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int match = 0;
    for (int i = 0; i < static_cast<int>(_countof(kThemeOptions)); ++i) {
        ::SendMessageW(combo, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(kThemeOptions[i]));
        if (v.theme_name == kThemeOptions[i]) match = i;
    }
    ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(match), 0);
}

/* ------------------------------------------------------------
 * DialogProc
 *
 * lParam 在 WM_INITDIALOG 时传入用户回填指针 SettingsValues*；
 * 之后用 GetWindowLongPtr(GWLP_USERDATA) 取回。
 * ------------------------------------------------------------ */
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsValues* state = reinterpret_cast<SettingsValues*>(
        ::GetWindowLongPtrW(hDlg, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            /* lParam 是调用方传入的初始 SettingsValues* */
            state = reinterpret_cast<SettingsValues*>(lp);
            ::SetWindowLongPtrW(hDlg, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(state));
            if (state) WriteToDialog(hDlg, *state);
            /* 让 EDIT 拿到默认焦点 */
            ::SetFocus(::GetDlgItem(hDlg, IDC_SET_INTERVAL_EDIT));
            return FALSE;   /* 我们已自定义焦点，返回 FALSE 阻止 dialog 默认聚焦 */
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wp);
            if (id == IDOK) {
                if (state) ReadFromDialog(hDlg, *state);
                ::EndDialog(hDlg, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                ::EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }

        case WM_CLOSE:
            ::EndDialog(hDlg, IDCANCEL);
            return TRUE;
    }
    return FALSE;   /* 让默认对话框过程继续处理 */
}

} /* namespace */

/* ============================================================
 * Public API
 * ============================================================ */

SettingsValues LoadSettings(const String& ini_path) {
    SettingsValues v = LoadDefault();
    /* GetPrivateProfileInt 在 ini 不存在时返回 default */
    int iv = utils::ReadIniInt(ini_path, kIniSection, kKeyInterval,
                                static_cast<int>(v.interval_ms));
    int to = utils::ReadIniInt(ini_path, kIniSection, kKeyTimeout,
                                static_cast<int>(v.timeout_ms));
    String th = utils::ReadIniString(ini_path, kIniSection, kKeyTheme,
                                      v.theme_name.c_str());
    v.interval_ms = static_cast<ULONG>(iv);
    v.timeout_ms  = static_cast<ULONG>(to);
    if (!th.empty()) v.theme_name = th;
    return v;
}

void SaveSettings(const String& ini_path, const SettingsValues& v) {
    utils::WriteIniInt   (ini_path, kIniSection, kKeyInterval,
                          static_cast<int>(v.interval_ms));
    utils::WriteIniInt   (ini_path, kIniSection, kKeyTimeout,
                          static_cast<int>(v.timeout_ms));
    utils::WriteIniString(ini_path, kIniSection, kKeyTheme,
                          v.theme_name.c_str());
}

bool ShowSettingsDialog(HWND parent, const String& ini_path, SettingsValues& out) {
    /* 用文件中的现有值预填充对话框 */
    SettingsValues working = LoadSettings(ini_path);

    /* 取主可执行文件的 HINSTANCE 作为资源加载源 */
    HINSTANCE hInst = ::GetModuleHandleW(nullptr);

    INT_PTR rc = ::DialogBoxParamW(
        hInst,
        MAKEINTRESOURCEW(IDD_SETTINGS),
        parent,
        SettingsDlgProc,
        reinterpret_cast<LPARAM>(&working));

    if (rc == IDOK) {
        SaveSettings(ini_path, working);
        out = working;
        return true;
    }
    return false;
}

} /* namespace mhx */
