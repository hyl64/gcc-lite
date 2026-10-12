// ============================================================================
//  test_power.cpp — 验证 PowerInfo 采集（不需要管理员权限）
// ============================================================================
#include "PowerInfo.h"
#include <cstdio>

using namespace gcc;

int main() {
    printf("=== 电源 / 功率采集测试 ===\n\n");

    for (int i = 0; i < 3; ++i) {
        PowerSnapshot s = CollectPower();

        printf("--- 采样 %d ---\n", i + 1);
        printf("  交流在线      : %s\n", s.acOnline ? "是" : "否");
        printf("  AC 状态码     : %u\n", s.acLineStatus);
        printf("  电池百分比    : %u%%\n", s.batteryPercent);
        printf("  电池存在      : %s\n", s.hasBattery ? "是" : "否");

        if (s.hasBattery) {
            printf("  电池电压      : %u mV (%.2f V)\n", s.voltageMv, s.voltageMv / 1000.0);
            printf("  充电电流      : %d mA\n", s.chargeRateMa);
            printf("  放电电流      : %d mA\n", s.dischargeRateMa);
            printf("  剩余容量      : %u mWh\n", s.remainingMwh);
            printf("  设计容量      : %u mWh\n", s.designedMwh);
            printf("  满电容量      : %u mWh\n", s.fullChargeMwh);
            printf("  电池功率      : %.3f W  (%d mW)\n",
                   s.BatteryWatts(), s.batteryPowerMw);
        }
        printf("  整机功率      : %.3f W  (%d mW)%s\n",
               s.SystemWatts(), s.systemPowerMw,
               s.systemPowerMw == 0 ? "  <- 本机固件未实现" : "");

        if (i < 2) Sleep(1500);
    }

    printf("\n=== 完成 ===\n");
    return 0;
}
