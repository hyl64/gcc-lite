// ============================================================================
//  EnergyMeter.cpp — CPU 功耗测量实现（RAPL）
// ============================================================================
#include "EnergyMeter.h"
#include <cwctype>

namespace gcc {

bool CEnergyMeter::Open(std::string& err) {
    if (PdhOpenQueryW(nullptr, 0, &queryPkg_) != ERROR_SUCCESS) {
        err = "PdhOpenQuery 失败";
        return false;
    }

    // 先确认 RAPL 实例存在
    DWORD need = 0;
    PDH_STATUS st = PdhExpandWildCardPathW(
        nullptr, L"\\Energy Meter(*)\\Energy", nullptr, &need, 0);
    if (need == 0) {
        err = "本机无 Energy Meter（RAPL）计数器";
        Close();
        return false;
    }

    std::vector<wchar_t> buf((size_t)need + 128, L'\0');
    DWORD cap = (DWORD)buf.size();
    st = PdhExpandWildCardPathW(nullptr, L"\\Energy Meter(*)\\Energy",
                                buf.data(), &cap, 0);
    if (st != ERROR_SUCCESS) {
        err = "展开 Energy Meter 路径失败";
        Close();
        return false;
    }

    // 找 package 级实例（rapl_package0_pkg）；找不到则退而用 _total
    // 注意：PdhExpandWildCardPathW 返回的路径是小写的，必须不区分大小写匹配
    std::wstring pkgEnergy, pkgTime;
    const wchar_t* p   = buf.data();
    const wchar_t* end = buf.data() + buf.size();
    while (p < end && *p) {
        size_t len = wcsnlen(p, (size_t)(end - p));
        if (len == 0) break;
        std::wstring path(p, len);
        std::wstring low = path;
        for (auto& c : low) c = (wchar_t)towlower(c);

        // 精确匹配 "rapl_package0_pkg)\energy" —— 排除 coreN_core 等子实例
        if (low.find(L"rapl_package0_pkg)") != std::wstring::npos &&
            low.find(L"\\energy") != std::wstring::npos) {
            pkgEnergy = path;
            // 同实例下的 Time 计数器：把结尾 "energy" 换成 "Time"
            size_t pos = path.rfind(L'\\');
            if (pos != std::wstring::npos) {
                pkgTime = path.substr(0, pos + 1) + L"Time";
            }
            break;
        }
        p += len + 1;
    }
    if (pkgEnergy.empty()) {
        // 回退：尝试 _Total 实例
        p = buf.data();
        while (p < end && *p) {
            size_t len = wcsnlen(p, (size_t)(end - p));
            if (len == 0) break;
            std::wstring path(p, len);
            std::wstring low = path;
            for (auto& c : low) c = (wchar_t)towlower(c);
            if (low.find(L"(_total)") != std::wstring::npos &&
                low.find(L"\\energy") != std::wstring::npos) {
                pkgEnergy = path;
                size_t pos = path.rfind(L'\\');
                if (pos != std::wstring::npos)
                    pkgTime = path.substr(0, pos + 1) + L"Time";
                break;
            }
            p += len + 1;
        }
    }
    if (pkgEnergy.empty()) {
        err = "未找到 RAPL package 实例";
        Close();
        return false;
    }

    if (PdhAddCounterW(queryPkg_, pkgEnergy.c_str(), 0, &hEnergy_) != ERROR_SUCCESS ||
        PdhAddCounterW(queryPkg_, pkgTime.c_str(),   0, &hTime_)   != ERROR_SUCCESS) {
        err = "添加 RAPL 计数器失败";
        Close();
        return false;
    }

    // 预热：RAPL 是累计型，需要两次采样求增量
    PdhCollectQueryData(queryPkg_);
    Sleep(300);
    PdhCollectQueryData(queryPkg_);
    lastTick_ = GetTickCount64();
    first_ = false;
    return true;
}

void CEnergyMeter::Close() {
    if (queryPkg_) { PdhCloseQuery(queryPkg_); queryPkg_ = nullptr; }
    hEnergy_ = hTime_ = nullptr;
    first_ = true;
    lastTick_ = 0;
}

bool CEnergyMeter::SampleCpu(PowerReading& out) {
    if (!queryPkg_) return false;

    // 两次读数之间至少 300ms，否则增量太小、噪声大
    ULONGLONG now = GetTickCount64();
    if (now - lastTick_ < 300) {
        Sleep((DWORD)(300 - (now - lastTick_)));
    }

    if (PdhCollectQueryData(queryPkg_) != ERROR_SUCCESS) return false;

    PDH_FMT_COUNTERVALUE vE, vT;
    if (PdhGetFormattedCounterValue(hEnergy_, PDH_FMT_LARGE, nullptr, &vE) != ERROR_SUCCESS ||
        PdhGetFormattedCounterValue(hTime_,   PDH_FMT_LARGE, nullptr, &vT) != ERROR_SUCCESS) {
        return false;
    }
    if (vE.CStatus != PDH_CSTATUS_VALID_DATA || vT.CStatus != PDH_CSTATUS_VALID_DATA)
        return false;

    long long e = vE.largeValue;   // 纳焦 nJ
    long long t = vT.largeValue;   // 毫秒 ms

    if (first_) {
        ePrev_ = e; tPrev_ = t;
        first_ = false;
        lastTick_ = GetTickCount64();
        return false;              // 第一个样本只记录基线
    }

    long long dE = e - ePrev_;
    long long dT = t - tPrev_;
    ePrev_ = e; tPrev_ = t;
    lastTick_ = GetTickCount64();

    if (dT <= 0 || dE < 0) return false;   // 计数器回绕或异常

    // Power(W) = (dE nJ / dT ms) * 1e-6
    double w = ((double)dE / (double)dT) * 1e-6;

    // 合理性过滤：单 CPU 封装不可能超过 300W 或为负
    if (w < 0.0 || w > 300.0) return false;

    out.cpuPackageW = w;
    out.raplValid   = true;
    return true;
}

} // namespace gcc
