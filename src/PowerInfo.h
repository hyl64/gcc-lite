// ============================================================================
//  PowerInfo.h — 电池 / 电源 / 功率采集
//  数据源：
//    电池电压/电流  root\WMI : BatteryStatus  (Voltage mV, ChargeRate/DischargeRate mA)
//    交流/电池状态  kernel32!GetSystemPowerStatus
//    整机功率       Win32_PerfFormattedData_PowerMeterCounter_PowerMeter（部分固件为 0）
// ============================================================================
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

namespace gcc {

struct PowerSnapshot {
    bool     hasBattery      = false;

    // 电池
    uint32_t voltageMv       = 0;   // 电压 mV
    int32_t  chargeRateMa    = 0;   // 充电电流 mA（正=充）
    int32_t  dischargeRateMa = 0;   // 放电电流 mA（正=放）
    uint32_t remainingMwh    = 0;   // 剩余容量 mWh
    uint32_t fullChargeMwh   = 0;   // 满电容量 mWh
    uint32_t designedMwh     = 0;   // 设计容量 mWh

    // 系统
    uint8_t  batteryPercent  = 0;   // 0-100
    bool     acOnline        = false;
    uint8_t  acLineStatus    = 0;   // GetSystemPowerStatus 原始值

    // 派生功率（mW）
    int32_t  batteryPowerMw  = 0;   // 正=充电, 负=放电
    int32_t  systemPowerMw   = 0;   // 整机功率（可能为 0）

    // 格式化输出
    double BatteryWatts() const { return batteryPowerMw / 1000.0; }
    double SystemWatts()  const { return systemPowerMw  / 1000.0; }
};

// 采集一次快照。battery 部分失败时 hasBattery=false，其余字段仍有效。
PowerSnapshot CollectPower();

} // namespace gcc
