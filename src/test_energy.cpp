// test_energy.cpp — CPU 功耗测试
#include <cstdio>
#include "EnergyMeter.h"

using namespace gcc;

int main() {
    printf("=== CPU 功耗测量 (RAPL) ===\n\n");

    CEnergyMeter em;
    std::string err;
    if (!em.Open(err)) {
        printf("打开失败: %s\n", err.c_str());
        return 1;
    }
    printf("RAPL 已就绪\n\n");

    PowerReading r;
    int got = 0;
    for (int i = 0; i < 8; ++i) {
        if (em.SampleCpu(r)) {
            printf("  采样 %d: CPU 封装功耗 = %.2f W\n", ++got, r.cpuPackageW);
        }
        Sleep(200);
    }
    if (got == 0) printf("  未取得有效样本\n");

    em.Close();
    printf("\n=== 完成 ===\n");
    return 0;
}
