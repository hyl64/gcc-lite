// ============================================================================
//  Schedule.h — 时间段调度（HH:MM - HH:MM → 性能模式）
//
//  设计：
//    - 最多 MAX_RULES 条规则，每条：起始时间、结束时间、目标模式、是否启用
//    - 支持跨午夜时间段（如 22:00 - 06:00）
//    - 每 10 秒检查一次，进入某时间段时切换到对应模式
//    - 规则按顺序匹配，第一条命中的生效
//    - 时间段之外：不干预（保持上一次设置），或可选回退到指定模式
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace gcc {

constexpr int MAX_RULES = 6;

// 模式取值（与 MiBridge.h 的 PerfMode 一致）
enum SchedMode : uint8_t {
    kSchedBalance     = 0,
    kSchedPerformance = 1,
    kSchedQuiet       = 2,
    kSchedFullspeed   = 3,
};

struct ScheduleRule {
    bool    enabled  = false;
    int     startMin = 0;      // 起始时间（自 00:00 起的分钟数）
    int     endMin   = 0;      // 结束时间
    uint8_t mode     = kSchedBalance;
};

// 判断当前时间是否落在规则内（支持跨午夜）
inline bool RuleMatches(const ScheduleRule& r, int nowMin) {
    if (!r.enabled) return false;
    if (r.startMin == r.endMin) return false;       // 空区间
    if (r.startMin < r.endMin)                      // 普通区间
        return nowMin >= r.startMin && nowMin < r.endMin;
    // 跨午夜：如 22:00 - 06:00
    return nowMin >= r.startMin || nowMin < r.endMin;
}

// 在规则集中找出当前生效的模式；返回 false 表示无规则命中
inline bool FindActiveMode(const std::vector<ScheduleRule>& rules,
                           int nowMin, uint8_t& modeOut, int& ruleIndexOut) {
    for (int i = 0; i < (int)rules.size(); ++i) {
        if (RuleMatches(rules[i], nowMin)) {
            modeOut = rules[i].mode;
            ruleIndexOut = i;
            return true;
        }
    }
    return false;
}

// 取当前本地时间的分钟数
inline int NowMinutes() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wHour * 60 + st.wMinute;
}

// 把分钟数格式化为 "HH:MM"
inline std::wstring FmtMinutes(int m) {
    if (m < 0) m = 0;
    m %= 1440;
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", m / 60, m % 60);
    return buf;
}

// 解析 "HH:MM" -> 分钟数；失败返回 -1
inline int ParseMinutes(const std::wstring& s) {
    int h = -1, m = -1;
    if (swscanf_s(s.c_str(), L"%d:%d", &h, &m) != 2) return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

} // namespace gcc
