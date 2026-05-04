/**
 * mhtab.exe 还原 - 工具函数
 *
 * 包含：
 *  - GetUniqueIdSuffix     MD5(workdir) 生成窗口类名后缀
 *  - MakeMHXYWinMgrClassName  构造 "MHXYWinMgr" + UID
 *  - GetWorkdirInstanceID  工作目录哈希作为实例 ID
 *  - GetActiveTabCount     检查 mhxy0~mhxy7 互斥锁限制
 *  - get_ini               INI 字符串读取
 *  - my_sprintf / my_sscanf  CRT 包装
 */

#include "include/globals.h"
#include <wincrypt.h>

/*
 * @0x1400146C0  GetUniqueIdSuffix
 *
 * 算法：MD5(GetCurrentDirectoryA()) → 32 字节 hex 字符串
 *
 * 用途：作为窗口类名后缀（防同进程不同目录冲突）
 * 重要常量：
 *   CALG_MD5 = 0x8003
 *   CRYPT_VERIFYCONTEXT = 0xF0000000
 *   HP_HASHVAL = 2
 */
char __fastcall GetUniqueIdSuffix(char* Destination, int dst_size)
{
    BYTE     pbData[16];
    char     Source[48];
    DWORD    pdwDataLen = 16;
    HCRYPTHASH phHash = 0;
    HCRYPTPROV phProv = 0;
    LPSTR    workdir = (LPSTR)&pbData;     /* 通过共用 stack pad 节省空间 */

    DWORD len = GetCurrentDirectoryA(0x400u, workdir);
    if (!len) return 0;

    if (!CryptAcquireContextA(&phProv, 0, 0, PROV_RSA_FULL,
                               CRYPT_VERIFYCONTEXT))
        return 0;
    if (!CryptCreateHash(phProv, CALG_MD5, 0, 0, &phHash)) {
        CryptReleaseContext(phProv, 0);
        return 0;
    }
    if (!CryptHashData(phHash, (BYTE*)workdir, len, 0)) {
        CryptReleaseContext(phProv, 0);
        CryptDestroyHash(phHash);
        return 0;
    }

    if (CryptGetHashParam(phHash, HP_HASHVAL, pbData, &pdwDataLen, 0)) {
        DWORD i;
        for (i = 0; i < pdwDataLen; ++i)
            my_sprintf(&Source[2 * i], "%02x", pbData[i]);
        if (2 * i >= 0x30) _report_rangecheckfailure();
        Source[2 * i] = 0;
    } else {
        GetLastError();
    }
    CryptDestroyHash(phHash);
    CryptReleaseContext(phProv, 0);

    strncpy(Destination, Source, dst_size);
    return 1;
}

/*
 * @0x140014940  MakeMHXYWinMgrClassName
 *
 * 输出: "MHXYWinMgr" + GetUniqueIdSuffix()
 * 同样的拼装方式也用于主窗口类 "MHXYMainFrame"+UID（在 TabCtrlApp_Init 中直接 inline 拼接）
 */
char __fastcall MakeMHXYWinMgrClassName(char* Destination, int dst_size)
{
    char Source[48];
    if (!GetUniqueIdSuffix(Source, 48)) return 0;

    strncpy(Destination, "MHXYWinMgr", dst_size);
    int len = lstrlenA("MHXYWinMgr");
    if (len < dst_size)
        strncpy(&Destination[len], Source, dst_size - len);
    return 1;
}

/*
 * @0x14000EE30  GetWorkdirInstanceID
 *
 * 用 Hash_StringToU32 计算 GetCurrentDirectory() 的 32 位哈希
 * 缓存到 g_workdir_hash_cache，全局只算一次
 *
 * 用途：
 *  - WinMain 中 SendMessage(0x8102) 返回此值，用于辨识"是否同一目录的实例"
 */
__int64 GetWorkdirInstanceID(void)
{
    if (!g_workdir_hash_cache) {
        CHAR Buffer[4096];
        GetCurrentDirectoryA(sizeof(Buffer), Buffer);
        g_workdir_hash_cache = Hash_StringToU32((uint8_t*)Buffer, NULL);
    }
    return (unsigned)g_workdir_hash_cache;
}

/*
 * @0x140018390  GetActiveTabCount
 *
 * 返回 max(本程序内 active view 数, 系统范围内 mhxy0~mhxy7 已被占用的数量)
 * 用于实现"最多 8 个梦幻西游同时运行"上限。
 */
__int64 __fastcall GetActiveTabCount(__int64 tabs_list)
{
    __int64* p = *(__int64**)tabs_list;
    unsigned int local_count  = 0;
    unsigned int system_count = 0;
    CHAR Name[16];

    /* 本进程内 active view */
    for (__int64 i = (__int64)(*(_QWORD*)(tabs_list + 8) - *(_QWORD*)tabs_list) >> 3;
         i; --i)
    {
        __int64 v = *p++;
        unsigned saved = local_count++;
        if (!*(_BYTE*)(v + 356)) local_count = saved;
    }

    /* 系统范围内的命名互斥锁 mhxy0~mhxy7 */
    for (int j = 0; j < 8; ++j) {
        my_sprintf(Name, "mhxy%d", j);
        HANDLE h = OpenMutexA(SYNCHRONIZE, FALSE, Name);
        if (h) { ++system_count; CloseHandle(h); }
    }

    return (int)local_count < (int)system_count ? system_count : local_count;
}

