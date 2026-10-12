# GCC Lite — Lecoo Bellator N176 Performance Control

A **229 KB** pure Win32 C++ replacement for the 433 MB .NET/WPF vendor console
(「斗战者控制台」) shipped with the Lecoo Bellator N176 laptop.

Reverse-engineered hardware protocol, zero third-party dependencies, single native executable.

[中文说明](#中文说明) · [Protocol](docs/PROTOCOL.md) · [Findings](docs/FINDINGS.md)

---

## Highlights

| | Original | GCC Lite |
|---|---|---|
| Size | **433 MB** (972 files) | **229 KB** (1 file) |
| Runtime | .NET 6 + WPF + 188 DLLs | **None** (static CRT, pure Win32) |
| Dependencies | LibreHardwareMonitor, Syncfusion, … | **Zero** |
| Reduction | — | **~1,900×** |

### Features

| Feature | Status |
|---|---|
| Four performance modes (Balance / Performance / Quiet / Fullspeed) | ✅ verified read+write |
| Fan speed monitoring (CPU / GPU RPM) | ✅ live |
| **Manual fan speed control** | ✅ new |
| **Time-range scheduling** (`HH:MM–HH:MM → mode`) | ✅ new |
| CPU temperature | ✅ live |
| CPU / GPU / memory utilization | ✅ live |
| **CPU power draw (hardware RAPL)** | ✅ new, 12–17 W typical |
| CPU frequency | ✅ live |
| Battery voltage / capacity / cycle count | ✅ live |
| Battery charge–discharge power | ✅ when current flows |
| Whole-system power (on battery) | ✅ voltage × discharge current |
| DC input power | ❌ firmware does not expose it |

---

## How it works

The vendor console talks to hardware through a **single ACPI-WMI method** — no
kernel driver, no EC poking:

```
root\WMI : MICommonInterface
  └─ instance  ACPI\PNP0C14\MIFS_0
      └─ method MiInterface(InData: byte[32], OutData: byte[30])
```

### Request buffer format

```c
byte buf[32] = {0};

buf[1] = methodType;   // 250 = Get, 251 = Set   ← offset 1
buf[3] = methodName;   // see table below
buf[4] = value;        // value for Set
```

> **Gotcha:** putting `methodType` at `buf[2]` (the intuitive offset) makes the
> firmware return `0x8004100F (WBEM_E_NOT_SUPPORTED)`. It must be `buf[1]`.
> This cost a long debugging session — see [FINDINGS.md](docs/FINDINGS.md).

### Method names

| # | Name | # | Name |
|---|------|---|------|
| 8 | SystemPerMode | 19 | SystemAcType |
| 9 | GPUMode | 20 | MaxFanSpeedSwitch |
| 10 | KeyboardType | 21 | MaxFanSpeed |
| 11 | FnLock | 22 | CPUThermometer |
| 12 | TPLock | **23** | CPUPower *(returns 0 on this model)* |
| **13** | **CPUGPUSYSFanSpeed** | 255 | UnKnow |
| 16 | RGBKeyboardMode | 17 | RGBKeyboardColor |

### Return values

| Method | Layout |
|---|---|
| SystemPerMode | `[4]` = 0 Balance / 1 Performance / 2 Quiet / 3 Fullspeed |
| CPUGPUSYSFanSpeed | `[4..5]` CPU RPM, `[6..7]` GPU RPM (uint16 LE) |
| CPUThermometer | `[4]` = °C |
| SystemAcType | `[4]` = 1 Type-C / 2 barrel |

Full detail: [docs/PROTOCOL.md](docs/PROTOCOL.md)

---

## Sensors without LibreHardwareMonitor

The vendor app bundles `LibreHardwareMonitorLib.dll` (686 KB + dependencies) for
its monitoring panel. This project uses **Windows built-in PDH counters** instead:

| Metric | Counter |
|---|---|
| CPU utilization | `\Processor Information(_Total)\% Processor Time` |
| CPU frequency | `\Processor Information(_Total)\% Processor Performance` × base clock |
| **CPU power** | `\Energy Meter(rapl_package0_pkg)\Energy` + `\Time` |
| GPU utilization | `\GPU Engine(*engtype_3D)\Utilization Percentage` (sum of instances) |
| Memory | `\Memory\% Committed Bytes In Use` + `GlobalMemoryStatusEx` |

### CPU power via AMD/Intel RAPL

```
Power(W) = ΔEnergy(nJ) / ΔTime(ms) × 1e-6
```

Measured **12–17 W** at idle on a Ryzen 9 8940HX — hardware-accurate, not an estimate.

---

## Build

Requirements: MSVC x64 (VS Build Tools) + Windows SDK.

```bat
build_gui.bat      :: -> build\gcc_lite.exe
build_console.bat  :: -> build\mi.exe
```

Manual:

```bat
call vcvars64.bat
cl /std:c++17 /EHsc /O2 /MT /utf-8 /Fe:build\selftest.exe ^
   src\selftest.cpp src\MiBridge.cpp src\PowerInfo.cpp ^
   advapi32.lib ole32.lib oleaut32.lib wbemuuid.lib
```

## Run

**Administrator required** — `MiInterface` needs elevation.
`gcc_lite.exe` embeds a `requireAdministrator` manifest, so double-clicking
triggers UAC automatically.

```powershell
.\build\gcc_lite.exe      # GUI
.\build\selftest.exe      # read-only hardware self-test
.\build\test_sched.exe    # scheduler unit tests (29 checks)
```

---

## Layout

```
src/                 core sources
  main.cpp           Win32 GUI
  MiBridge.*         ACPI-WMI hardware bridge
  Sensors.*          PDH monitoring (CPU/GPU/memory)
  EnergyMeter.*      CPU power (RAPL)
  PowerInfo.*        battery / power
  Schedule.h         time-range scheduler (header-only)
  test_*.cpp         unit tests
tools/               reverse-engineering scripts (documented for reproduction)
docs/                protocol, findings, power analysis
```

---

## Safety

All hardware writes go through the **same official ACPI-WMI interface the vendor
app uses**. This project does **not** touch:

- EC direct read/write
- undocumented IOCTLs
- kernel drivers

Every parameter value comes from enumerated constants recovered from the vendor
binary — nothing is guessed. `WinRing0` is present on the test machine but is
deliberately not used.

---

## Caveats

- Developed and verified on **Lecoo Bellator N176** (Bitland ODM, BIOS 1.0.19.0,
  AMD Ryzen 9 8940HX + RTX 5060 Laptop).
- Other models sharing the `MICommonInterface` interface may work; others may not.
- **DC input power is unavailable** on this firmware — see
  [docs/POWER-FINDINGS.md](docs/POWER-FINDINGS.md) for the full list of routes tried.

---

## License

MIT — see [LICENSE](LICENSE).

This is an independent reverse-engineering project, not affiliated with or
endorsed by Lecoo, Lenovo, or Bitland. Use at your own risk.

---

<a id="中文说明"></a>
## 中文说明

用纯 Win32 C++ 重写联想 Lecoo 斗战者 N176 随机附带的管理程序（原版
433 MB / 972 文件 → 229 KB / 1 文件，缩小约 1900 倍），并新增手动风扇调速、
时间段定时切换、CPU 功耗监测。

### 硬件通路

原程序不装任何驱动，控制 100% 走 Windows 自带的 ACPI-WMI 桥：

```
root\WMI : MICommonInterface
  └─ 实例 ACPI\PNP0C14\MIFS_0
      └─ 方法 MiInterface(InData: byte[32], OutData: byte[30])
```

请求缓冲区：`buf[1]=Get(250)/Set(251)`、`buf[3]=方法号`、`buf[4]=值`。

> ⚠ 关键坑：把 `MethodType` 放在 `buf[2]` 会收到 `0x8004100F`，
> 必须放 `buf[1]`。

### 功能

四模式切换、风扇调速、时段调度、CPU/GPU/内存占用率、CPU 频率、
CPU 功耗（RAPL 硬件计数器，实测 12–17 W）、CPU 温度、
风扇转速（CPU/GPU RPM）、电池电压/容量/循环次数/充放电功率。

### 需要管理员权限

`MiInterface` 要求提权，exe 已内嵌 `requireAdministrator` 清单，双击自动弹 UAC。

### 已知限制

- 仅在 **斗战者 N176**（蓝天代工，BIOS 1.0.19.0）上验证
- **DC 输入功率不可得**：本机固件不提供适配器功率报告
  （已穷举 PowerMeter / MSPower / ACPI 适配器属性 / PDH 电池计数器等，全部无效）
- 整机功率需**拔掉电源**用电池时才能实测（电压 × 放电电流）

### 安全

所有硬件写操作均通过厂商同款官方 ACPI-WMI 接口，不涉及 EC 直写、
未知 IOCTL 或内核驱动。参数值全部来自逆向出的枚举常量。

### 许可证

MIT。本项目为独立逆向工程成果，与 Lecoo / 联想 / 蓝天无隶属关系。
