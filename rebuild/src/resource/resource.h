/**
 * mhtabx 资源 ID
 */
#pragma once

#define IDI_APP_ICON            101

#define IDR_MAINMENU            200
#define IDR_ACCELERATOR         201
#define IDR_TAB_CONTEXT_MENU    202   /* W6-1: Tab 头右键菜单 */

/* === 对话框（W6-3） === */
#define IDD_SETTINGS            250
#define IDC_SET_INTERVAL_EDIT   1001
#define IDC_SET_TIMEOUT_EDIT    1002
#define IDC_SET_THEME_COMBO     1003

#define IDC_TAB_MAIN            300
#define IDC_STATUS_BAR          301
#define IDC_TOOLBAR             302

/* === 菜单/命令 ID === */
#define ID_FILE_NEW             40001
#define ID_FILE_CLOSE_TAB       40002
#define ID_FILE_EXIT            40003
#define ID_FILE_SETTINGS        40004   /* W6-3: 打开设置对话框 */

#define ID_TAB_NEXT             40010
#define ID_TAB_PREV             40011
#define ID_TAB_RENAME           40012
#define ID_TAB_DETACH           40013
#define ID_TAB_COPY_PATH        40014   /* W6-1: 复制 exe+cmdline 到剪贴板 */
#define ID_TAB_ACTIVATE         40015   /* W6-1: 右键菜单"激活"项（等价于点击 Tab） */

#define ID_HELP_ABOUT           40020
