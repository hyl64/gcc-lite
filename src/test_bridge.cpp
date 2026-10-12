// ============================================================================
//  test_bridge.cpp — 命令行验证工具
//  用法:
//    mi.exe                读取全部状态 (只读)
//    mi.exe --mode 1       设置性能模式 (1=性能 0=均衡 2=静音 3=全速)
//    mi.exe --fan 80       设置风扇转速
//    mi.exe --maxfan on    风扇全速开关
//  注意：读写均需管理员权限。
// ============================================================================
#include "MiBridge.h"
#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "advapi32.lib")

using namespace gcc;

static const char* ModeName(uint8_t m) {
    switch (m) {
        case kBalance:     return "均衡 Balanced";
        case kPerformance: return "性能 Performance";
        case kQuiet:       return "静音 Quiet";
        case kFullspeed:   return "全速 Fullspeed";
        default:           return "未知";
    }
}

static void ShowState(MiBridge& b) {
    uint8_t v = 0;
    std::string err;

    printf("%-22s", "性能模式:");
    if (b.GetPerfMode(v, err)) printf("%u  (%s)\n", v, ModeName(v));
    else                         printf("读取失败: %s\n", err.c_str());

    printf("%-22s", "风扇转速:");
    if (b.GetFanSpeed(v, err)) printf("%u\n", v);
    else                       printf("读取失败: %s\n", err.c_str());

    printf("%-22s", "CPU 温度:");
    if (b.GetCpuTemp(v, err)) printf("%u\n", v);
    else                      printf("读取失败: %s\n", err.c_str());

    printf("%-22s", "CPU 功率:");
    if (b.GetCpuPower(v, err)) printf("%u\n", v);
    else                       printf("读取失败: %s\n", err.c_str());

    printf("%-22s", "供电类型:");
    if (b.GetRaw(kSystemAcType, v, err)) printf("%u\n", v);
    else                                 printf("读取失败: %s\n", err.c_str());

    printf("%-22s", "风扇全速:");
    if (b.GetRaw(kMaxFanSpeedSwitch, v, err)) printf("%u\n", v);
    else                                      printf("读取失败: %s\n", err.c_str());
}

int main(int argc, char** argv) {
    // 提权检查
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION e = {0};
        DWORD sz = sizeof(e);
        if (GetTokenInformation(token, TokenElevation, &e, sz, &sz) && !e.TokenIsElevated) {
            printf("[!] 未以管理员权限运行，WMI 调用将失败。\n");
            printf("    请右键终端 -> 以管理员身份运行。\n\n");
        }
        CloseHandle(token);
    }

    MiBridge bridge;
    std::string err;
    if (!bridge.Open(err)) {
        printf("[X] 打开接口失败: %s\n", err.c_str());
        return 1;
    }
    printf("[+] 已连接 root\\WMI : MICommonInterface\n\n");

    // 无参数 = 只读状态
    if (argc == 1) {
        ShowState(bridge);
        return 0;
    }

    // 解析参数
    if (argc >= 3 && strcmp(argv[1], "--mode") == 0) {
        int m = atoi(argv[2]);
        if (m < 0 || m > 3) { printf("[X] 模式取值 0-3\n"); return 2; }
        if (bridge.SetPerfMode((uint8_t)m, err))
            printf("[+] 性能模式已设为 %d (%s)\n", m, ModeName((uint8_t)m));
        else
            printf("[X] 设置失败: %s\n", err.c_str());
    }
    else if (argc >= 3 && strcmp(argv[1], "--fan") == 0) {
        int s = atoi(argv[2]);
        if (s < 0 || s > 255) { printf("[X] 转速取值 0-255\n"); return 2; }
        if (bridge.SetFanSpeed((uint8_t)s, err))
            printf("[+] 风扇转速已设为 %d\n", s);
        else
            printf("[X] 设置失败: %s\n", err.c_str());
    }
    else if (argc >= 3 && strcmp(argv[1], "--maxfan") == 0) {
        bool on = (strcmp(argv[2], "on") == 0);
        if (bridge.SetMaxFanSpeed(on, err))
            printf("[+] 风扇全速: %s\n", on ? "开" : "关");
        else
            printf("[X] 设置失败: %s\n", err.c_str());
    }
    else {
        printf("用法:\n");
        printf("  mi                 读取状态\n");
        printf("  mi --mode <0-3>    设置性能模式\n");
        printf("  mi --fan <0-255>   设置风扇转速\n");
        printf("  mi --maxfan on|off 风扇全速开关\n");
        return 2;
    }

    printf("\n--- 当前状态 ---\n");
    ShowState(bridge);
    return 0;
}
