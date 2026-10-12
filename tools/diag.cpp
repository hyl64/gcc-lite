// ============================================================================
//  diag.cpp — WMI 调用逐步诊断，打印每一步 HRESULT
// ============================================================================
#include <windows.h>
#include <objbase.h>
#include <wbemidl.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static const char* HrName(HRESULT hr) {
    switch (hr) {
        case S_OK: return "S_OK";
        case WBEM_E_INVALID_PARAMETER:  return "WBEM_E_INVALID_PARAMETER";
        case WBEM_E_INVALID_METHOD:     return "WBEM_E_INVALID_METHOD";
        case WBEM_E_NOT_FOUND:          return "WBEM_E_NOT_FOUND";
        case WBEM_E_ACCESS_DENIED:      return "WBEM_E_ACCESS_DENIED";
        case WBEM_E_FAILED:             return "WBEM_E_FAILED";
        case WBEM_E_INVALID_CLASS:      return "WBEM_E_INVALID_CLASS";
        case E_ACCESSDENIED:            return "E_ACCESSDENIED";
        default: return "";
    }
}
#define CHECK(expr, label) do { \
    HRESULT _h = (expr); \
    printf("%-34s hr=0x%08lX %s\n", label, (unsigned long)_h, HrName(_h)); \
    if (FAILED(_h)) { printf("  -> 失败，终止\n"); goto done; } \
} while (0)

