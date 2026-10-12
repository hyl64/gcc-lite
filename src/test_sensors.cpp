// test_sensors.cpp — 轻量监控自测
#include <cstdio>
#include "Sensors.h"

using namespace gcc;

int main() {
    printf("=== 轻量系统监控测试 (PDH) ===\n\n");

    CPdhSensors s;
    std::string err;
    if (!s.Open(err)) {
        printf("打开失败: %s\n", err.c_str());
        return 1;
    }
    printf("PDH 已就绪\n\n");

    SysSensors v;
    for (int i = 0; i < 3; ++i) {
        if (s.Sample(v)) {
            printf("--- 采样 %d ---\n", i + 1);
            printf("  CPU 占用率 : %.1f %%\n", v.cpuUsagePct);
            printf("  内存占用率 : %.1f %%   (%.1f / %.1f GB)\n",
                   v.memUsagePct,
                   v.memUsedBytes / 1073741824.0,
                   v.memTotalBytes / 1073741824.0);
            printf("  GPU 占用率 : %.1f %%\n", v.gpuUsagePct);
            printf("  CPU 基准   : %.0f MHz\n", v.cpuBaseMhz);
            printf("  CPU 性能   : %.1f %%   -> 估算 %.0f MHz\n",
                   v.cpuPerfPct, v.cpuSpeedMhz);
        }
        if (i < 2) Sleep(1000);
    }
    printf("\n=== 完成 ===\n");
    return 0;
}
