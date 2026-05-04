/**
 * mhtab.exe 还原 - TabsList 槽位池
 *
 * g_TabsList 是一个 std::vector<ViewSlot*>，每个 ViewSlot 是 0x278(632) 字节。
 * 提供三个操作：分配、释放、按 hProcess 查找。
 *
 * 槽位字段映射（关键偏移）：
 *   +0   (int)   view_id / 空闲时 = -1
 *   +8   (ptr)   ViewContainer* （所属主框架的 view）
 *   +16  (HWND)  container_hwnd
 *   +24  (int)   tab_index (在 Tab 控件中的位置；-1 表示未挂载)
 *   +28  (int)   view_id
 *   +32  (byte)  mouse_down_flag
 *   +272 (byte)  poll_timer_obj 开始
 *   +288 (48B)   View_TimerExpired / TabView_PollTick 使用的计时器对象
 *   +336 (HWND)  game_hwnd（子进程的游戏主窗口）
 *   +344 (HANDLE)hProcess（子进程进程句柄）
 *   +352 (int)   serial
 *   +356 (byte)  active_flag（0=freed, 1=allocated, 2=bound）
 *   +357 ..485   显示名等字符串（strncpy 128 字节）
 *   +600 (byte)  ready_flag
 *   +613 (byte)  on_ready_confirmed
 *   +616 (int)   state_machine（1=closing, 4=timer_mode）
 *   +620 (int)   bucket_30s（Calc_30s_Bucket 结果）
 *   +624 (DWORD) pid
 */

#include "include/globals.h"

/*
 * @0x1400181F0  TabsList::allocSlot
 *
 * 策略：先扫描现有 slot 找 view_id<0 && active_flag==0 的"空闲"项复用；
 * 找不到就 new 一个 0x278 字节的新 slot 并 push_back。
 *
 * 槽位初始化（new 时）：
 *   view_id    = -1
 *   +288 (float) = 0.01 秒（1008981770 ≈ 0x3C23D70A ≈ 0.01f）
 *   +356 = 1    (分配后)
 *   所有指针/偏移清零
 *
 * 返回：slot 指针；*out_idx = 分配到的索引
 */
_QWORD* __fastcall TabsList_AllocSlot(__int64* self, _DWORD* out_idx)
{
    __int64 v4 = 0;
    __int64 v5 = *self;
    unsigned __int64 v6 = (self[1] - *self) >> 3;
    _QWORD* v9 = NULL;

    /* 扫描空闲 slot */
    if (v6) {
        while (v4 < (__int64)v6) {
            __int64 v7 = *(_QWORD*)(v5 + 8 * v4);
            if (*(int*)v7 < 0 && !*(_BYTE*)(v7 + 356))
                break;
            ++v4;
        }
        if (v4 < (__int64)v6) {
            *out_idx = (DWORD)v4;
            if ((DWORD)v4 != (DWORD)-1) {
                v9 = *(_QWORD**)(*self + 8LL * (int)v4);
                goto LABEL_14;
            }
        } else {
            *out_idx = -1;
        }
    } else {
        *out_idx = -1;
    }

    /* 新建 slot */
    _QWORD* v8 = (_QWORD*)operator_new(0x278u);
    v9 = v8;
    if (v8) {
        v8[1] = 0;
        v8[2] = 0;
        v8[3] = -1LL;
        ((_BYTE*)v8)[32]  = 0;
        ((_BYTE*)v8)[272] = 0;
        *(_QWORD*)((char*)v8 + 284) = 0;
        ((_DWORD*)v8)[73] = 1008981770;   /* +292: float 0.01f */
        ((_BYTE*)v8)[296] = 0;
        v8[38] = 0; v8[39] = 0; v8[40] = 0;
        ((_DWORD*)v8)[82] = 0;
        v8[42] = 0; v8[43] = 0;
        ((_DWORD*)v8)[88] = 0;
        ((_BYTE*)v8)[356] = 0;
        *(_DWORD*)v8 = -1;
        ((_BYTE*)v8)[613] = 0;
        v8[77] = 0;
        ((_DWORD*)v8)[156] = 0;
        memset_kr((char*)v8 + 357, 0, 0x100);

        *out_idx = (DWORD)((self[1] - *self) >> 3);

        /* push_back: 用完 capacity 则走 reserve */
        _QWORD* end = (_QWORD*)self[1];
        if (end == (_QWORD*)self[2]) {
            _QWORD* tmp = v9;
            sub_140002E50(self, end, &tmp);    /* Vector_PushBack_Reserve */
        } else {
            *end = (_QWORD)v9;
            self[1] += 8;
        }
    } else {
        *out_idx = (DWORD)((self[1] - *self) >> 3);
    }

    v4 = (int)*out_idx;

LABEL_14:
    *(_DWORD*)v9 = (DWORD)v4;
    ((_BYTE*)v9)[356] = 1;      /* 标记 allocated */
    ++((_DWORD*)self)[6];        /* 有效计数 */
    return v9;
}

