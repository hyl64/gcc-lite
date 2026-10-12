// test_sensors2.cpp — PDH 分步诊断
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdio>
#include <vector>
#include <string>

#pragma comment(lib, "pdh.lib")

static void Hr(const char* label, PDH_STATUS s) {
    printf("%-44s 0x%08lX %s\n", label, (unsigned long)s,
           s == ERROR_SUCCESS ? "OK" : "");
}

int main() {
    printf("=== PDH 分步诊断 ===\n\n");

    PDH_HQUERY q = nullptr;
    Hr("PdhOpenQuery", PdhOpenQueryW(nullptr, 0, &q));
    if (!q) return 1;

    PDH_HCOUNTER hCpu = nullptr, hMem = nullptr, hPerf = nullptr;

    Hr("PdhAddEnglishCounterW(CPU)", PdhAddEnglishCounterW(
        q, L"\\Processor Information(_Total)\\% Processor Time", 0, &hCpu));
    Hr("PdhAddEnglishCounterW(MEM)", PdhAddEnglishCounterW(
        q, L"\\Memory\\% Committed Bytes In Use", 0, &hMem));
    Hr("PdhAddEnglishCounterW(PERF)", PdhAddEnglishCounterW(
        q, L"\\Processor Information(_Total)\\% Processor Performance", 0, &hPerf));

    printf("\n=== GPU 通配符展开 ===\n");
    DWORD sz = 0;
    PDH_STATUS st = PdhExpandWildCardPathW(nullptr,
        L"\\GPU Engine(*)\\Utilization Percentage", nullptr, &sz, 0);
    printf("  首次查询: 0x%08lX  需要字节数=%lu\n", (unsigned long)st, sz);

    if (st == PDH_MORE_DATA && sz > 0) {
        std::vector<wchar_t> buf(sz / sizeof(wchar_t) + 8);
        st = PdhExpandWildCardPathW(nullptr,
            L"\\GPU Engine(*)\\Utilization Percentage", buf.data(), &sz, 0);
        printf("  展开: 0x%08lX\n", (unsigned long)st);
        if (st == ERROR_SUCCESS) {
            int n = 0;
            const wchar_t* p = buf.data();
            while (*p) {
                printf("  实例 %d: %ls\n", ++n, p);
                p += wcslen(p) + 1;
                if (n > 20) { printf("  ...\n"); break; }
            }
        }
    }

    printf("\n=== 采集 ===\n");
    Hr("PdhCollectQueryData#1", PdhCollectQueryData(q));
    Sleep(1000);
    Hr("PdhCollectQueryData#2", PdhCollectQueryData(q));

    PDH_FMT_COUNTERVALUE v;
    if (hCpu) {
        st = PdhGetFormattedCounterValue(hCpu, PDH_FMT_DOUBLE, nullptr, &v);
        printf("  CPU: st=0x%08lX cstatus=0x%08lX val=%.2f\n",
               (unsigned long)st, (unsigned long)v.CStatus, v.doubleValue);
    }
    if (hMem) {
        st = PdhGetFormattedCounterValue(hMem, PDH_FMT_DOUBLE, nullptr, &v);
        printf("  MEM: st=0x%08lX cstatus=0x%08lX val=%.2f\n",
               (unsigned long)st, (unsigned long)v.CStatus, v.doubleValue);
    }
    if (hPerf) {
        st = PdhGetFormattedCounterValue(hPerf, PDH_FMT_DOUBLE, nullptr, &v);
        printf("  PERF: st=0x%08lX cstatus=0x%08lX val=%.2f\n",
               (unsigned long)st, (unsigned long)v.CStatus, v.doubleValue);
    }

    PdhCloseQuery(q);
    printf("\n完成\n");
    return 0;
}
