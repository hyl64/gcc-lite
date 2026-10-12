// ============================================================================
//  diag3.cpp — 探测 provider 行为
//  假设1: 需要每次重新查询实例（旧对象失效）
//  假设2: 存在多个实例
//  假设3: 需要用 IWbemServices::ExecMethod 但走 __RELPATH 且大小写敏感
//  假设4: 该 ACPI 方法在非 AC 状态或需要特定前置调用
// ============================================================================
#include <windows.h>
#include <objbase.h>
#include <wbemidl.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static void Hr(const char* l, HRESULT h) {
    printf("  %-40s 0x%08lX\n", l, (unsigned long)h);
}

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);

    IWbemLocator* loc=nullptr;
    CoCreateInstance(CLSID_WbemLocator,0,CLSCTX_INPROC_SERVER,IID_IWbemLocator,(void**)&loc);
    IWbemServices* svc=nullptr;
    { BSTR ns=SysAllocString(L"ROOT\\WMI");
      loc->ConnectServer(ns,0,0,0,0,0,0,&svc); SysFreeString(ns); }
    CoSetProxyBlanket(svc,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,
        RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE);

    printf("=== 假设2: 枚举所有 MICommonInterface 实例 ===\n");
    { BSTR wql=SysAllocString(L"WQL");
      BSTR q=SysAllocString(L"SELECT * FROM MICommonInterface");
      IEnumWbemClassObject* en=nullptr;
      HRESULT hr=svc->ExecQuery(wql,q,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,0,&en);
      SysFreeString(wql); SysFreeString(q);
      if (SUCCEEDED(hr)) {
          IWbemClassObject* objs[8]={0};
          ULONG got=0;
          hr=en->Next(WBEM_INFINITE,8,objs,&got);
          printf("  找到实例数: %lu\n", got);
          for (ULONG i=0;i<got;i++) {
              VARIANT v; VariantInit(&v);
              if (SUCCEEDED(objs[i]->Get(L"InstanceName",0,&v,0,0)))
                  printf("   [%lu] %ls\n", i, v.bstrVal);
              VariantClear(&v);
              objs[i]->Release();
          }
      }
      if (en) en->Release(); }

    printf("\n=== 假设1: 每次重新查询后立即调用 ===\n");
    for (int attempt = 0; attempt < 2; ++attempt) {
        IEnumWbemClassObject* en=nullptr;
        IWbemClassObject* inst=nullptr;
        IWbemClassObject* cls=nullptr;
        IWbemClassObject* inSig=nullptr;
        IWbemClassObject* inP=nullptr;
        IWbemClassObject* outP=nullptr;

        { BSTR wql=SysAllocString(L"WQL");
          BSTR q=SysAllocString(L"SELECT * FROM MICommonInterface");
          svc->ExecQuery(wql,q,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,0,&en);
          SysFreeString(wql); SysFreeString(q); }
        ULONG n=0;
        if (en) en->Next(WBEM_INFINITE,1,&inst,&n);

        { BSTR cp=SysAllocString(L"MICommonInterface");
          svc->GetObject(cp,0,0,&cls,0); SysFreeString(cp); }
        if (cls) { BSTR mn=SysAllocString(L"MiInterface");
                   cls->GetMethod(mn,0,&inSig,nullptr); SysFreeString(mn); }
        if (inSig) inSig->SpawnInstance(0,&inP);

        BSTR path=nullptr;
        if (inst) { VARIANT v; VariantInit(&v);
            if (SUCCEEDED(inst->Get(L"__PATH",0,&v,0,0))) path=SysAllocString(v.bstrVal);
            VariantClear(&v); }

        if (inP) {
            VARIANT av; VariantInit(&av);
            av.vt=VT_ARRAY|VT_UI1;
            SAFEARRAYBOUND b; b.lLbound=0; b.cElements=32;
            av.parray=SafeArrayCreate(VT_UI1,1,&b);
            void* p=nullptr; SafeArrayAccessData(av.parray,&p);
            unsigned char buf[32]={0}; buf[2]=250; buf[3]=8;
            memcpy(p,buf,32); SafeArrayUnaccessData(av.parray);
            BSTR nm=SysAllocString(L"InData");
            inP->Put(nm,0,&av,0); SysFreeString(nm); VariantClear(&av);

            BSTR mn=SysAllocString(L"MiInterface");
            printf("  尝试 %d (path=%ls):\n", attempt+1, path?path:L"(null)");
            HRESULT hr = svc->ExecMethod(path, mn, 0, nullptr, inP, &outP, nullptr);
            Hr("ExecMethod", hr);
            if (SUCCEEDED(hr) && outP) {
                printf("  *** 成功 ***\n");
                VARIANT vv; VariantInit(&vv);
                BSTR on=SysAllocString(L"OutData");
                if (SUCCEEDED(outP->Get(on,0,&vv,0,0))) {
                    if ((vv.vt==(VT_ARRAY|VT_UI1)) && vv.parray) {
                        unsigned char* d=nullptr;
                        SafeArrayAccessData(vv.parray,(void**)&d);
                        ULONG c=vv.parray->rgsabound[0].cElements;
                        printf("  OutData(%lu) = ", c);
                        for (ULONG k=0;k<c;k++) printf("%u ", d[k]);
                        printf("\n");
                        SafeArrayUnaccessData(vv.parray);
                    } else printf("  OutData vt=%u\n", vv.vt);
                }
                VariantClear(&vv); SysFreeString(on);
            }
            SysFreeString(mn);
        }
        if (path) SysFreeString(path);
        if (outP) outP->Release();
        if (inP) inP->Release();
        if (inSig) inSig->Release();
        if (cls) cls->Release();
        if (inst) inst->Release();
        if (en) en->Release();
        Sleep(200);
    }

    printf("\n=== 假设3: 用 IWbemServices::ExecMethodAsync 或直接看类是否有实例绑定 ===\n");
    { // 检查类的 __CLASS 与实例是否 dynamic provider
      BSTR cp=SysAllocString(L"MICommonInterface");
      IWbemClassObject* c=nullptr;
      svc->GetObject(cp,0,0,&c,0); SysFreeString(cp);
      if (c) {
          const wchar_t* names[] = {L"__CLASS", L"__DERIVATION", L"__DYNASTY", L"__GENUS", L"__SUPERCLASS"};
          for (auto n : names) {
              VARIANT v; VariantInit(&v);
              if (SUCCEEDED(c->Get(n,0,&v,0,0))) {
                  if (v.vt==VT_BSTR) printf("  %ls = %ls\n", n, v.bstrVal);
                  else printf("  %ls = (vt=%u)\n", n, v.vt);
              }
              VariantClear(&v);
          }
          c->Release();
      } }

    svc->Release(); loc->Release(); CoUninitialize();
    return 0;
}
