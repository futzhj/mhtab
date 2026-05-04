/**
 * mhtabx - 工具函数
 *
 * 聚合无状态的小工具：字符串/哈希/路径/INI。
 */

#pragma once
#include "common.h"

namespace mhx::utils {

/* ============================================================
 * 字符串转换
 * ============================================================ */

/* ANSI → UTF-16 */
String FromAnsi(std::string_view s);

/* UTF-16 → ANSI（仅用于 Win32 API 接口桥接） */
AString ToAnsi(StringView s);

/* 格式化到 std::wstring（内部自动扩容） */
String Format(const wchar_t* fmt, ...);

/* ============================================================
 * 路径 / 工作目录
 * ============================================================ */

/* 获取当前工作目录（末尾保证一个 `\`） */
String GetWorkingDirectory();

/* 当前可执行文件完整路径 */
String GetExecutablePath();

/* 当前可执行文件所在目录（末尾 `\`） */
String GetExecutableDirectory();

/* ============================================================
 * 哈希
 *
 * MD5(workdir) 用作窗口类名后缀，避免不同目录实例冲突。
 * ============================================================ */

/**
 * 计算任意字节数组的 MD5，返回 32 字符小写 hex 字符串。
 * 内部使用 Win32 CryptoAPI (wincrypt.h)。
 */
String Md5Hex(const void* data, size_t size);

/**
 * 直接对当前工作目录计算 MD5，结果缓存后多次调用返回同一值。
 */
String GetWorkingDirectoryFingerprint();

/* ============================================================
 * INI
 *
 * 基于 GetPrivateProfileStringW / WritePrivateProfileStringW。
 * ============================================================ */

/**
 * 从 ini_path 读取 [section]key，不存在则返回 def。
 */
String ReadIniString(const String& ini_path, const wchar_t* section,
                     const wchar_t* key, const wchar_t* def = L"");

int ReadIniInt(const String& ini_path, const wchar_t* section,
               const wchar_t* key, int def = 0);

bool WriteIniString(const String& ini_path, const wchar_t* section,
                    const wchar_t* key, const wchar_t* value);

bool WriteIniInt(const String& ini_path, const wchar_t* section,
                 const wchar_t* key, int value);

/* ============================================================
 * 错误 → 可读字符串
 * ============================================================ */

/* 把 GetLastError() 的错误码翻译成本地化字符串 */
String FormatSystemError(DWORD code);

/* ============================================================
 * DPI（W5-2）
 * ============================================================ */

/**
 * 进程级 DPI 感知设置：按 Win 版本自动降级
 *   Win10 1703+  PER_MONITOR_AWARE_V2
 *   Win10 1607   PER_MONITOR_AWARE
 *   Win8.1       PROCESS_PER_MONITOR_DPI_AWARE（shcore.dll）
 *   Vista+       SetProcessDPIAware
 *
 * 必须在创建任何 HWND 之前调用（否则后续 GetDpiForWindow 拿到旧值）。
 */
void SetupDpiAwareness();

/**
 * 获取某窗口的 DPI。Win10 1607 以下降级为系统 DPI。
 */
UINT GetDpiForHwnd(HWND hwnd);

} /* namespace mhx::utils */
