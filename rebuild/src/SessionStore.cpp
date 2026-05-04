/**
 * mhtabx - SessionStore 实现
 */

#include "SessionStore.h"
#include "Utils.h"

namespace mhx {

constexpr const wchar_t* kSection = L"Session";
constexpr const wchar_t* kKeyCount = L"count";

SessionStore::SessionStore(const String& ini_path)
    : ini_path_(ini_path) {
}

/* ============================================================
 * Load
 * ============================================================ */
std::vector<SessionEntry> SessionStore::Load() const {
    std::vector<SessionEntry> result;

    int count = utils::ReadIniInt(ini_path_, kSection, kKeyCount, 0);
    if (count <= 0) return result;

    /* 限定上限避免恶意 ini 写出超大值导致循环爆炸 */
    constexpr int kHardLimit = 64;
    if (count > kHardLimit) count = kHardLimit;

    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        String section = utils::Format(L"Tab%d", i);
        SessionEntry e;
        e.exe_path = utils::ReadIniString(ini_path_, section.c_str(), L"exe");
        e.args     = utils::ReadIniString(ini_path_, section.c_str(), L"args");
        e.title    = utils::ReadIniString(ini_path_, section.c_str(), L"title");

        if (e.exe_path.empty()) {
            MHX_LOG_WARN(L"SessionStore: Tab%d has empty exe, skipped", i);
            continue;
        }
        result.push_back(std::move(e));
    }

    MHX_LOG_INFO(L"SessionStore::Load: %zu entries from %s",
                 result.size(), ini_path_.c_str());
    return result;
}

/* ============================================================
 * Save
 * ============================================================ */
bool SessionStore::Save(const std::vector<SessionEntry>& entries) const {
    /* 清掉旧 count，再覆盖各 Tab 段。
     * GetPrivateProfileString 在键缺失时返回 default，所以多余的旧段不会"污染"读取，
     * 只是占用磁盘空间，可接受。 */
    bool ok = utils::WriteIniInt(ini_path_, kSection, kKeyCount,
                                  static_cast<int>(entries.size()));

    for (size_t i = 0; i < entries.size(); ++i) {
        String section = utils::Format(L"Tab%zu", i);
        ok &= utils::WriteIniString(ini_path_, section.c_str(),
                                     L"exe",   entries[i].exe_path.c_str());
        ok &= utils::WriteIniString(ini_path_, section.c_str(),
                                     L"args",  entries[i].args.c_str());
        ok &= utils::WriteIniString(ini_path_, section.c_str(),
                                     L"title", entries[i].title.c_str());
    }

    MHX_LOG_INFO(L"SessionStore::Save: %zu entries -> %s (ok=%d)",
                 entries.size(), ini_path_.c_str(), ok);
    return ok;
}

/* ============================================================
 * Clear
 * ============================================================ */
void SessionStore::Clear() const {
    utils::WriteIniInt(ini_path_, kSection, kKeyCount, 0);
    MHX_LOG_INFO(L"SessionStore::Clear: %s", ini_path_.c_str());
}

} /* namespace mhx */
