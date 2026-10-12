// ============================================================================
//  selftest.cpp — 无界面自检：验证协议调用与功率采集
//  不需要点击界面，直接输出结果，便于自动化验证。
// ============================================================================
#include <windows.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "advapi32.lib")

#include "MiBridge.h"
#include "PowerInfo.h"

using namespace gcc;

static const char* ModeName(uint8_t m) {
    switch (m) {
        case kBalance:     return "均衡 Balance";
        case kPerformance: return "性能 Performance";
        case kQuiet:       return "静音 Quiet";
        case kFullspeed:   return "全速 Fullspeed";
        default:           return "未知/Unknow";
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    printf("========================================\n");
    printf("  斗战者控制台 · 轻量版  自检\n");
    printf("========================================\n\n");

    // 权限
    HANDLE tok = nullptr;
    bool elevated = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION e = {0};
        DWORD sz = sizeof(e);
        if (GetTokenInformation(tok, TokenElevation, &e, sz, &sz))
            elevated = (e.TokenIsElevated != 0);
        CloseHandle(tok);
    }
    printf("[权限] %s\n\n", elevated ? "管理员 ✅" : "普通用户 ⚠（WMI 调用会失败）");

    // ---- 1. 硬件接口 ----
    printf("[1] 硬件接口 root\\WMI : MICommonInterface\n");
    MiBridge bridge;
    std::string err;
    bool ok = bridge.Open(err);
    printf("    连接: %s\n", ok ? "成功 ✅" : ("失败 ❌  " + err).c_str());
    if (ok) {
        uint8_t v = 0;

        if (bridge.GetPerfMode(v, err))
            printf("    性能模式  : %u  (%s) ✅\n", v, ModeName(v));
        else
            printf("    性能模式  : 读取失败 ❌ %s\n", err.c_str());

        if (bridge.GetFanSpeed(v, err))
            printf("    风扇转速  : %u ✅\n", v);
        else
            printf("    风扇转速  : 读取失败 ❌ %s\n", err.c_str());

        if (bridge.GetCpuTemp(v, err))
            printf("    CPU 温度  : %u ✅\n", v);
        else
            printf("    CPU 温度  : 读取失败 ❌ %s\n", err.c_str());

        if (bridge.GetCpuPower(v, err))
            printf("    CPU 功率  : %u ✅\n", v);
        else
            printf("    CPU 功率  : 读取失败 ❌ %s\n", err.c_str());
    }

    // ---- 2. 功率采集 ----
    printf("\n[2] 功率与电池采集（标准接口）\n");
    PowerSnapshot p = CollectPower();
    printf("    交流在线  : %s\n", p.acOnline ? "是" : "否");
    printf("    电池百分比: %u%%\n", p.batteryPercent);
    if (p.hasBattery) {
        printf("    电池电压  : %.2f V\n", p.voltageMv / 1000.0);
        printf("    充电电流  : %d mA\n", p.chargeRateMa);
        printf("    放电电流  : %d mA\n", p.dischargeRateMa);
        printf("    剩余容量  : %u mWh\n", p.remainingMwh);
        printf("    设计容量  : %u mWh\n", p.designedMwh);
        printf("    电池功率  : %.2f W ✅\n", p.BatteryWatts());
    } else {
        printf("    电池信息  : 不可用\n");
    }
    printf("    整机功率  : %.2f W %s\n", p.SystemWatts(),
           p.systemPowerMw == 0 ? "(固件未提供)" : "✅");

    bridge.Close();
    printf("\n========================================\n");
    return ok ? 0 : 1;
}
