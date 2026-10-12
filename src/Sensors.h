// ============================================================================
//  Sensors.h — 轻量系统监控（PDH 性能计数器，零第三方依赖）
//
//  CPU 占用率  : \Processor Information(_Total)\% Processor Time
//  内存占用率  : \Memory\% Committed Bytes In Use
//  内存容量    : GlobalMemoryStatusEx
//  GPU 占用率  : \GPU Engine(*engtype_3D)\Utilization Percentage  (需枚举实例)
//  CPU 频率    : \Processor Information(_Total)\% Processor Performance × 基准频率
//
//  全部走 Windows 内置 PDH（pdh.dll），不引入 LibreHardwareMonitor 等库。
// ============================================================================
#pragma once

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdint>
#include <string>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "advapi32.lib")

namespace gcc {

struct SysSensors {
    double  cpuUsagePct   = -1.0;   // CPU 占用率 %
    double  memUsagePct   = -1.0;   // 内存占用率 %
    double  gpuUsagePct   = -1.0;   // GPU 3D 占用率 %

    uint64_t memTotalBytes = 0;
    uint64_t memAvailBytes = 0;
    uint64_t memUsedBytes  = 0;

    double  cpuBaseMhz    = 0.0;    // CPU 基准频率
    double  cpuPerfPct    = -1.0;   // % Processor Performance
    double  cpuSpeedMhz   = -1.0;   // 估算当前频率
};

// ---------------------------------------------------------------------------
//  CPdhSensors — 封装 PDH 采集
// ---------------------------------------------------------------------------
class CPdhSensors {
public:
    CPdhSensors() = default;
    ~CPdhSensors() { Close(); }

    bool Open(std::string& err);
    void Close();
    bool IsOpen() const { return query_ != nullptr; }

    // 采集一次（首次调用后需间隔至少 ~500ms 才有有效值）
    bool Sample(SysSensors& out);

private:
    PDH_HQUERY   query_     = nullptr;
    PDH_HCOUNTER hCpu_      = nullptr;
    PDH_HCOUNTER hMem_      = nullptr;
    PDH_HCOUNTER hGpu_      = nullptr;
    PDH_HCOUNTER hCpuPerf_  = nullptr;
    bool         firstDone_ = false;
    ULONGLONG    lastTick_  = 0;    // 上次采集时刻（保证最小间隔）

    // GPU 计数器需要显式添加每个实例
    // 注意：GPU Engine 实例可能多达数百个（每个 PID × 引擎类型），
    //       只用 3D 引擎，仍可能超过 64 个，故取 256 上限。
    static constexpr int kMaxGpuCounters = 256;
    struct GpuCounter { PDH_HCOUNTER h; };
    GpuCounter   gpuList_[kMaxGpuCounters] = {};
    int          gpuCount_    = 0;

    bool AddGpuInstances(PDH_HQUERY q);
};

} // namespace gcc
