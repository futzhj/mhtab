/**
 * mhtabx - W6-3 设置对话框
 *
 * 把 mhtabx.ini 中的运行时参数（心跳/主题）通过 Win32 模态对话框暴露给用户。
 *
 * 设计目标：
 *   - 不引入第三方 GUI 库，纯 Win32 DialogBox
 *   - 保存按"接受"按钮触发，取消则不动 ini
 *   - 调用方仅需提供 ini_path 和 parent hwnd
 */

#pragma once
#include "common.h"

namespace mhx {

struct SettingsValues {
    ULONG interval_ms = 1000;       /* 心跳发送间隔 */
    ULONG timeout_ms  = 3000;       /* 心跳超时阈值 */
    String theme_name = L"FlatModern";
};

/**
 * 从 ini_path 读取所有设置项，未配置项使用 SettingsValues 的默认值。
 * 不写文件，不会修改任何状态。
 */
SettingsValues LoadSettings(const String& ini_path);

/**
 * 把 SettingsValues 写回 ini_path。覆盖式写入，未涉及的 section 不动。
 */
void SaveSettings(const String& ini_path, const SettingsValues& v);

/**
 * 弹出模态对话框让用户编辑设置。
 *
 * @param parent     父窗口（一般是主窗口）
 * @param ini_path   要读写的 ini 文件路径
 * @param out        如果用户点击"确定"，写入用户填写的新值；否则不修改
 * @return true 用户点击"确定"且 out 已更新；false 用户取消或对话框创建失败
 */
bool ShowSettingsDialog(HWND parent, const String& ini_path, SettingsValues& out);

} /* namespace mhx */
