// ============================================================================
//  PowerInfo.cpp — 电池 / 电源 / 功率采集实现
// ============================================================================
#include "PowerInfo.h"

#include <objbase.h>
#include <wbemidl.h>
#include <comdef.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace gcc {

namespace {

// ---------------------------------------------------------------------------
//  用 WMI 读取 root\WMI : BatteryStatus
// ---------------------------------------------------------------------------
bool ReadBattery(PowerSnapshot& s) {
    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* enumerator = nullptr;
    IWbemClassObject* obj = nullptr;
    bool ok = false;

    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (FAILED(hr)) return false;

    do {
        BSTR ns = SysAllocString(L"ROOT\\WMI");
        hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr,
                                   nullptr, &services);
        SysFreeString(ns);
        if (FAILED(hr)) break;

        CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                          RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                          nullptr, EOAC_NONE);
        {
            BSTR wql = SysAllocString(L"WQL");
            BSTR q   = SysAllocString(L"SELECT * FROM BatteryStatus");
            hr = services->ExecQuery(wql, q,
                                     WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                     nullptr, &enumerator);
            SysFreeString(wql);
            SysFreeString(q);
        }
        if (FAILED(hr) || !enumerator) break;
        {
            ULONG n = 0;
            hr = enumerator->Next(WBEM_INFINITE, 1, &obj, &n);
            if (FAILED(hr) || n == 0 || !obj) break;
        }

        // 辅助：读一个整数字段（兼容多种 VARIANT 类型）
        auto readNum = [&](const wchar_t* name, long& out) -> bool {
            VARIANT v;
            VariantInit(&v);
            BSTR b = SysAllocString(name);
            HRESULT h = obj->Get(b, 0, &v, nullptr, nullptr);
            SysFreeString(b);
            if (FAILED(h)) return false;
            bool got = false;
            if (v.vt == VT_I4)       { out = v.lVal;  got = true; }
            else if (v.vt == VT_UI4) { out = static_cast<long>(v.ulVal); got = true; }
            else if (v.vt == VT_INT) { out = v.intVal; got = true; }
            else if (v.vt == VT_UINT){ out = static_cast<long>(v.uintVal); got = true; }
            else if (v.vt == VT_I2)  { out = v.iVal;  got = true; }
            else if (v.vt == VT_UI2) { out = v.uiVal; got = true; }
            else if (v.vt == VT_I1)  { out = v.cVal;  got = true; }
            else if (v.vt == VT_UI1) { out = v.bVal;  got = true; }
            else if (v.vt == VT_BSTR && v.bstrVal) { out = _wtoi(v.bstrVal); got = true; }
            VariantClear(&v);
            return got;
        };

        long tmp = 0;
        bool any = false;
        if (readNum(L"Voltage", tmp))           { s.voltageMv = static_cast<uint32_t>(tmp); any = true; }
        if (readNum(L"ChargeRate", tmp))        { s.chargeRateMa = static_cast<int32_t>(tmp); any = true; }
        if (readNum(L"DischargeRate", tmp))     { s.dischargeRateMa = static_cast<int32_t>(tmp); any = true; }
        if (readNum(L"RemainingCapacity", tmp)) { s.remainingMwh = static_cast<uint32_t>(tmp); any = true; }

        {
            VARIANT v;
            VariantInit(&v);
            BSTR b = SysAllocString(L"PowerOnline");
            if (SUCCEEDED(obj->Get(b, 0, &v, nullptr, nullptr)) && v.vt == VT_BOOL)
                s.acOnline = (v.boolVal != VARIANT_FALSE);
            SysFreeString(b);
            VariantClear(&v);
        }

        ok = any;
    } while (false);

    if (obj)        obj->Release();
    if (enumerator) enumerator->Release();
    if (services)   services->Release();
    if (locator)    locator->Release();
    return ok;
}

