// ============================================================================
//  MiBridge.cpp — ACPI-WMI 硬件控制桥实现
// ============================================================================
#include "MiBridge.h"

#pragma comment(lib, "wbemuuid.lib")

namespace gcc {

// 安全释放 COM 对象
template <class T> static void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

MiBridge::~MiBridge() { Close(); }

void MiBridge::Close() {
    SafeRelease(inSig_);
    SafeRelease(instance_);
    SafeRelease(services_);
    SafeRelease(locator_);
    instancePath_.clear();
    if (comInited_) {
        CoUninitialize();
        comInited_ = false;
    }
}

// ----------------------------------------------------------------------------
//  Open: 初始化 COM，连接 root\WMI，定位 MICommonInterface 实例并取方法签名
// ----------------------------------------------------------------------------
bool MiBridge::Open(std::string& err) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        comInited_ = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        // 已以其它模式初始化，继续使用
    } else {
        err = "CoInitializeEx 失败";
        return false;
    }

    // WMI 需要模拟安全级别
    hr = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE, nullptr);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        err = "CoInitializeSecurity 失败";
        return false;
    }

    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, reinterpret_cast<void**>(&locator_));
    if (FAILED(hr)) {
        err = "创建 WbemLocator 失败";
        return false;
    }

    BSTR ns = SysAllocString(L"ROOT\\WMI");
    hr = locator_->ConnectServer(ns, nullptr, nullptr, nullptr, 0,
                                 nullptr, nullptr, &services_);
    SysFreeString(ns);
    if (FAILED(hr)) {
        err = "连接 root\\WMI 失败（需管理员权限）";
        return false;
    }

    hr = CoSetProxyBlanket(services_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                           nullptr, RPC_C_AUTHN_LEVEL_CALL,
                           RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        err = "设置代理安全级别失败";
        return false;
    }

    // 枚举 MICommonInterface 实例
    BSTR wql  = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT * FROM MICommonInterface");
    IEnumWbemClassObject* enumerator = nullptr;
    hr = services_->ExecQuery(wql, query,
                              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                              nullptr, &enumerator);
    SysFreeString(wql);
    SysFreeString(query);
    if (FAILED(hr)) {
        err = "查询 MICommonInterface 失败";
        return false;
    }

    ULONG returned = 0;
    hr = enumerator->Next(WBEM_INFINITE, 1, &instance_, &returned);
    enumerator->Release();
    if (FAILED(hr) || returned == 0 || instance_ == nullptr) {
        err = "未找到 MICommonInterface 实例";
        return false;
    }

    // 取类定义以获得 MiInterface 的输入签名
    BSTR clsPath = SysAllocString(L"MICommonInterface");
    IWbemClassObject* cls = nullptr;
    hr = services_->GetObject(clsPath, 0, nullptr, &cls, nullptr);
    SysFreeString(clsPath);
    if (FAILED(hr) || !cls) {
        err = "获取 MICommonInterface 类定义失败";
        return false;
    }

    BSTR mName = SysAllocString(L"MiInterface");
    hr = cls->GetMethod(mName, 0, &inSig_, nullptr);
    SysFreeString(mName);
    cls->Release();
    if (FAILED(hr) || !inSig_) {
        err = "获取 MiInterface 方法签名失败";
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
//  Call: 组装 InData 并执行 MiInterface，取回 OutData
// ----------------------------------------------------------------------------
bool MiBridge::Call(uint8_t method, uint8_t type, uint8_t value,
                    uint8_t* out32, std::string& err) {
    if (!instance_ || !inSig_) { err = "接口未初始化"; return false; }

    // --- 组装 32 字节 InData ---
    // 偏移来自 _MakeMethodPrams 的 IL 还原：
    //   Array.Clear(buf); buf[1] = methodType; buf[3] = methodName;
    uint8_t in[32] = {0};
    in[1] = type;         // 250 = Get / 251 = Set   ← 偏移 1（关键）
    in[3] = method;       // 方法号
    in[4] = value;        // 参数值（Set 时使用）

    // --- 构造输入参数对象 ---
    IWbemClassObject* inParams = nullptr;
    HRESULT hr = inSig_->SpawnInstance(0, &inParams);
    if (FAILED(hr)) { err = "SpawnInstance 失败"; return false; }

    // InData 作为 UInt8Array 放入
    VARIANT vIn;
    VariantInit(&vIn);
    vIn.vt = VT_ARRAY | VT_UI1;
    SAFEARRAYBOUND bound;
    bound.lLbound = 0;
    bound.cElements = 32;
    SAFEARRAY* sa = SafeArrayCreate(VT_UI1, 1, &bound);
    if (!sa) { inParams->Release(); err = "SafeArrayCreate 失败"; return false; }
    void* pData = nullptr;
    SafeArrayAccessData(sa, &pData);
    memcpy(pData, in, 32);
    SafeArrayUnaccessData(sa);
    vIn.parray = sa;

    BSTR inDataName = SysAllocString(L"InData");
    hr = inParams->Put(inDataName, 0, &vIn, 0);
    VariantClear(&vIn);
    SysFreeString(inDataName);
    if (FAILED(hr)) {
        inParams->Release();
        err = "设置 InData 失败";
        return false;
    }

    // OutData / Reserved 声明为 [out]，不可写入入参对象。只设置 InData。
    (void)inDataName;

    // --- 取实例对象路径 (ExecMethod 需要 bstrObjectPath) ---
    std::wstring objPath = instancePath_;
    if (objPath.empty()) {
        VARIANT vPath;
        VariantInit(&vPath);
        if (SUCCEEDED(instance_->Get(L"__PATH", 0, &vPath, nullptr, nullptr)) &&
            vPath.vt == VT_BSTR && vPath.bstrVal) {
            objPath.assign(vPath.bstrVal, SysStringLen(vPath.bstrVal));
        }
        VariantClear(&vPath);
    }
    if (objPath.empty()) {
        inParams->Release();
        err = "无法取得实例路径";
        return false;
    }

    // --- 调用 MiInterface ---
    IWbemClassObject* outParams = nullptr;
    BSTR methodName  = SysAllocString(L"MiInterface");
    BSTR objPathBstr = SysAllocStringLen(objPath.c_str(),
                                         static_cast<UINT>(objPath.size()));
    hr = services_->ExecMethod(objPathBstr, methodName, 0, nullptr,
                               inParams, &outParams, nullptr);
    SysFreeString(objPathBstr);
    SysFreeString(methodName);

    if (FAILED(hr) || !outParams) {
        if (outParams) outParams->Release();
        inParams->Release();
        char hex[64];
        sprintf_s(hex, " hr=0x%08lX", static_cast<unsigned long>(hr));
        err = "ExecMethod 失败";
        err += hex;
        return false;
    }

    bool ok = false;
    VARIANT vArr;
    VariantInit(&vArr);
    BSTR outName = SysAllocString(L"OutData");
    if (SUCCEEDED(outParams->Get(outName, 0, &vArr, nullptr, nullptr)) &&
        (vArr.vt == (VT_ARRAY | VT_UI1)) && vArr.parray) {
        void* pOut = nullptr;
        if (SUCCEEDED(SafeArrayAccessData(vArr.parray, &pOut)) && pOut) {
            ULONG n = vArr.parray->rgsabound[0].cElements;
            memcpy(out32, pOut, n < 32 ? n : 32);
            SafeArrayUnaccessData(vArr.parray);
            ok = true;
        }
    }
    VariantClear(&vArr);
    SysFreeString(outName);
    outParams->Release();
    inParams->Release();

    if (!ok) err = "未取得 OutData";
    return ok;
}

bool MiBridge::GetRaw(uint8_t method, uint8_t& value, std::string& err) {
    uint8_t out[32] = {0};
    if (!Call(method, kGet, 0, out, err)) return false;
    value = out[4];
    return true;
}

bool MiBridge::SetRaw(uint8_t method, uint8_t value, std::string& err) {
    uint8_t out[32] = {0};
    return Call(method, kSet, value, out, err);
}

} // namespace gcc
