// ============================================================================
//  diag2.cpp — 精确复刻原程序的调用方式
//  原程序: ManagementObjectSearcher(query) -> ManagementObject -> InvokeMethod
//  关键差异: 用 searcher 得到的对象（带完整 scope）而非手工拼路径
// ============================================================================
#include <windows.h>
#include <objbase.h>
#include <wbemidl.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static void DumpHr(const char* label, HRESULT hr) {
    printf("%-44s hr=0x%08lX\n", label, (unsigned long)hr);
}

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

    printf("=== diag2: 复刻原始调用路径 ===\n\n");

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);

    CoCreateInstance(CLSID_WbemLocator,0,CLSCTX_INPROC_SERVER,IID_IWbemLocator,(void**)&loc);

    { BSTR ns=SysAllocString(L"ROOT\\WMI");
      hr=loc->ConnectServer(ns,0,0,0,0,0,0,&svc); DumpHr("ConnectServer",hr);
      SysFreeString(ns);
      if (FAILED(hr)) goto done; }

    CoSetProxyBlanket(svc,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,
        RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE);

    // 查询实例
    { BSTR wql=SysAllocString(L"WQL");
      BSTR q=SysAllocString(L"SELECT * FROM MICommonInterface");
      hr=svc->ExecQuery(wql,q,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,0,&en);
      DumpHr("ExecQuery",hr); SysFreeString(wql); SysFreeString(q);
      if (FAILED(hr)) goto done; }

    { ULONG n=0; hr=en->Next(WBEM_INFINITE,1,&inst,&n); DumpHr("Next",hr);
      if (FAILED(hr)||n==0) goto done; }

    // 取得实例路径（两种方式）
    {
        BSTR path1 = nullptr, path2 = nullptr;
        VARIANT v; VariantInit(&v);
        if (SUCCEEDED(inst->Get(L"__PATH",0,&v,0,0))) {
            path1 = SysAllocString(v.bstrVal);
            printf("__PATH      = %ls\n", v.bstrVal);
        }
        VariantClear(&v);
        if (SUCCEEDED(inst->Get(L"__RELPATH",0,&v,0,0))) {
            path2 = SysAllocString(v.bstrVal);
            printf("__RELPATH   = %ls\n", v.bstrVal);
        }
        VariantClear(&v);

        // 取方法签名
        { BSTR cp=SysAllocString(L"MICommonInterface");
          hr=svc->GetObject(cp,0,0,&cls,0); DumpHr("GetObject(class)",hr);
          SysFreeString(cp); }
        if (cls) {
            BSTR mn=SysAllocString(L"MiInterface");
            hr=cls->GetMethod(mn,0,&inSig,nullptr); DumpHr("GetMethod",hr);
            SysFreeString(mn);
        }

        if (inSig) {
            hr=inSig->SpawnInstance(0,&inP); DumpHr("SpawnInstance",hr);
            if (inP) {
                VARIANT av; VariantInit(&av);
                av.vt=VT_ARRAY|VT_UI1;
                SAFEARRAYBOUND b; b.lLbound=0; b.cElements=32;
                av.parray=SafeArrayCreate(VT_UI1,1,&b);
                void* p=nullptr; SafeArrayAccessData(av.parray,&p);
                unsigned char buf[32]={0}; buf[2]=250; buf[3]=8;
                memcpy(p,buf,32); SafeArrayUnaccessData(av.parray);
                BSTR n=SysAllocString(L"InData");
                hr=inP->Put(n,0,&av,0); DumpHr("Put(InData)",hr);
                SysFreeString(n); VariantClear(&av);

                BSTR mn=SysAllocString(L"MiInterface");

                printf("\n--- 尝试1: ExecMethod(__PATH) ---\n");
                hr=svc->ExecMethod(path1,mn,0,nullptr,inP,&outP,nullptr);
                DumpHr("ExecMethod(full path)",hr);
                if (outP) { outP->Release(); outP=nullptr; }

                printf("\n--- 尝试2: ExecMethod(__RELPATH) ---\n");
                hr=svc->ExecMethod(path2,mn,0,nullptr,inP,&outP,nullptr);
                DumpHr("ExecMethod(rel path)",hr);
                if (outP) { outP->Release(); outP=nullptr; }

                printf("\n--- 尝试3: ExecMethodOnObject(实例对象) ---\n");
                // IWbemServices::ExecMethod 要求 path；某些 provider 需要 class path
                { BSTR cp2=SysAllocString(L"MICommonInterface");
                  hr=svc->ExecMethod(cp2,mn,0,nullptr,inP,&outP,nullptr);
                  DumpHr("ExecMethod(class path)",hr);
                  SysFreeString(cp2);
                  if (outP) { outP->Release(); outP=nullptr; } }

                printf("\n--- 尝试4: 用 __RELPATH 但 class-level (InstanceName 加引号) ---\n");
                { std::wstring p3 = L"MICommonInterface.InstanceName=\"ACPI\\\\PNP0C14\\\\MIFS_0\"";
                  BSTR b3=SysAllocString(p3.c_str());
                  hr=svc->ExecMethod(b3,mn,0,nullptr,inP,&outP,nullptr);
                  DumpHr("ExecMethod(relpath quoted)",hr);
                  if (outP) {
                      printf("  *** 成功! 读取 OutData ***\n");
                      VARIANT vv; VariantInit(&vv);
                      BSTR on=SysAllocString(L"OutData");
                      if (SUCCEEDED(outP->Get(on,0,&vv,0,0)) && (vv.vt==(VT_ARRAY|VT_UI1))) {
                          unsigned char* d=nullptr;
                          SafeArrayAccessData(vv.parray,(void**)&d);
                          ULONG c=vv.parray->rgsabound[0].cElements;
                          printf("  OutData(%lu) = ", c);
                          for (ULONG k=0;k<c;k++) printf("%u ", d[k]);
                          printf("\n");
                          SafeArrayUnaccessData(vv.parray);
                      }
                      VariantClear(&vv); SysFreeString(on);
                      outP->Release(); outP=nullptr;
                  }
                  SysFreeString(b3); }

                SysFreeString(mn);
            }
        }
        if (path1) SysFreeString(path1);
        if (path2) SysFreeString(path2);
    }

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
