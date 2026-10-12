// test_sched.cpp — 时间段调度逻辑自测（不需要硬件/管理员）
#include <cstdio>
#include <vector>
#include "Schedule.h"

using namespace gcc;

static int Fails = 0;

static void Expect(bool cond, const char* desc) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", desc);
    if (!cond) ++Fails;
}

int main() {
    printf("=== 时间段调度逻辑测试 ===\n\n");

    // 1. 普通区间 08:00-18:00
    ScheduleRule work{ true, 8*60, 18*60, kSchedPerformance };
    printf("规则: 08:00-18:00 -> 性能\n");
    Expect( RuleMatches(work, 7*60+59) == false, "07:59 不匹配");
    Expect( RuleMatches(work, 8*60)    == true,  "08:00 匹配");
    Expect( RuleMatches(work, 12*60)   == true,  "12:00 匹配");
    Expect( RuleMatches(work, 17*60+59)== true,  "17:59 匹配");
    Expect( RuleMatches(work, 18*60)   == false, "18:00 不匹配（右开区间）");
    Expect( RuleMatches(work, 23*60)   == false, "23:00 不匹配");

    // 2. 跨午夜 22:00-06:00
    ScheduleRule night{ true, 22*60, 6*60, kSchedQuiet };
    printf("\n规则: 22:00-06:00 -> 静音（跨午夜）\n");
    Expect( RuleMatches(night, 23*60)  == true,  "23:00 匹配");
    Expect( RuleMatches(night, 0)      == true,  "00:00 匹配");
    Expect( RuleMatches(night, 3*60)   == true,  "03:00 匹配");
    Expect( RuleMatches(night, 5*60+59)== true,  "05:59 匹配");
    Expect( RuleMatches(night, 6*60)   == false, "06:00 不匹配");
    Expect( RuleMatches(night, 12*60)  == false, "12:00 不匹配");
    Expect( RuleMatches(night, 21*60+59)==false, "21:59 不匹配");

    // 3. 停用的规则
    ScheduleRule off{ false, 8*60, 18*60, kSchedBalance };
    printf("\n规则: 停用\n");
    Expect( RuleMatches(off, 12*60) == false, "停用规则永不匹配");

    // 4. 空区间
    ScheduleRule empty{ true, 8*60, 8*60, kSchedBalance };
    printf("\n规则: 空区间 08:00-08:00\n");
    Expect( RuleMatches(empty, 8*60) == false, "空区间不匹配");

    // 5. 多规则优先级
    printf("\n多规则匹配（第一条命中生效）\n");
    std::vector<ScheduleRule> rules = {
        { true, 8*60,  18*60, kSchedPerformance },
        { true, 18*60, 23*60, kSchedBalance     },
        { true, 22*60, 6*60,  kSchedQuiet       },
    };
    uint8_t m; int idx;
    Expect(FindActiveMode(rules, 10*60, m, idx) && idx == 0 && m == kSchedPerformance, "10:00 -> 规则0 性能");
    Expect(FindActiveMode(rules, 20*60, m, idx) && idx == 1 && m == kSchedBalance,     "20:00 -> 规则1 均衡");
    Expect(FindActiveMode(rules, 23*60+30, m, idx) && idx == 2 && m == kSchedQuiet,    "23:30 -> 规则2 静音");
    Expect(FindActiveMode(rules, 3*60, m, idx) && idx == 2 && m == kSchedQuiet,        "03:00 -> 规则2 静音");
    // 07:00 落在 06:00-08:00 空档（规则2 到 06:00 结束，规则0 从 08:00 开始）-> 无匹配
    Expect(FindActiveMode(rules, 7*60, m, idx) == false,                               "07:00 -> 空档无匹配");

    // 6. 无匹配
    std::vector<ScheduleRule> only = { { true, 8*60, 18*60, kSchedPerformance } };
    Expect(FindActiveMode(only, 20*60, m, idx) == false, "20:00 无匹配");

    // 7. 时间解析与格式化
    printf("\n时间解析 / 格式化\n");
    Expect(ParseMinutes(L"08:00") == 480,  "08:00 -> 480");
    Expect(ParseMinutes(L"23:59") == 1439, "23:59 -> 1439");
    Expect(ParseMinutes(L"00:00") == 0,    "00:00 -> 0");
    Expect(ParseMinutes(L"abc")   == -1,   "非法输入 -> -1");
    Expect(ParseMinutes(L"25:00") == -1,   "25:00 -> -1");
    Expect(ParseMinutes(L"12:99") == -1,   "12:99 -> -1");
    Expect(FmtMinutes(480)  == L"08:00",   "480 -> 08:00");
    Expect(FmtMinutes(1439) == L"23:59",   "1439 -> 23:59");

    printf("\n=== %s (失败 %d) ===\n", Fails == 0 ? "全部通过" : "有失败", Fails);
    return Fails == 0 ? 0 : 1;
}
