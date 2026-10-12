// ============================================================================
//  MiBridge.h  —  ACPI-WMI 硬件控制桥 (斗战者控制台 / Lecoo Bellator N176)
//  协议来源：逆向 斗战者控制台.dll (BLD.WMIOperation.WMIMethodServices)
//  ---------------------------------------------------------------------------
//  WMI 命名空间 : root\WMI
//  WMI 类       : MICommonInterface
//  实例         : ACPI\PNP0C14\MIFS_0
//  方法         : MiInterface(InData : UInt8Array[32], OutData : UInt8Array[32])
//
//  请求缓冲区布局（逆向自 _MakeMethodPrams 的 IL）：
//      [0..1]  保留
//      [2]     MethodType  250=Get / 251=Set
//      [3]     MethodName  见 WmiMethod
//      [4]     参数值 (Set 时写入)
//      [5..31] 保留
//
//  响应：OutData[4] 起为返回值
// ============================================================================
#pragma once

#include <windows.h>
#include <objbase.h>
#include <wbemidl.h>
#include <comdef.h>
#include <cstdint>
#include <string>
#include <optional>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace gcc {

// ---- 方法号 (WMIMethodName) ------------------------------------------------
enum WmiMethod : uint8_t {
    kSystemPerMode       = 8,
    kGpuMode             = 9,
    kKeyboardType        = 10,
    kFnLock              = 11,
    kTpLock              = 12,
    kCpuGpuSysFanSpeed   = 13,   // 风扇转速
    kRgbKeyboardMode     = 16,
    kRgbKeyboardColor    = 17,
    kRgbKeyboardBright    = 18,
    kSystemAcType        = 19,
    kMaxFanSpeedSwitch   = 20,   // 风扇全速开关
    kMaxFanSpeed         = 21,   // 风扇全速
    kCpuThermometer      = 22,   // CPU 温度
    kCpuPower            = 23,   // CPU 功率
};

// ---- 方法类型 (WMIMethodType) ----------------------------------------------
enum WmiMethodType : uint8_t { kGet = 250, kSet = 251 };

// ---- 性能模式 (WMISystemPerMode) -------------------------------------------
enum PerfMode : uint8_t {
    kBalance     = 0,
    kPerformance = 1,
    kQuiet       = 2,
    kFullspeed   = 3,
};

// ---- 风扇类型 (WMIFanType) -------------------------------------------------
enum FanType : uint8_t { kCpuGpuFan = 0, kSysFan = 1 };

// ---- 开关状态 (WMIResultState) ---------------------------------------------
enum SwitchState : uint8_t { kOff = 0, kOn = 1 };

// ---- 供电类型 (WMISystemACType) --------------------------------------------
enum AcType : uint8_t { kTypeC = 1, kCircularHole = 2 };

// ============================================================================
//  MiBridge — 封装 WMI 调用
// ============================================================================
class MiBridge {
public:
    MiBridge() = default;
    ~MiBridge();

    // 初始化 COM + 定位 MICommonInterface 实例。成功返回 true。
    bool Open(std::string& err);

    void Close();
    bool IsOpen() const { return instance_ != nullptr; }

    // 执行一次 MiInterface 调用。
    //   method : 方法号
    //   type   : kGet / kSet
    //   value  : Set 时的值（Get 时忽略）
    //   out    : 返回的 32 字节 OutData
    bool Call(uint8_t method, uint8_t type, uint8_t value,
              uint8_t* out32, std::string& err);

    // ---- 便利接口 ----------------------------------------------------------
    bool GetRaw(uint8_t method, uint8_t& value, std::string& err);
    bool SetRaw(uint8_t method, uint8_t value, std::string& err);

    // 性能模式
    bool GetPerfMode(uint8_t& mode, std::string& err) {
        return GetRaw(kSystemPerMode, mode, err);
    }
    bool SetPerfMode(uint8_t mode, std::string& err) {
        return SetRaw(kSystemPerMode, mode, err);
    }

    // 风扇
    bool SetFanSpeed(uint8_t speed, std::string& err) {
        return SetRaw(kCpuGpuSysFanSpeed, speed, err);
    }
    bool GetFanSpeed(uint8_t& speed, std::string& err) {
        return GetRaw(kCpuGpuSysFanSpeed, speed, err);
    }
    bool SetMaxFanSpeed(bool on, std::string& err) {
        return SetRaw(kMaxFanSpeedSwitch, on ? kOn : kOff, err);
    }

    // CPU 温度 / 功率
    bool GetCpuTemp(uint8_t& v, std::string& err) {
        return GetRaw(kCpuThermometer, v, err);
    }
    bool GetCpuPower(uint8_t& v, std::string& err) {
        return GetRaw(kCpuPower, v, err);
    }

    // 风扇转速：OutData[4..9] 为三个 16 位小端 RPM（CPU / GPU / SYS）
    struct FanRpm { int cpu = -1; int gpu = -1; int sys = -1; };
    bool GetFanRpm(FanRpm& out, std::string& err) {
        uint8_t buf[32] = {0};
        if (!Call(kCpuGpuSysFanSpeed, kGet, 0, buf, err)) return false;
        out.cpu = buf[4] | (buf[5] << 8);
        out.gpu = buf[6] | (buf[7] << 8);
        out.sys = buf[8] | (buf[9] << 8);
        return true;
    }

private:
    IWbemServices*   services_ = nullptr;
    IWbemLocator*    locator_  = nullptr;
    IWbemClassObject* instance_ = nullptr;
    IWbemClassObject* inSig_   = nullptr;   // MiInterface 输入签名
    bool comInited_ = false;
    std::wstring instancePath_;             // 实例对象路径
};

} // namespace gcc
