// mini3.cpp — 逐步测试 CPdhSensors 类
#include <windows.h>
#include <cstdio>
#include "Sensors.h"

using namespace gcc;

int main() {
    printf("A: start\n"); fflush(stdout);

    CPdhSensors s;
    printf("B: constructed\n"); fflush(stdout);

    std::string err;
    bool ok = s.Open(err);
    printf("C: Open -> %s (err=%s)\n", ok ? "true" : "false", err.c_str()); fflush(stdout);

    SysSensors v;
    printf("D: sampling...\n"); fflush(stdout);

    if (s.Sample(v)) {
        printf("E: cpu=%.1f mem=%.1f gpu=%.1f\n",
               v.cpuUsagePct, v.memUsagePct, v.gpuUsagePct); fflush(stdout);
    } else {
        printf("E: Sample failed\n"); fflush(stdout);
    }

    s.Close();
    printf("F: done\n"); fflush(stdout);
    return 0;
}
