// ============================================================================
//  diag5.cpp — 穷举路径转义组合，找到 provider 接受的那一个
// ============================================================================
#include <windows.h>
#include <objbase.h>
#include <wbemidl.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static bool PutInData(IWbemClassObject* inP, const unsigned char* b32) {
    VARIANT v; VariantInit(&v);
    v.vt = VT_ARRAY | VT_UI1;
    SAFEARRAYBOUND b; b.lLbound=0; b.cElements=32;
    v.parray = SafeArrayCreate(VT_UI1,1,&b);
    if (!v.parray) return false;
    void* p=nullptr; SafeArrayAccessData(v.parray,&p); memcpy(p,b32,32);
    SafeArrayUnaccessData(v.parray);
    BSTR n=SysAllocString(L"InData");
    HRESULT hr=inP->Put(n,0,&v,0);
    SysFreeString(n); VariantClear(&v);
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
    { BSTR cp=SysAllocString(L"MICommonInterface"); svc->GetObject(cp,0,0,&cls,0); SysFreeString(cp); }
    IWbemClassObject* inSig=nullptr;
    if (cls) { BSTR mn=SysAllocString(L"MiInterface"); cls->GetMethod(mn,0,&inSig,nullptr); SysFreeString(mn); }
    if (!inSig) { printf("init failed\n"); return 1; }

    // 各种路径写法
    const wchar_t* paths[] = {
        // 单引号 + 单反斜杠
        L"MICommonInterface.InstanceName='ACPI\\PNP0C14\\MIFS_0'",
        // 单引号 + 双反斜杠
        L"MICommonInterface.InstanceName='ACPI\\\\PNP0C14\\\\MIFS_0'",
        // 双引号 + 单反斜杠
        L"MICommonInterface.InstanceName=\"ACPI\\PNP0C14\\MIFS_0\"",
        // 双引号 + 四反斜杠
        L"MICommonInterface.InstanceName=\"ACPI\\\\\\\\PNP0C14\\\\\\\\MIFS_0\"",
        // 单引号 + 四反斜杠
        L"MICommonInterface.InstanceName='ACPI\\\\\\\\PNP0C14\\\\\\\\MIFS_0'",
        // 单引号 + 三反斜杠
        L"MICommonInterface.InstanceName='ACPI\\\\\\PNP0C14\\\\\\MIFS_0'",
    };
    const int NPATH = sizeof(paths)/sizeof(paths[0]);

    for (int pi = 0; pi < NPATH; ++pi) {
        IWbemClassObject* inP=nullptr;
        inSig->SpawnInstance(0,&inP);
        unsigned char buf[32]={0}; buf[2]=250; buf[3]=13;
        PutInData(inP, buf);

        IWbemClassObject* outP=nullptr;
        BSTR mn=SysAllocString(L"MiInterface");
        HRESULT hr=svc->ExecMethod(const_cast<BSTR>(paths[pi]),mn,0,nullptr,inP,&outP,nullptr);
        SysFreeString(mn);

        printf("[%d] hr=0x%08lX  %ls\n", pi, (unsigned long)hr, paths[pi]);
        if (SUCCEEDED(hr) && outP) {
            VARIANT vv; VariantInit(&vv);
            BSTR on=SysAllocString(L"OutData");
            if (SUCCEEDED(outP->Get(on,0,&vv,0,0)) && (vv.vt==(VT_ARRAY|VT_UI1)) && vv.parray) {
                unsigned char* d=nullptr; SafeArrayAccessData(vv.parray,(void**)&d);
                ULONG c=vv.parray->rgsabound[0].cElements;
                printf("    *** OutData(%lu) = ", c);
                for (ULONG k=0;k<c && k<12;k++) printf("%u ", d[k]);
                printf("   [4]=%u ***\n", d[4]);
                SafeArrayUnaccessData(vv.parray);
            }
            VariantClear(&vv); SysFreeString(on); outP->Release();
        }
        inP->Release();
    }

    inSig->Release(); if(cls) cls->Release(); svc->Release(); loc->Release();
    CoUninitialize();
    return 0;
}