// ---------------------------------------------------------------------------
//  电池静态数据：
//    BatteryStaticData          -> DesignedCapacity  （设计容量）
//    BatteryFullChargedCapacity -> FullChargedCapacity（当前满电容量）
//  注意：这两个字段分属不同 WMI 类，必须分别查询。
// ---------------------------------------------------------------------------
void ReadBatteryStatic(PowerSnapshot& s) {
    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (FAILED(hr)) return;

    BSTR ns = SysAllocString(L"ROOT\\WMI");
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr,
                               nullptr, &services);
    SysFreeString(ns);
    if (FAILED(hr) || !services) {
        if (locator) locator->Release();
        return;
    }
    CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr, EOAC_NONE);

    // 辅助：从指定类读取字段
    auto queryField = [&](const wchar_t* cls, const wchar_t* field, uint32_t& out) {
        IEnumWbemClassObject* en = nullptr;
        IWbemClassObject* obj = nullptr;
        BSTR wql = SysAllocString(L"WQL");
        BSTR q   = SysAllocString(cls);
        HRESULT h = services->ExecQuery(wql, q,
                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                        nullptr, &en);
        SysFreeString(wql);
        SysFreeString(q);
        if (SUCCEEDED(h) && en) {
            ULONG n = 0;
            if (SUCCEEDED(en->Next(WBEM_INFINITE, 1, &obj, &n)) && n > 0 && obj) {
                VARIANT v;
                VariantInit(&v);
                BSTR b = SysAllocString(field);
                if (SUCCEEDED(obj->Get(b, 0, &v, nullptr, nullptr))) {
                    if (v.vt == VT_I4)       out = static_cast<uint32_t>(v.lVal);
                    else if (v.vt == VT_UI4) out = v.ulVal;
                    else if (v.vt == VT_I2)  out = static_cast<uint32_t>(v.iVal);
                }
                SysFreeString(b);
                VariantClear(&v);
            }
        }
        if (obj) obj->Release();
        if (en)  en->Release();
    };

    queryField(L"SELECT * FROM BatteryStaticData",
               L"DesignedCapacity", s.designedMwh);
    queryField(L"SELECT * FROM BatteryFullChargedCapacity",
               L"FullChargedCapacity", s.fullChargeMwh);

    if (services) services->Release();
    if (locator)  locator->Release();
}

// ---------------------------------------------------------------------------
//  整机功率计数器（固件支持时为非 0）
// ---------------------------------------------------------------------------
void ReadSystemPower(PowerSnapshot& s) {
    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* en = nullptr;
    IWbemClassObject* obj = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (FAILED(hr)) return;

    BSTR ns = SysAllocString(L"ROOT\\CIMV2");
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr,
                               nullptr, &services);
    SysFreeString(ns);
    if (FAILED(hr)) goto cleanup;

    CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr, EOAC_NONE);
    {
        BSTR wql = SysAllocString(L"WQL");
        BSTR q = SysAllocString(
            L"SELECT Power FROM Win32_PerfFormattedData_PowerMeterCounter_PowerMeter "
            L"WHERE Name='_Total'");
        hr = services->ExecQuery(wql, q,
                                 WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                 nullptr, &en);
        SysFreeString(wql);
        SysFreeString(q);
    }
    if (FAILED(hr) || !en) goto cleanup;

    {
        ULONG n = 0;
        if (FAILED(en->Next(WBEM_INFINITE, 1, &obj, &n)) || n == 0 || !obj)
            goto cleanup;
    }
    {
        VARIANT v;
        VariantInit(&v);
        BSTR b = SysAllocString(L"Power");
        if (SUCCEEDED(obj->Get(b, 0, &v, nullptr, nullptr))) {
            if (v.vt == VT_UI4)      s.systemPowerMw = static_cast<int32_t>(v.ulVal);
            else if (v.vt == VT_I4)  s.systemPowerMw = v.lVal;
        }
        SysFreeString(b);
        VariantClear(&v);
    }

cleanup:
    if (obj)      obj->Release();
    if (en)       en->Release();
    if (services) services->Release();
    if (locator)  locator->Release();
}

} // namespace

// ---------------------------------------------------------------------------
//  主采集函数
// ---------------------------------------------------------------------------
PowerSnapshot CollectPower() {
    PowerSnapshot s;

    // 初始化 COM（幂等；若外部已初始化，沿用）
    bool needUninit = false;
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hrCom)) {
        needUninit = true;
        CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                             RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                             nullptr, EOAC_NONE, nullptr);
    }
    // 注意：若返回 RPC_E_CHANGED_MODE 或 S_FALSE，仍可使用现有单元

    // 1) 系统级电源状态（始终可用）
    SYSTEM_POWER_STATUS sps = {0};
    if (GetSystemPowerStatus(&sps)) {
        s.acLineStatus   = sps.ACLineStatus;      // 0=离线 1=在线 255=未知
        s.acOnline       = (sps.ACLineStatus == 1);
        s.batteryPercent = (sps.BatteryLifePercent == 255) ? 0 : sps.BatteryLifePercent;
    }

    // 2) WMI 电池详情
    s.hasBattery = ReadBattery(s);
    if (s.hasBattery) {
        ReadBatteryStatic(s);
        // 功率 = 电压(mV) x 电流(mA) / 1000 = mW
        int32_t netMa = s.chargeRateMa - s.dischargeRateMa;
        s.batteryPowerMw = static_cast<int32_t>(
            (static_cast<int64_t>(s.voltageMv) * netMa) / 1000);
    }

    // 3) 整机功率（可选）
    ReadSystemPower(s);

    if (needUninit) CoUninitialize();
    return s;
}

} // namespace gcc
