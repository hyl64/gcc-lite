#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdio>

#pragma comment(lib, "pdh.lib")

int main() {
    printf("step1: open query\n"); fflush(stdout);
    PDH_HQUERY q = nullptr;
    PDH_STATUS st = PdhOpenQueryW(nullptr, 0, &q);
    printf("  st=0x%08lX q=%p\n", (unsigned long)st, (void*)q); fflush(stdout);

    printf("step2: add cpu counter\n"); fflush(stdout);
    PDH_HCOUNTER hCpu = nullptr;
    st = PdhAddEnglishCounterW(q, L"\\Processor Information(_Total)\\% Processor Time", 0, &hCpu);
    printf("  st=0x%08lX h=%p\n", (unsigned long)st, (void*)hCpu); fflush(stdout);

    printf("step3: collect\n"); fflush(stdout);
    st = PdhCollectQueryData(q);
    printf("  st=0x%08lX\n", (unsigned long)st); fflush(stdout);

    Sleep(1000);
    st = PdhCollectQueryData(q);
    printf("  collect2 st=0x%08lX\n", (unsigned long)st); fflush(stdout);

    printf("step4: read value\n"); fflush(stdout);
    PDH_FMT_COUNTERVALUE v;
    st = PdhGetFormattedCounterValue(hCpu, PDH_FMT_DOUBLE, nullptr, &v);
    printf("  st=0x%08lX cstatus=0x%08lX val=%.2f\n",
           (unsigned long)st, (unsigned long)v.CStatus, v.doubleValue); fflush(stdout);

    printf("step5: gpu wildcard\n"); fflush(stdout);
    DWORD sz = 0;
    st = PdhExpandWildCardPathW(nullptr, L"\\GPU Engine(*)\\Utilization Percentage", nullptr, &sz, 0);
    printf("  st=0x%08lX sz=%lu\n", (unsigned long)st, sz); fflush(stdout);

    PdhCloseQuery(q);
    printf("done\n"); fflush(stdout);
    return 0;
}