/*
 * @0x140014B40  get_ini
 *
 * 读取当前目录下 "{workdir}\xyz.ini" 中的字符串值
 *
 * 注意：原代码用 std::string 存储参数（IDA 把 std::string 反编译为 _OWORD*）
 *  - workdir / section / key / default 都是 std::string
 *  - 调用 GetCurrentDirectoryA 自动填充 workdir，并保证末尾有 '\\'
 *  - 调用 GetPrivateProfileStringA 读 INI（缓冲区 1024 字节）
 */
_OWORD* __fastcall get_ini(_OWORD* result, _QWORD* workdir,
                            const char* section, const char* key, LPCSTR default_val)
{
    LPCSTR  cwd_str[2] = {0};
    __int64 cwd_len    = 0;
    unsigned __int64 cwd_cap = 15;
    __int128 result_buf = {0};
    __int128 result_meta;
    *(_QWORD*)&result_meta            = 0;
    *((_QWORD*)&result_meta + 1)      = 15;

    /* 1. 确保 g_workdir_buf 已加载 + 末尾有 '\\' */
    if (!g_workdir_buf[0]) {
        GetCurrentDirectoryA(0x400u, g_workdir_buf);
        __int64 len = -1;
        do ++len; while (g_workdir_buf[len]);
        if (!len) goto OUT;
        if (g_workdir_buf[len - 1] != '\\')
            g_workdir_buf[len] = '\\';
    }

    /* 2. 拼接 workdir + 文件名 → 完整 INI 路径 */
    size_t cwd_real_len = 0;
    while (g_workdir_buf[cwd_real_len]) ++cwd_real_len;
    stl_string_assign((void**)cwd_str, g_workdir_buf, cwd_real_len);

    size_t fname_len = workdir[2];
    void* fname_data = (workdir[3] >= 0x10) ? (void*)*workdir : (void*)workdir;
    if (fname_len > cwd_cap - cwd_len) {
        stl_string_grow(cwd_str, fname_len, 0, fname_data, fname_len);
    } else {
        cwd_len += fname_len;
        char* dst = (char*)((cwd_cap >= 0x10 ? (LPCSTR*)cwd_str[0] : cwd_str)) + (cwd_len - fname_len);
        memmove_kr(dst, fname_data, fname_len);
        dst[fname_len] = 0;
    }

    /* 3. 解 std::string 为裸 char* */
    const char* full_path  = (cwd_cap >= 0x10) ? cwd_str[0] : (LPCSTR)cwd_str;
    const char* def_str    = (((_QWORD*)default_val)[3] >= 0x10) ? *(const char**)default_val : default_val;
    const char* key_str    = (((_QWORD*)key)[3]         >= 0x10) ? *(const char**)key         : key;
    const char* sec_str    = (((_QWORD*)section)[3]     >= 0x10) ? *(const char**)section     : section;

    char v24[8];
    Log_Stub(v24, "get_ini, file = %s, session = %s, strName=%s, default = %s\n",
             full_path, sec_str, key_str, def_str);

    /* 4. GetPrivateProfileStringA 读取 */
    CHAR ReturnedString[1024];
    GetPrivateProfileStringA(sec_str, key_str, def_str, ReturnedString, sizeof(ReturnedString),
                             full_path);

    /* 5. 把读取的字符串存入 result（std::string 赋值） */
    size_t ret_len = 0;
    while (ReturnedString[ret_len]) ++ret_len;
    stl_string_assign((void**)&result_buf, ReturnedString, ret_len);

OUT:
    *result        = result_buf;
    result[1]      = result_meta;

    /* 释放临时 std::string */
    if (cwd_cap >= 0x10) {
        char* p = (char*)cwd_str[0];
        if (cwd_cap + 1 < 0x1000 || (p = (char*)*((_QWORD*)cwd_str[0] - 1),
                                      (unsigned __int64)(cwd_str[0] - p - 8) <= 0x1F))
        {
            j_j_free(p);
        } else {
            _invalid_parameter_noinfo_noreturn();
        }
    }
    return result;
}

/* @0x14000BFC0  my_sprintf - sprintf 包装 */
int my_sprintf(char* buf, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int ret = __stdio_common_vsprintf(*(unsigned __int64*)sub_140006970(),
                                      buf, (size_t)-1, fmt, NULL, va);
    va_end(va);
    return ret;
}

/* @0x14000EA40  my_sscanf - sscanf 包装 */
int my_sscanf(const char* buf, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int ret = __stdio_common_vsscanf(*(unsigned __int64*)stdio_get_iob_func(),
                                     buf, (size_t)-1, fmt, NULL, va);
    va_end(va);
    return ret;
}

/* @0x14000AFE0  Log_Stub
 * Release 编译已 NOP 化，此函数仅返回 0。
 * 但保留所有 format string 字符串（便于通过 IDA 字符串视图理解函数语义）。
 */
__int64 Log_Stub(_QWORD a, const char* fmt, ...)
{
    (void)a; (void)fmt;
    return 0;
}