/*
 * @0x1400184D0  TabsList::freeSlot
 *
 * 清空 slot 所有字段（保留对象以便后续 allocSlot 复用）
 */
char __fastcall TabsList_FreeSlot(__int64* self, int idx)
{
    __int64 v3 = *self;
    if ((unsigned __int64)idx >= ((self[1] - v3) >> 3)) return 0;
    __int64 v4 = *(_QWORD*)(v3 + 8LL * idx);
    if (!*(_BYTE*)(v4 + 356)) return 0;

    *(_DWORD*)v4 = -1;
    __int64 slot = *(_QWORD*)(*self + 8LL * idx);
    *(_BYTE*)(slot + 356)  = 0;
    *(_DWORD*)slot         = -1;
    *(_BYTE*)(slot + 613)  = 0;
    *(_DWORD*)(slot + 616) = 0;

    /* 清字符串区 + 中间偏移 */
    memset_kr((void*)(slot + 357), 0, 0x100);
    memset_kr((void*)(slot + 32),  0, 0xF0);
    *(_OWORD*)(slot + 272) = 0;
    *(_QWORD*)(slot + 336) = 0;
    *(_QWORD*)(slot + 344) = 0;
    *(_DWORD*)(slot + 624) = 0;
    *(_DWORD*)(slot + 352) = 0;
    *(_DWORD*)(slot + 288) = 0;
    *(_BYTE*)(slot + 296)  = 0;
    *(_DWORD*)(slot + 328) = 0;

    /* 释放 vector<int> 型字段（offset 304~312） */
    sub_140017E70(*(_QWORD*)(slot + 304), *(_QWORD*)(slot + 312),
                  (__int64)(*(_QWORD*)(slot + 312) - *(_QWORD*)(slot + 304)) >> 2, 0);

    --((_DWORD*)self)[6];
    return 1;
}

/*
 * @0x140018640  TabsList::findByProcHandle
 *
 * 遍历所有 slot，返回 (active && slot+344 == hProc) 的索引
 * 用于 TabCtrlApp::waitforCmd 通过 hProcess 定位死亡的子进程 slot
 */
unsigned __int64 __fastcall TabsList_FindByProcHandle(__int64* self, __int64 hProc)
{
    __int64 v2 = *self;
    unsigned __int64 i = 0;
    unsigned __int64 cnt = (self[1] - *self) >> 3;
    if (cnt) {
        do {
            if ((int)i < (int)cnt
                && *(_BYTE*)(*(_QWORD*)(v2 + 8LL * (int)i) + 356LL)
                && hProc == *(_QWORD*)(*(_QWORD*)(v2 + 8 * i) + 344LL))
                break;
            ++i;
        } while (i < cnt);
    }
    return (i == cnt) ? 0xFFFFFFFFu : i;
}

/*
 * @0x14000EAA0  Hash_StringToU32
 *
 * 自定义字符串混淆 hash，用作 workdir instance ID。
 *
 * 预处理：
 *   - 大写 → 小写 (a + 32)
 *   - '/' → '\\'
 *   - 最多处理 4095 字节
 *
 * 主循环（Murmur 风格）：
 *   - 每次读 4 字节做一个 u32 block
 *   - 两条累加链：
 *       v11 (奇) 初值 2002301995 (0x77560E6B)
 *       v8  (偶) 初值  933775118 (0x37A0234E)
 *   - 乘法掩码：
 *       ((v7 + v15) & 0xBDEB77DE) | 0x2040801
 *       ((v7 + v16) & 0x7D7EBBDE) | 0x804021
 *   - v7/v9 每轮 ROL + XOR 0x267B0B11
 *
 * 收尾：XOR 常量 0x9BE74448 / 0x66F42C48
 *
 * 返回：32 位 hash
 *
 * 反逆向价值：相同 workdir 产生相同 hash，不同 workdir 基本不会碰撞，
 * 用于主进程间 IPC 时识别"我们是同一份安装的实例"。
 */
/* 反编译代码过长（包含大量位操作），此处仅文档化。完整代码见 IDB 中 0x14000EAA0 */

/*
 * 以下两个 SingletonA/B 是 SingletonC/D 风格的辅助对象：
 *  - Sub_GetSomeSingletonA @0x140015260 (RookieGuide 状态记录)
 *  - Sub_GetSomeSingletonB @0x1400151A0 (皮肤/主题管理)
 *  - Sub_OnViewLost        @0x14000E4F0 (view 丢失通知)
 */

/*
 * @0x140014890  Calc_30s_Bucket (名称为逆向推测)
 *
 * 把某个 serial 值除以 30 秒刻度，作为"时间段 ID"。
 * 实现细节：两参数签名表明可能是把 (base, delta) 规整到同一时间桶。
 */