int main() {
    HRESULT hr;
    IWbemLocator* loc = nullptr;
    IWbemServices* svc = nullptr;
    IEnumWbemClassObject* en = nullptr;
    IWbemClassObject* inst = nullptr;
    IWbemClassObject* cls = nullptr;
    IWbemClassObject* inSig = nullptr;
    IWbemClassObject* inP = nullptr;
    IWbemClassObject* outP = nullptr;

    printf("=== WMI 逐步诊断 ===\n\n");

    CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
    CHECK(CoInitializeSecurity(nullptr,-1,nullptr,nullptr,
          RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
          nullptr, EOAC_NONE, nullptr), "CoInitializeSecurity");
    CHECK(CoCreateInstance(CLSID_WbemLocator,0,CLSCTX_INPROC_SERVER,
          IID_IWbemLocator,(void**)&loc), "CoCreateInstance(WbemLocator)");

    { BSTR ns = SysAllocString(L"ROOT\\WMI");
      CHECK(loc->ConnectServer(ns,0,0,0,0,0,0,&svc), "ConnectServer(ROOT\\WMI)");
      SysFreeString(ns); }
    CHECK(CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
          RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE),
          "CoSetProxyBlanket");

    { BSTR wql=SysAllocString(L"WQL"), q=SysAllocString(L"SELECT * FROM MICommonInterface");
      CHECK(svc->ExecQuery(wql,q,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,
            0,&en), "ExecQuery(MICommonInterface)");
      SysFreeString(wql); SysFreeString(q); }

    { ULONG n=0;
      CHECK(en->Next(WBEM_INFINITE,1,&inst,&n), "Next(instance)");
      printf("  实例数: %lu\n", n); }

    { // dump 实例属性
      SAFEARRAY* pa = nullptr;
      CHECK(inst->GetNames(nullptr, WBEM_FLAG_ALWAYS|WBEM_FLAG_NONSYSTEM_ONLY,
            0, &pa), "inst->GetNames");
      LONG lo=0, hi=-1; SafeArrayGetLBound(pa,1,&lo); SafeArrayGetUBound(pa,1,&hi);
      printf("  实例属性: ");
      for (LONG i=lo;i<=hi;i++){ BSTR nm=nullptr; SafeArrayGetElement(pa,&i,&nm);
        printf("%ls ", nm); SysFreeString(nm); }
      printf("\n");
      SafeArrayDestroy(pa);
    }

    { BSTR cp = SysAllocString(L"MICommonInterface");
      CHECK(svc->GetObject(cp,0,0,&cls,0), "GetObject(class)");
      SysFreeString(cp); }

    { BSTR mn = SysAllocString(L"MiInterface");
      CHECK(cls->GetMethod(mn,0,&inSig,nullptr), "GetMethod(MiInterface)");
      SysFreeString(mn); }

    { // dump 方法入参名
      SAFEARRAY* pa = nullptr;
      CHECK(inSig->GetNames(nullptr, WBEM_FLAG_ALWAYS|WBEM_FLAG_NONSYSTEM_ONLY,
            0,&pa), "inSig->GetNames");
      LONG lo=0, hi=-1; SafeArrayGetLBound(pa,1,&lo); SafeArrayGetUBound(pa,1,&hi);
      printf("  方法入参: ");
      for (LONG i=lo;i<=hi;i++){ BSTR nm=nullptr; SafeArrayGetElement(pa,&i,&nm);
        printf("%ls ", nm); SysFreeString(nm); }
      printf("\n");
      SafeArrayDestroy(pa); }

    CHECK(inSig->SpawnInstance(0,&inP), "SpawnInstance(inParams)");

    { // 装配 InData
      VARIANT v; VariantInit(&v);
      v.vt = VT_ARRAY|VT_UI1;
      SAFEARRAYBOUND b; b.lLbound=0; b.cElements=32;
      v.parray = SafeArrayCreate(VT_UI1,1,&b);
      void* p=nullptr; SafeArrayAccessData(v.parray,&p);
      unsigned char buf[32] = {0}; buf[2]=250; buf[3]=8;
      memcpy(p, buf, 32); SafeArrayUnaccessData(v.parray);
      BSTR n = SysAllocString(L"InData");
      CHECK(inP->Put(n,0,&v,0), "inP->Put(InData)");
      SysFreeString(n);
      VariantClear(&v); }

    { // 取实例路径
      VARIANT vp; VariantInit(&vp);
      CHECK(inst->Get(L"__PATH",0,&vp,0,0), "inst->Get(__PATH)");
      printf("  实例路径: %ls\n", vp.bstrVal ? vp.bstrVal : L"(null)");

      BSTR mn = SysAllocString(L"MiInterface");
      hr = svc->ExecMethod(vp.bstrVal, mn, 0, nullptr, inP, &outP, nullptr);
      printf("%-34s hr=0x%08lX %s\n", "ExecMethod", (unsigned long)hr, HrName(hr));
      SysFreeString(mn);

      if (SUCCEEDED(hr) && outP) {
          printf("\n  === 调用成功 ===\n");
          SAFEARRAY* pa=nullptr;
          if (SUCCEEDED(outP->GetNames(nullptr,WBEM_FLAG_ALWAYS|WBEM_FLAG_NONSYSTEM_ONLY,0,&pa))) {
              LONG lo=0,hi=-1; SafeArrayGetLBound(pa,1,&lo); SafeArrayGetUBound(pa,1,&hi);
              for (LONG i=lo;i<=hi;i++){
                  BSTR nm=nullptr; SafeArrayGetElement(pa,&i,&nm);
                  VARIANT vv; VariantInit(&vv);
                  if (SUCCEEDED(outP->Get(nm,0,&vv,0,0))) {
                      if ((vv.vt==(VT_ARRAY|VT_UI1)) && vv.parray) {
                          printf("  %ls = [", nm);
                          unsigned char* d=nullptr;
                          SafeArrayAccessData(vv.parray,(void**)&d);
                          ULONG c=vv.parray->rgsabound[0].cElements;
                          for (ULONG k=0;k<c && k<8;k++) printf("%u%s", d[k], k<7?",":"");
                          printf("...] (共%lu字节)\n", c);
                          SafeArrayUnaccessData(vv.parray);
                      } else if (vv.vt==VT_UI2) {
                          printf("  %ls = %u\n", nm, vv.uiVal);
                      } else if (vv.vt==VT_BOOL) {
                          printf("  %ls = %s\n", nm, vv.boolVal?"TRUE":"FALSE");
                      } else {
                          printf("  %ls = (vt=%u)\n", nm, vv.vt);
                      }
                  }
                  VariantClear(&vv); SysFreeString(nm);
              }
              SafeArrayDestroy(pa);
          }
      }
      VariantClear(&vp); }

done:
    if (outP) outP->Release();
    if (inP) inP->Release();
    if (inSig) inSig->Release();
    if (cls) cls->Release();
    if (inst) inst->Release();
    if (en) en->Release();
    if (svc) svc->Release();
    if (loc) loc->Release();
    CoUninitialize();
    return 0;
}
