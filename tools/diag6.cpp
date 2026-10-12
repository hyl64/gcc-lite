// ============================================================================
//  diag6.cpp — 路径用 [2] 形式，穷举 InData 参数构造方式
// ============================================================================
#include <windows.h>
#include <objbase.h>
#include <wbemidl.h>
#include <cstdio>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

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

    BSTR path = SysAllocString(L"MICommonInterface.InstanceName=\"ACPI\\PNP0C14\\MIFS_0\"");
    unsigned char payload[32]={0}; payload[2]=250; payload[3]=13;

    auto tryVariant = [&](const char* label, auto makeVariant) {
        IWbemClassObject* inP=nullptr;
        inSig->SpawnInstance(0,&inP);
        if (!inP) { printf("%-38s spawn failed\n", label); return; }
        VARIANT v; VariantInit(&v);
        if (!makeVariant(v)) { printf("%-38s variant build failed\n", label); inP->Release(); return; }
        BSTR n=SysAllocString(L"InData");
        HRESULT hp=inP->Put(n,0,&v,0);
        SysFreeString(n); VariantClear(&v);

        IWbemClassObject* outP=nullptr;
        BSTR mn=SysAllocString(L"MiInterface");
        HRESULT hr=svc->ExecMethod(path,mn,0,nullptr,inP,&outP,nullptr);
        SysFreeString(mn);
        printf("%-38s put=0x%08lX exec=0x%08lX", label,(unsigned long)hp,(unsigned long)hr);
        if (SUCCEEDED(hr)&&outP) {
            VARIANT vv; VariantInit(&vv);
            BSTR on=SysAllocString(L"OutData");
            if (SUCCEEDED(outP->Get(on,0,&vv,0,0)) && (vv.vt==(VT_ARRAY|VT_UI1)) && vv.parray) {
                unsigned char* d=nullptr; SafeArrayAccessData(vv.parray,(void**)&d);
                ULONG c=vv.parray->rgsabound[0].cElements;
                printf("  *** OK OutData(%lu)[4]=%u ***", c, d[4]);
                SafeArrayUnaccessData(vv.parray);
            }
            VariantClear(&vv); SysFreeString(on); outP->Release();
        }
        printf("\n");
        inP->Release();
    };

    // 1. VT_ARRAY|VT_UI1 (SafeArray of byte)
    tryVariant("1. VT_ARRAY|VT_UI1 byte[32]", [&](VARIANT& v){
        v.vt=VT_ARRAY|VT_UI1;
        SAFEARRAYBOUND b; b.lLbound=0; b.cElements=32;
        v.parray=SafeArrayCreate(VT_UI1,1,&b);
        if(!v.parray) return false;
        void* p=nullptr; SafeArrayAccessData(v.parray,&p); memcpy(p,payload,32);
        SafeArrayUnaccessData(v.parray);
        return true;
    });

    // 2. VT_ARRAY|VT_UI1 但下界=1
    tryVariant("2. byte[32] lbound=1", [&](VARIANT& v){
        v.vt=VT_ARRAY|VT_UI1;
        SAFEARRAYBOUND b; b.lLbound=1; b.cElements=32;
        v.parray=SafeArrayCreate(VT_UI1,1,&b);
        if(!v.parray) return false;
        void* p=nullptr; SafeArrayAccessData(v.parray,&p); memcpy(p,payload,32);
        SafeArrayUnaccessData(v.parray);
        return true;
    });

    // 3. VT_ARRAY|VT_VARIANT (变体数组)
    tryVariant("3. VT_ARRAY|VT_VARIANT", [&](VARIANT& v){
        v.vt=VT_ARRAY|VT_VARIANT;
        SAFEARRAYBOUND b; b.lLbound=0; b.cElements=32;
        v.parray=SafeArrayCreate(VT_VARIANT,1,&b);
        if(!v.parray) return false;
        for (LONG i=0;i<32;i++) { VARIANT e; VariantInit(&e); e.vt=VT_UI1; e.bVal=payload[i];
                                  SafeArrayPutElement(v.parray,&i,&e); VariantClear(&e); }
        return true;
    });

    // 4. 作为 VT_UNKNOWN (IWbemClassObject)
    tryVariant("4. 32 字节 -> 30 字节数组", [&](VARIANT& v){
        v.vt=VT_ARRAY|VT_UI1;
        SAFEARRAYBOUND b; b.lLbound=0; b.cElements=30;
        v.parray=SafeArrayCreate(VT_UI1,1,&b);
        if(!v.parray) return false;
        void* p=nullptr; SafeArrayAccessData(v.parray,&p); memcpy(p,payload,30);
        SafeArrayUnaccessData(v.parray);
        return true;
    });

    SysFreeString(path);
    inSig->Release(); if(cls) cls->Release(); svc->Release(); loc->Release();
    CoUninitialize();
    return 0;
}
