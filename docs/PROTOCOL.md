# 斗战者控制台（Lecoo Bellator N176）硬件控制协议 — 逆向结果

## 1. 硬件通路架构

```
任何程序
   └─ WMI 调用
        └─ root\WMI : MICommonInterface  (Windows 内置 ACPI-WMI 桥)
             └─ 实例 ACPI\PNP0C14\MIFS_0
                  └─ 方法 MiInterface(InData:byte[32], OutData:byte[32], Reserved)
                       └─ ACPI 固件 (BIOS/UEFI)
                            └─ EC / 风扇 / 功耗墙
```

**关键结论：不存在厂商私有内核驱动。**

- 原程序 `斗战者控制台.dll` 的全部 P/Invoke 只有 8 个：`user32.dll`
  （`SetWindowsHookEx` / `CallNextHookEx` / `GetKeyState` / `keybd_event` /
  `GetMessage` / `TranslateMessage` / `DispatchMessage`）与 `kernel32.dll`
  （`GetSystemPowerStatus`）。**没有任何 EC / IOCTL / 驱动访问。**
- 控制通道 100% 是 WMI。`MICommonInterface` 是微软 Windows 自带的
  ACPI-WMI 映射接口（`ACPI\PNP0C14\MIFS`），因此无需任何驱动即可复现。
- 早前误判：`kbldfltr.sys` 与风扇无关，它是微软签名的
  **Keyboard Lockdown Filter**（键盘锁定过滤器）。

## 2. 报文格式（32 字节）

原程序 `BLD.WMIOperation.WMIMethodServices._MakeMethodPrams` 的 IL 显示：

```
byte[32] buf;                  // ldc.i4.s 32 ; newarr
buf[2] = (byte)methodType;     // 偏移 4+
buf[3] = (byte)methodName;
```

`SetValue(methodName, value)` 把值写到**偏移 4**：

```
buf[4] = value;                // Set 时写入的数值
```

即请求缓冲区布局：

| 偏移 | 含义 |
|-----|------|
| 0–1 | 保留（0） |
| 2   | MethodType：250 = Get，251 = Set |
| 3   | MethodName：见下表 |
| 4   | 参数值（Set 时的写入值；Get 时为 0） |
| 5–31| 保留（0） |

响应为 32 字节 `OutData`；`Get` 返回值从 `OutData[4]` 起解出。

## 3. MethodName 枚举（WMIMethodName）

| 值 | 名称 | 含义 |
|----|------|------|
| 8  | SystemPerMode | 性能模式 |
| 9  | GPUMode | 显卡模式 |
| 10 | KeyboardType | 键盘类型 |
| 11 | FnLock | Fn 锁定 |
| 12 | TPLock | 触摸板锁定 |
| **13** | **CPUGPUSYSFanSpeed** | **风扇转速（CPU/GPU/系统）** |
| 14 | GPUFanSpeed_NotUse | 弃用 |
| 16 | RGBKeyboardMode | RGB 模式 |
| 17 | RGBKeyboardColor | RGB 颜色 |
| 18 | RGBKeyboardBrightness | RGB 亮度 |
| 19 | SystemAcType | 供电类型 |
| **20** | **MaxFanSpeedSwitch** | **风扇全速开关** |
| **21** | **MaxFanSpeed** | **风扇全速** |
| **22** | **CPUThermometer** | **CPU 温度** |
| **23** | **CPUPower** | **CPU 功率** |
| 255| UnKnow | — |

## 4. 四个性能模式（WMISystemPerMode）

| 值 | 名称 | 对应界面 |
|----|------|---------|
| 0 | BalanceMode | 均衡模式 |
| 1 | PerformanceMode | 性能模式 |
| 2 | QuietMode | 静音模式 |
| 3 | FullspeedMode | 全速模式 |

## 5. 风扇（WMIFanType）

| 值 | 名称 |
|----|------|
| 0 | CPUGPUFan |
| 1 | SYSFan |
| 255 | Unknow |

## 6. 结果状态（WMIResultState）

| 值 | 含义 |
|----|------|
| 0 | OFF |
| 1 | ON |
| 255 | Unknow |

## 7. 事件（WMIEventService）

通过 `root\WMI` 的 ACPI 事件类订阅（原程序用 `ManagementEventWatcher`）：

| 值 | 事件名 |
|----|--------|
| 15 | SystemPerMode |
| 26 | CPUFanSpeed |
| 32 | GPUFanSpeed |
| 4  | AirPlaneMode |
| 7  | FnState |
| 9  | CapsLkState |

事件类型：`HotKey = 1`。

## 8. 电池 / 功率数据源

| 数据 | 来源 | 实测 |
|------|------|------|
| 电池电压 | `root\WMI : BatteryStatus.Voltage` | 15400 (mV) |
| 充电速率 | `BatteryStatus.ChargeRate` | mA |
| 放电速率 | `BatteryStatus.DischargeRate` | mA |
| 剩余容量 | `BatteryStatus.RemainingCapacity` | mWh |
| 交流状态 | `kernel32!GetSystemPowerStatus` | ✅ |
| 整机功率 | `Win32_PerfFormattedData_PowerMeterCounter_PowerMeter` | ⚠️ 本机恒为 0，固件未实现 |
| DC 输入功率 | 无 ACPI 电源表接口 | ❌ 不可得 |

> 电池功率 = Voltage × (ChargeRate 或 DischargeRate)，两者单位分别为 mV 与 mA。

## 9. 与原程序的功能对应

| 需求 | 实现方式 |
|------|---------|
| 风扇自由调速 | MethodName 13 (`CPUGPUSYSFanSpeed`) + FanType |
| 四个性能模式 | MethodName 8 (`SystemPerMode`)，值 0/1/2/3 |
| 定时切换模式 | 本地定时器 → 调用 SystemPerMode Set |
| 实时功率显示 | 电池：BatteryStatus；CPU：MethodName 23 |
| 温度 | MethodName 22 (`CPUThermometer`) |

## 10. 安全性说明

所有写操作均通过**官方 ACPI-WMI 接口**，与厂商程序走完全相同的路径，
不涉及 EC 直写、不涉及未知 IOCTL、不涉及内核驱动。
参数值全部来自逆向出的枚举，未做任何猜测。
