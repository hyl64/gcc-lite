#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdio>
#include <vector>
#include <string>

#pragma comment(lib, "pdh.lib")

int main() {
    PDH_HQUERY q = nullptr;
    PdhOpenQueryW(nullptr, 0, &q);

    // GPU 路径展开：排查为何没找到 3D 引擎
    DWORD need = 0;
    PDH_STATUS st = PdhExpandWildCardPathW(nullptr,
        L"\\GPU Engine(*)\\Utilization Percentage", nullptr, &need, 0);
    printf("expand#1 st=0x%08lX need=%lu bytes\n", (unsigned long)st, need);

    if (need > 0) {
        DWORD chars = need / sizeof(wchar_t) + 64;
        std::vector<wchar_t> buf(chars, L'\0');
        DWORD capIn = chars;
        st = PdhExpandWildCardPathW(nullptr,
            L"\\GPU Engine(*)\\Utilization Percentage", buf.data(), &capIn, 0);
        printf("expand#2 st=0x%08lX capOut=%lu\n", (unsigned long)st, capIn);
        if (st == ERROR_SUCCESS) {
            int total = 0, e3d = 0, added = 0;
            const wchar_t* p = buf.data();
            const wchar_t* end = buf.data() + chars;
            while (p < end && *p) {
                size_t len = wcsnlen(p, (size_t)(end - p));
                if (!len) break;
                std::wstring path(p, len);
                ++total;
                if (path.find(L"engtype_3D") != std::wstring::npos) {
                    ++e3d;
                    PDH_HCOUNTER h = nullptr;
                    PDH_STATUS s2 = PdhAddCounterW(q, path.c_str(), 0, &h);
                    if (s2 == ERROR_SUCCESS) ++added;
                    if (e3d <= 3) printf("  3D实例: %ls  add=0x%08lX\n", path.c_str(), (unsigned long)s2);
                }
                p += len + 1;
            }
            printf("总实例=%d  3D引擎=%d  成功添加=%d\n", total, e3d, added);
        }
    }

    // CPU：连续两次采样
    PDH_HCOUNTER hCpu = nullptr;
    PdhAddEnglishCounterW(q, L"\\Processor Information(_Total)\\% Processor Time", 0, &hCpu);
    PdhCollectQueryData(q);
    Sleep(1000);
    PdhCollectQueryData(q);
    PDH_FMT_COUNTERVALUE v;
    PdhGetFormattedCounterValue(hCpu, PDH_FMT_DOUBLE, nullptr, &v);
    printf("CPU(第二次采样)=%.1f%%\n", v.doubleValue);

    PdhCloseQuery(q);
    return 0;
}
