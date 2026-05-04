/**
 * mhtabx - 工具函数实现
 */

#include "Utils.h"

#include <wincrypt.h>
#include <cstdarg>
#include <cwchar>

#pragma comment(lib, "advapi32")

namespace mhx {

/* ============================================================
 * LogImpl 在 common.h 中声明
 * ============================================================ */
void LogImpl(LogLevel level, const wchar_t* file, int line, const wchar_t* fmt, ...) {
    const wchar_t* level_tag = L"?";
    switch (level) {
        case LogLevel::Trace: level_tag = L"TRACE"; break;
        case LogLevel::Info:  level_tag = L"INFO ";  break;
        case LogLevel::Warn:  level_tag = L"WARN "; break;
        case LogLevel::Error: level_tag = L"ERROR"; break;
    }

    /* 拆出文件名 */
    const wchar_t* base = file;
    for (const wchar_t* p = file; *p; ++p) {
        if (*p == L'\\' || *p == L'/') base = p + 1;
    }

    wchar_t msg[1024];
    int off = _snwprintf_s(msg, _TRUNCATE,
                           L"[%s] %s:%d  ", level_tag, base, line);
    if (off < 0) off = 0;

    va_list va;
    va_start(va, fmt);
    _vsnwprintf_s(msg + off, _countof(msg) - off, _TRUNCATE, fmt, va);
    va_end(va);

    /* 确保末尾有换行 */
    size_t len = wcslen(msg);
    if (len > 0 && msg[len - 1] != L'\n' && len + 1 < _countof(msg)) {
        msg[len] = L'\n';
        msg[len + 1] = L'\0';
    }

    ::OutputDebugStringW(msg);

#ifdef _DEBUG
    fputws(msg, stderr);
#endif
}

} /* namespace mhx */

namespace mhx::utils {

/* ============================================================
 * 字符串转换
 * ============================================================ */
String FromAnsi(std::string_view s) {
    if (s.empty()) return {};
    int need = ::MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    if (need <= 0) return {};
    String out(need, L'\0');
    ::MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), out.data(), need);
    return out;
}

AString ToAnsi(StringView s) {
    if (s.empty()) return {};
    int need = ::WideCharToMultiByte(CP_ACP, 0, s.data(), (int)s.size(),
                                     nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    AString out(need, '\0');
    ::WideCharToMultiByte(CP_ACP, 0, s.data(), (int)s.size(),
                          out.data(), need, nullptr, nullptr);
    return out;
}

String Format(const wchar_t* fmt, ...) {
    va_list va1, va2;
    va_start(va1, fmt);
    va_copy(va2, va1);

    int need = _vscwprintf(fmt, va1);
    va_end(va1);
    if (need < 0) { va_end(va2); return {}; }

    String out(need, L'\0');
    _vsnwprintf_s(out.data(), need + 1, _TRUNCATE, fmt, va2);
    va_end(va2);
    return out;
}

/* ============================================================
 * 路径
 * ============================================================ */
String GetWorkingDirectory() {
    DWORD need = ::GetCurrentDirectoryW(0, nullptr);
    if (need == 0) return {};
    String buf(need, L'\0');
    DWORD got = ::GetCurrentDirectoryW(need, buf.data());
    if (got == 0) return {};
    buf.resize(got);
    if (!buf.empty() && buf.back() != L'\\') buf.push_back(L'\\');
    return buf;
}

String GetExecutablePath() {
    /* Windows 路径最长 32767（UNICODE），但 MAX_PATH=260 对绝大多数情况够 */
    String buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD got = ::GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (got == 0) return {};
        if (got < buf.size()) { buf.resize(got); return buf; }
        /* 缓冲区不够，翻倍重试 */
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2, L'\0');
    }
}

String GetExecutableDirectory() {
    String p = GetExecutablePath();
    size_t pos = p.find_last_of(L'\\');
    if (pos == String::npos) return p;
    return p.substr(0, pos + 1);
}

/* ============================================================
 * MD5 哈希（Windows CryptoAPI）
 * ============================================================ */
static constexpr wchar_t kHexLower[] = L"0123456789abcdef";

String Md5Hex(const void* data, size_t size) {
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    String result;

    if (!::CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL,
                                CRYPT_VERIFYCONTEXT))
        return result;

    if (!::CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
        ::CryptReleaseContext(prov, 0);
        return result;
    }

    if (!::CryptHashData(hash, static_cast<const BYTE*>(data), (DWORD)size, 0)) {
        ::CryptDestroyHash(hash);
        ::CryptReleaseContext(prov, 0);
        return result;
    }

    BYTE  digest[16] = {};
    DWORD dlen = sizeof(digest);
    if (::CryptGetHashParam(hash, HP_HASHVAL, digest, &dlen, 0)) {
        result.reserve(32);
        for (DWORD i = 0; i < dlen; ++i) {
            result.push_back(kHexLower[digest[i] >> 4]);
            result.push_back(kHexLower[digest[i] & 0x0F]);
        }
    }

    ::CryptDestroyHash(hash);
    ::CryptReleaseContext(prov, 0);
    return result;
}

String GetWorkingDirectoryFingerprint() {
    /* 静态缓存：同进程内只算一次 */
    static std::once_flag once;
    static String cached;
    std::call_once(once, [] {
        String wd = GetWorkingDirectory();
        if (wd.empty()) return;
        cached = Md5Hex(wd.data(), wd.size() * sizeof(wchar_t));
    });
    return cached;
}

/* ============================================================
 * INI 读写
 * ============================================================ */
String ReadIniString(const String& ini_path, const wchar_t* section,
                     const wchar_t* key, const wchar_t* def) {
    wchar_t buf[1024];
    ::GetPrivateProfileStringW(section, key, def, buf,
                               _countof(buf), ini_path.c_str());
    return String(buf);
}

int ReadIniInt(const String& ini_path, const wchar_t* section,
               const wchar_t* key, int def) {
    return (int)::GetPrivateProfileIntW(section, key, def, ini_path.c_str());
}

bool WriteIniString(const String& ini_path, const wchar_t* section,
                    const wchar_t* key, const wchar_t* value) {
    return ::WritePrivateProfileStringW(section, key, value, ini_path.c_str()) != 0;
}

bool WriteIniInt(const String& ini_path, const wchar_t* section,
                 const wchar_t* key, int value) {
    wchar_t tmp[32];
    _snwprintf_s(tmp, _TRUNCATE, L"%d", value);
    return WriteIniString(ini_path, section, key, tmp);
}

/* ============================================================
 * 系统错误格式化
 * ============================================================ */
String FormatSystemError(DWORD code) {
    wchar_t* buf = nullptr;
    DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&buf, 0, nullptr);
    String out;
    if (n > 0 && buf) {
        /* 去掉末尾换行 */
        while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L'\n')) --n;
        out.assign(buf, n);
    }
    if (buf) ::LocalFree(buf);
    return Format(L"%s (0x%08lX)",
                  out.empty() ? L"Unknown error" : out.c_str(), code);
}

} /* namespace mhx::utils */
