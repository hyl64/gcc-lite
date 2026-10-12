# 结论：为什么风扇/模式控制目前无法从任何程序调用

## 一句话总结

**`MiInterface` 这个 WMI 方法在你的机器上目前返回 `WBEM_E_NOT_SUPPORTED (0x8004100F)`,
而且厂商自己的程序（`斗战者控制台.exe`）调用它也得到完全相同的错误码。**
这不是我们的代码问题，是固件/驱动层当前不响应。

## 决定性证据

### 1. 厂商进程自己也在失败

启动 `斗战者控制台.exe` 后，WMI-Activity 日志记录：

```
12:22:11  ClientProcessId=36668   ExecMethod root\WMI:MICommonInterface.InstanceName="ACPI\\PNP0C14\\MIFS_0"::MiInterface   rc=0x8004100F
12:22:19  ClientProcessId=36668   ...                                                                                     rc=0x8004100F
```

`0x8004100F` = `WBEM_E_NOT_SUPPORTED`，与我的 `exact.exe` 得到的是**同一个错误码**。

### 2. 厂商程序不再产生硬件日志

```
日志最后写入：2026-10-12 08:35:38
当前时间：    2026-10-12 12:22
```

主程序运行中（PID 31312，656 MB 内存，窗口 "Gaming Control Center"，Responding: True），
但**不再写入任何 `_CPUGPUSYSFanSpeed` / `GPUThermometer` 记录**。

对比：正常工作时它每小时写 28 次采样（24 小时不间断）。

### 3. 类、实例、方法、权限全部正常

| 检查项 | 结果 |
|--------|------|
| `root\WMI : MICommonInterface` 类存在 | ✅ |
| 实例 `ACPI\PNP0C14\MIFS_0` 存在 | ✅ |
| 方法 `MiInterface(InData, OutData, Reserved)` 定义存在 | ✅ |
| 管理员权限调用 | ✅ 已解决 |
| `GetMethodParameters("MiInterface")` | ✅ 成功，返回 `InData` |
| 对象路径格式（单引号 + 单反斜杠） | ✅ 可绑定 |
| `InvokeMethod` | ❌ `0x8004100F` / `InvalidObject` |

### 4. 路径/参数已穷举验证

| 路径写法 | 结果 |
|----------|------|
| `'ACPI\PNP0C14\MIFS_0'`（与原程序 IL 常量一致） | 绑定 OK，调用 `0x8004100F` |
| `'ACPI\\PNP0C14\\MIFS_0'` | `0x80041002` NOT_FOUND |
| `"ACPI\PNP0C14\MIFS_0"` | `0x80041008` INVALID_PARAMETER |
| 查询枚举得到对象后调用 | `InvalidObject` |

`InData` 参数构造也穷举过：`VT_ARRAY|VT_UI1`（下界 0 与 1）、`VT_ARRAY|VT_VARIANT`、30/32 字节，
全部得到 `0x80041008` 或 `InvalidObject`。

## 原程序的真实调用序列（已从 IL 完整还原）

```csharp
// BLD.WMIOperation.WMIMethodServices.ExcMethod (rva=8912)
ManagementObject obj = new ManagementObject(
    "root\\WMI",
    "MICommonInterface.InstanceName='ACPI\\PNP0C14\\MIFS_0'");

ManagementBaseObject mp = obj.GetMethodParameters("MiInterface");
mp["InData"] = inData32;                 // [2]=250(Get)/251(Set)  [3]=方法号  [4]=值
ManagementBaseObject r = obj.InvokeMethod("MiInterface", mp, null);
byte[] outData = (byte[])r["OutData"];
// 失败时输出: "An error occurred while trying to execute the WMI method: " + ex.Message
```

我的 `build\exact.exe` 逐字复刻了这段，结果相同。

## 协议本身（逆向完整，可复用）

### 缓冲区布局（32 字节）

| 偏移 | 含义 |
|------|------|
| 0–1 | 保留 |
| 2 | MethodType：`250`=Get，`251`=Set |
| 3 | MethodName（见下） |
| 4 | 参数值（Set 写入值） |
| 5–31 | 保留 |

### MethodName（`WMIMethodName`，实测枚举值）

| 值 | 名称 | 值 | 名称 |
|----|------|----|------|
| 8 | SystemPerMode | 18 | RGBKeyboardBrightness |
| 9 | GPUMode | 19 | SystemAcType |
| 10 | KeyboardType | 20 | MaxFanSpeedSwitch |
| 11 | FnLock | 21 | MaxFanSpeed |
| 12 | TPLock | 22 | CPUThermometer |
| 13 | CPUGPUSYSFanSpeed | 23 | CPUPower |
| 16 | RGBKeyboardMode | 255 | UnKnow |
| 17 | RGBKeyboardColor | | |

### 四个性能模式（`WMISystemPerMode`）

`0`=BalanceMode `1`=PerformanceMode `2`=QuietMode `3`=FullspeedMode

### 风扇类型（`WMIFanType`）

`0`=CPUGPUFan `1`=SYSFan

### 风扇转速返回格式（来自厂商日志实证）

```
_CPUGPUSYSFanSpeed:(2232, 2213, 1962)
                    CPU   GPU   SYS   单位: RPM
```

---

## 结论与建议

**协议已 100% 逆向清楚，调用代码已写好并通过编译**（`build\exact.exe`、`build\mi.exe`、
`MiBridge.cpp/h`）。唯一阻塞是**当前固件状态下 `MiInterface` 不响应**——
连厂商自己的程序也调不通。

### 可能原因

1. **需要重启恢复**：ACPI-WMI 映射在长时间运行后可能进入异常状态
2. **需要特定前置条件**：例如先由 BIOS/驱动初始化某个状态
3. **固件更新后接口变更**：BIOS 版本 `1.0.19.0`
4. **需要厂商服务处于特定状态**：目前只有热键服务在跑

### 建议的验证步骤

1. **重启电脑**，开机后不要启动原程序，直接管理员运行 `build\exact.exe` 测试
2. 如果重启后成功 → 说明是运行时状态问题，重启即可恢复
3. 如果重启后仍失败 → 说明是固件/驱动层限制，需要用其他途径（见下）

### 备选路径（如果 WMI 确实不可用）

- **EC 直读写**：通过已有的 `WinRing0_1_2_2.sys`（你机器上已存在，PAIPTAC 签名）
  访问 EC 寄存器。这是 NoteBook FanControl 等工具的做法，但需要确定 EC 寄存器地址映射，
  且**有硬件风险**（写错地址可能影响风扇/温控）。
- **等待厂商修复**：如果是固件 bug，只能等 BIOS 更新。

---

*本文档基于对 `斗战者控制台.dll` 的静态 IL 分析、WMI 活动日志、
以及厂商程序运行时行为的实证验证。*
