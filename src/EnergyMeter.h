// ============================================================================
//  EnergyMeter.h — CPU 功耗测量（AMD/Intel RAPL 硬件能量计数器）
//
//  数据源：PDH \Energy Meter(rapl_package0_pkg)\Energy  (纳焦 nJ)
//          PDH \Energy Meter(rapl_package0_pkg)\Time    (毫秒 ms)
//
//  换算：Power(W) = ΔEnergy(nJ) / ΔTime(ms) × 1e-6
//        因为 1 nJ/ms = 1e-9 J / 1e-3 s = 1e-6 W
//
//  RAPL 域（实测存在于本机 Ryzen 9 8940HX）：
//    rapl_package0_pkg        — CPU 封装总功耗（含核心+核显）
//    rapl_package0_coreN_core — 各核心功耗
//
//  另提供整机功率估算（电池放电时最准；接电源时为估算值）。
// ============================================================================
#pragma once

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "pdh.lib")

namespace gcc {

struct PowerReading {
    double cpuPackageW  = -1.0;   // CPU 封装功耗 W（RAPL）
    double cpuCoresW    = -1.0;   // CPU 核心合计 W（可选）
    bool   raplValid    = false;  // RAPL 数据是否有效

    // 整机/DC 输入功率估算
    double systemEstimateW = -1.0;   // 估算整机功率 W
    bool   estimateIsReal  = false;  // true = 来自电池实测（放电时）
};

// ---------------------------------------------------------------------------
class CEnergyMeter {
public:
    CEnergyMeter() = default;
    ~CEnergyMeter() { Close(); }

    bool Open(std::string& err);
    void Close();
    bool IsOpen() const { return queryPkg_ != nullptr; }

    // 采集 CPU 功耗。首次调用需 300ms 以上间隔才有意义。
    bool SampleCpu(PowerReading& out);

private:
    PDH_HQUERY   queryPkg_ = nullptr;
    PDH_HCOUNTER hEnergy_  = nullptr;
    PDH_HCOUNTER hTime_    = nullptr;

    ULONGLONG lastTick_ = 0;
    bool      first_    = true;
    long long ePrev_    = 0;
    long long tPrev_    = 0;
};

} // namespace gcc
