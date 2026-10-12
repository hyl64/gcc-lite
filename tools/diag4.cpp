// ============================================================================
//  diag4.cpp — 精确复刻原程序的调用
//  路径格式（原程序字符串）: root\WMI  +  MICommonInterface.InstanceName='ACPI\\PNP0C14\\MIFS_0'
//  用单引号包裹 InstanceName！
// ============================================================================
#include <windows.h>
#include <objbase.h>
#include <wbemidl.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// 把 InData 组装为 VARIANT(byte[]) 并放入参数对象
static bool PutInData(IWbemClassObject* inP, const unsigned char* buf32) {
    VARIANT v; VariantInit(&v);
    v.vt = VT_ARRAY | VT_UI1;
    SAFEARRAYBOUND b; b.lLbound = 0; b.cElements = 32;
    v.parray = SafeArrayCreate(VT_UI1, 1, &b);
    if (!v.parray) return false;
    void* p = nullptr;
    SafeArrayAccessData(v.parray, &p);
    memcpy(p, buf32, 32);
    SafeArrayUnaccessData(v.parray);
    BSTR n = SysAllocString(L"InData");
    HRESULT hr = inP->Put(n, 0, &v, 0);
    SysFreeString(n);
    VariantClear(&v);
    return SUCCEEDED(hr);
}

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);

    IWbemLocator* loc=nullptr;
    CoCreateInstance(CLSID_WbemLocator,0,CLSCTX_INPROC_SERVER,IID_IWbemLocator,(void**)&loc);
    IWbemServices* svc=nullptr;
    { BSTR ns=SysAllocString(L"root\\WMI");
      loc->ConnectServer(ns,0,0,0,0,0,0,&svc); SysFreeString(ns); }
    CoSetProxyBlanket(svc,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,
        RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE);

    IWbemClassObject* cls=nullptr;
    { BSTR cp=SysAllocString(L"MICommonInterface");
      svc->GetObject(cp,0,0,&cls,0); SysFreeString(cp); }
    if (!cls) { printf("no class\n"); return 1; }

    IWbemClassObject* inSig=nullptr;
    { BSTR mn=SysAllocString(L"MiInterface");
      cls->GetMethod(mn,0,&inSig,nullptr); SysFreeString(mn); }
    if (!inSig) { printf("no method\n"); return 1; }

    // 原程序路径（单引号）
    const wchar_t* paths[] = {
        L"MICommonInterface.InstanceName='ACPI\\\\PNP0C14\\\\MIFS_0'",
        L"MICommonInterface.InstanceName=\"ACPI\\\\PNP0C14\\\\MIFS_0\"",
    };

    for (int pi = 0; pi < 2; ++pi) {
        printf("=== 路径 %d: %ls ===\n", pi+1, paths[pi]);
        for (int mid : {8, 13, 22, 23}) {
            IWbemClassObject* inP=nullptr;
            inSig->SpawnInstance(0,&inP);
            if (!inP) continue;

            unsigned char buf[32] = {0};
            buf[2] = 250;
            buf[3] = (unsigned char)mid;
            if (!PutInData(inP, buf)) { inP->Release(); continue; }

            IWbemClassObject* outP=nullptr;
            BSTR mn = SysAllocString(L"MiInterface");
            HRESULT hr = svc->ExecMethod(const_cast<BSTR>(paths[pi]), mn,
                                         0, nullptr, inP, &outP, nullptr);
            SysFreeString(mn);
            printf("  id=%-3d ExecMethod hr=0x%08lX", mid, (unsigned long)hr);
            if (SUCCEEDED(hr) && outP) {
                VARIANT vv; VariantInit(&vv);
                BSTR on=SysAllocString(L"OutData");
                if (SUCCEEDED(outP->Get(on,0,&vv,0,0)) &&
                    (vv.vt==(VT_ARRAY|VT_UI1)) && vv.parray) {
                    unsigned char* d=nullptr;
                    SafeArrayAccessData(vv.parray,(void**)&d);
                    ULONG c=vv.parray->rgsabound[0].cElements;
                    printf("  -> OutData(%lu)=", c);
                    for (ULONG k=0;k<c && k<10;k++) printf("%u ", d[k]);
                    printf("  [4]=%u", d[4]);
                    SafeArrayUnaccessData(vv.parray);
                }
                VariantClear(&vv); SysFreeString(on);
                outP->Release();
            }
            printf("\n");
            inP->Release();
        }
        printf("\n");
    }

    inSig->Release(); cls->Release(); svc->Release(); loc->Release();
    CoUninitialize();
    return 0;
}
