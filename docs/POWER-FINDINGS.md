# 功率数据源结论（实测）

## 已验证可用的功率数据

| 数据 | 来源 | 实测 | 状态 |
|------|------|------|------|
| **CPU 封装功耗** | PDH `\Energy Meter(rapl_package0_pkg)\Energy`（AMD RAPL） | 12–17 W | ✅ **精确** |
| CPU 温度 | MiInterface id=22 | 76–88 °C | ✅ |
| 电池电压 | `root\WMI : BatteryStatus.Voltage` | 15400 mV | ✅ |
| 电池剩余容量 | `BatteryStatus.RemainingCapacity` | 71390 mWh | ✅ |
| 电池满电容量 | `BatteryFullChargedCapacity.FullChargedCapacity` | 72820 mWh | ✅ |
| 电池循环次数 | `BatteryCycleCount.CycleCount` | 38 | ✅ |
| 电池功率（放电时） | `Voltage × DischargeRate` | 接电源时为 0 | ⚠️ 仅放电可用 |
| 电池功率（充电时） | `Voltage × ChargeRate` | 接电源时为 0 | ⚠️ 仅充电可用 |

## 硬限制（固件层，非软件可解）

### 1. 接电源时无电流采样

```
BatteryStatus.ChargeRate    = 0 mA
BatteryStatus.DischargeRate = 0 mA
BatteryStatus.PowerOnline   = True
BatteryStatus.Charging      = False
BatteryStatus.Discharging   = False
```

本机 ACPI 固件在**交流供电 + 电池满电**状态下，不通过 `_BST` 方法返回
充放电电流。这是 BIOS 实现决定的，Windows 也拿不到。

### 2. 无 DC 输入功率（适配器功率）

已穷举验证，全部不可用：

| 尝试 | 结果 |
|------|------|
| `Win32_PerfFormattedData_PowerMeterCounter_PowerMeter.Power` | 恒 `0` |
| `\Power Meter(*)\Power` PDH 计数器 | 恒 `0` |
| `root\WMI : MSPower` | 只有 DeviceEnable，无功率 |
| `root\WMI : MS_SM_AdapterInformationQuery` | 无实例 |
| ACPI 适配器设备 `ACPI\ACPI0003` 属性 | 无功率评级字段 |
| PDH 电池能量计数器 | **不存在**（只有 CPU 的 Energy Meter） |
| ACPI `_PSR`/`_PCL` 方法 | 仅报告"是否插入"，无功率 |

**结论**：本机固件未实现适配器功率报告。DC 输入功率无法获取。

### 3. MiInterface 的 `CPUPower`（id=23）未实现

```
5 次采样均为: OutData = [0,128,0,23,0,0,...]
                        ↑ id  ↑ 值恒为 0
```

该固件方法存在但返回 0，不可用。

---

## 可信的替代方案

### A. 电池放电时（最准）

拔掉电源后，`DischargeRate` 会有真实数值，此时：

```
整机功率 = 电压 × 放电电流
```

这测量的是**真实整机功耗**（CPU+GPU+屏幕+主板全部），已实现在程序中，
拔电源即自动显示。

### B. 接电源时（估算）

可以给出**可知功耗下界**：

```
CPU 功耗（RAPL 实测） + 其他估算
```

程序当前显示 CPU 功耗（精确）+ 说明文字，不做不可靠的整机估算。

---

## 建议

1. **想要精确整机功率** → 拔掉电源使用电池，程序会自动显示实测值
2. **想要适配器功率** → 需厂商在 BIOS 中实现 ACPI 功率报告，软件无法解决
3. **关注 CPU 功耗** → RAPL 提供硬件级精确值，已实现

---

*本结论基于对本机（Lecoo Bellator N176 / AMD Ryzen 9 8940HX）
所有已知功率数据源的穷举实测，时间 2026-10-12。*
