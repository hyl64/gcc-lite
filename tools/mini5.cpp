#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdio>
#include <vector>
#include <string>

#pragma comment(lib, "pdh.lib")

int main() {
    // 先只展开一个具体实例，确认格式
    // 观察：need=60074 "bytes"。按字符算需要 30037 wchar。
    // 尝试1: 用 need*2 字节的缓冲区（即 need 个 wchar）
    DWORD need = 0;
    PDH_STATUS st = PdhExpandWildCardPathW(nullptr,
        L"\\GPU Engine(*)\\Utilization Percentage", nullptr, &need, 0);
    printf("need(报告值)=%lu\n", need);

    // 关键：这个 API 的 in/out 参数在部分实现里是「字符数」
    // 用 need 作为字符数尝试
    std::vector<wchar_t> buf(need + 64, L'\0');
    DWORD capChars = (DWORD)buf.size();
    st = PdhExpandWildCardPathW(nullptr,
        L"\\GPU Engine(*)\\Utilization Percentage", buf.data(), &capChars, 0);
    printf("尝试A (cap=%lu 字符): st=0x%08lX capOut=%lu\n",
           (DWORD)buf.size(), (unsigned long)st, capChars);

    if (st == ERROR_SUCCESS) {
        int n = 0;
        const wchar_t* p = buf.data();
        const wchar_t* end = buf.data() + buf.size();
        while (p < end && *p) {
            size_t len = wcsnlen(p, (size_t)(end - p));
            if (!len) break;
            if (++n <= 5) printf("  [%d] %ls\n", n, p);
            p += len + 1;
        }
        printf("  总实例=%d\n", n);
    } else {
        // 尝试B: 用 2 倍缓冲
        std::vector<wchar_t> b2((size_t)need * 2 + 128, L'\0');
        DWORD cap2 = (DWORD)b2.size();
        st = PdhExpandWildCardPathW(nullptr,
            L"\\GPU Engine(*)\\Utilization Percentage", b2.data(), &cap2, 0);
        printf("尝试B (cap=%lu 字符): st=0x%08lX capOut=%lu\n",
               (DWORD)b2.size(), (unsigned long)st, cap2);
        if (st == ERROR_SUCCESS) {
            int n = 0;
            const wchar_t* p = b2.data();
            const wchar_t* end = b2.data() + b2.size();
            while (p < end && *p) {
                size_t len = wcsnlen(p, (size_t)(end - p));
                if (!len) break;
                if (++n <= 5) printf("  [%d] %ls\n", n, p);
                p += len + 1;
            }
            printf("  总实例=%d\n", n);
        }
    }
    return 0;
}
