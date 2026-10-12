// ============================================================================
//  Sensors.cpp — 轻量系统监控实现（PDH）
// ============================================================================
#include "Sensors.h"
#include <vector>

namespace gcc {

bool CPdhSensors::Open(std::string& err) {
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
        err = "PdhOpenQuery 失败";
        return false;
    }

    // CPU 占用率
    if (PdhAddEnglishCounterW(query_, L"\\Processor Information(_Total)\\% Processor Time",
                              0, &hCpu_) != ERROR_SUCCESS) {
        // 回退到备用计数器名
        if (PdhAddCounterW(query_, L"\\Processor Information(_Total)\\% Processor Time",
                           0, &hCpu_) != ERROR_SUCCESS) {
            err = "添加 CPU 计数器失败";
            Close();
            return false;
        }
    }

    // 内存占用率
    PdhAddEnglishCounterW(query_, L"\\Memory\\% Committed Bytes In Use", 0, &hMem_);

    // CPU 性能百分比（用于估算频率）
    PdhAddEnglishCounterW(query_,
        L"\\Processor Information(_Total)\\% Processor Performance", 0, &hCpuPerf_);

    // GPU 3D 引擎占用率：需要展开通配符
    AddGpuInstances(query_);

    // PDH 的速率型计数器（CPU 占用率）需要两次采样才能得到有效值。
    // 这里先采一次并等待一小段，避免首次 Sample 返回 100%。
    PdhCollectQueryData(query_);
    Sleep(300);
    PdhCollectQueryData(query_);
    firstDone_ = true;
    return true;
}

bool CPdhSensors::AddGpuInstances(PDH_HQUERY q) {
    // GPU Engine 实例非常多（实测本机 500+，每个 PID×引擎类型一个）。
    //
    // 注意：PdhExpandWildCardPathW 的 in/out 尺寸参数单位是「字符数」，
    //       而首次调用返回的 need 也是字符数语义。因此缓冲区要按 need 个
    //       wchar_t 分配（早期版本误按字节换算，导致缓冲区不足而崩溃）。
    DWORD need = 0;
    PDH_STATUS st = PdhExpandWildCardPathW(
        nullptr, L"\\GPU Engine(*)\\Utilization Percentage", nullptr, &need, 0);
    if (need == 0) return false;

    std::vector<wchar_t> buf((size_t)need + 128, L'\0');
    DWORD capChars = (DWORD)buf.size();
    st = PdhExpandWildCardPathW(nullptr, L"\\GPU Engine(*)\\Utilization Percentage",
                                buf.data(), &capChars, 0);
    if (st != ERROR_SUCCESS) return false;

    // 只添加 3D 引擎（engtype_3D），与原程序一致
    const wchar_t* p   = buf.data();
    const wchar_t* end = buf.data() + buf.size();
    while (p < end && *p && gpuCount_ < kMaxGpuCounters) {
        size_t len = wcsnlen(p, (size_t)(end - p));
        if (len == 0) break;
        std::wstring path(p, len);
        if (path.find(L"engtype_3D") != std::wstring::npos) {
            PDH_HCOUNTER h = nullptr;
            if (PdhAddCounterW(q, path.c_str(), 0, &h) == ERROR_SUCCESS) {
                gpuList_[gpuCount_++].h = h;
            }
        }
        p += len + 1;
    }
    return gpuCount_ > 0;
}

void CPdhSensors::Close() {
    if (query_) {
        PdhCloseQuery(query_);
        query_ = nullptr;
    }
    hCpu_ = hMem_ = hGpu_ = hCpuPerf_ = nullptr;
    gpuCount_ = 0;
    firstDone_ = false;
}

bool CPdhSensors::Sample(SysSensors& out) {
    if (!query_) return false;

    // 保证与上次采集至少间隔 220ms，否则速率型计数器（CPU 占用率）会失真。
    // 首次采样时 lastTick_ 为 0，此时直接用 Open() 已做过的预热数据。
    ULONGLONG now = GetTickCount64();
    if (lastTick_ != 0 && now - lastTick_ < 220) {
        Sleep((DWORD)(220 - (now - lastTick_)));
        now = GetTickCount64();
    }

    if (lastTick_ != 0 || !firstDone_) {
        // 非首次：重新采集
        if (PdhCollectQueryData(query_) != ERROR_SUCCESS) return false;
    }
    lastTick_ = now;

    PDH_FMT_COUNTERVALUE v;

    // CPU 占用率
    if (hCpu_ && PdhGetFormattedCounterValue(hCpu_, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS
        && v.CStatus == PDH_CSTATUS_VALID_DATA) {
        out.cpuUsagePct = v.doubleValue;
        if (out.cpuUsagePct < 0) out.cpuUsagePct = 0;
        if (out.cpuUsagePct > 100) out.cpuUsagePct = 100;
    }

    // 内存占用率
    if (hMem_ && PdhGetFormattedCounterValue(hMem_, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS
        && v.CStatus == PDH_CSTATUS_VALID_DATA) {
        out.memUsagePct = v.doubleValue;
    }

    // CPU 性能百分比
    if (hCpuPerf_ && PdhGetFormattedCounterValue(hCpuPerf_, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS
        && v.CStatus == PDH_CSTATUS_VALID_DATA) {
        out.cpuPerfPct = v.doubleValue;
    }

    // GPU：累加所有 3D 引擎实例
    double gpuSum = 0.0;
    bool gpuAny = false;
    for (int i = 0; i < gpuCount_; ++i) {
        if (PdhGetFormattedCounterValue(gpuList_[i].h, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS
            && v.CStatus == PDH_CSTATUS_VALID_DATA) {
            gpuSum += v.doubleValue;
            gpuAny = true;
        }
    }
    if (gpuAny) {
        out.gpuUsagePct = gpuSum;
        if (out.gpuUsagePct > 100) out.gpuUsagePct = 100;
    }

    // 内存容量
    MEMORYSTATUSEX ms = { sizeof(ms) };
    if (GlobalMemoryStatusEx(&ms)) {
        out.memTotalBytes = ms.ullTotalPhys;
        out.memAvailBytes = ms.ullAvailPhys;
        out.memUsedBytes  = ms.ullTotalPhys - ms.ullAvailPhys;
    }

    // CPU 基准频率
    if (out.cpuBaseMhz <= 0.0) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                0, KEY_READ, &k) == ERROR_SUCCESS) {
            DWORD mhz = 0, sz = sizeof(mhz), type = 0;
            if (RegQueryValueExW(k, L"~MHz", nullptr, &type, (LPBYTE)&mhz, &sz) == ERROR_SUCCESS)
                out.cpuBaseMhz = (double)mhz;
            RegCloseKey(k);
        }
        if (out.cpuBaseMhz <= 0.0) out.cpuBaseMhz = 2000.0;
    }

    // 估算当前频率
    if (out.cpuPerfPct >= 0.0 && out.cpuBaseMhz > 0.0)
        out.cpuSpeedMhz = out.cpuBaseMhz * out.cpuPerfPct / 100.0;

    return true;
}

} // namespace gcc
