# 移植记录：gcc-lite Windows → Ubuntu 24.04

本文记录把 hyl64/gcc-lite（Windows）带到 Ubuntu 上所做的每一步决策，
以及为什么这样做。全部结论都在本机（Lecoo Bellator N176，Ryzen 9 8940HX，
Ubuntu 24.04.5，内核 7.0.0-38-generic = 7.0.14，Secure Boot 开启）上验证过。

## 0. 通路选择：为什么移植的是“上游内核驱动”而不是别的

Windows 版通过 `root\WMI:MICommonInterface.MiInterface(InData[32], OutData[30])`
控制硬件。Linux 用户态**没有**调用任意 WMI 方法的通用接口，可选路线只有：

| 路线 | 结论 |
|---|---|
| A. 上游 `bitland-mifs-wmi` 内核驱动回移植 | ✅ 采用。协议由内核社区审核、实现正确、暴露标准接口 |
| B. `acpi_call` 模块 + 手写 AML 调用 | ❌ 同样要装内核模块、要 DSDT 逆向方法路径，且绕过类型检查，更危险 |
| C. EC 直写 | ❌ 原项目明确不做（Safety 一节），固件无文档 |
| D. 等 Ubuntu 内核升级 | 驱动 7.1 才进主线；本机 7.0.14 等不到 |

本机核实到的事实链：

- `PNP0C14:00`，`/sys/bus/acpi/devices/PNP0C14:00/uid` = **MIFS**，ACPI 路径
  `\_SB.PCI0.WMID` —— 与 Windows 实例 `ACPI\PNP0C14\MIFS_0` 同一设备；
- WMI 总线上存在 GUID `B60BFB48-3E5B-49E4-A0E9-8CFFE1B3434B`（object_id `AA`），
  **未绑定任何驱动** —— 即 `MICommonInterface`，随时可被我们的驱动认领；
- 上游内核文档（`Documentation/wmi/devices/bitland-mifs-wmi.rst`）给出的
  MOF 与本仓库逆向的协议逐字节一致（见 §2）。

## 1. 两处内核 API 差异（7.0.14 → 7.1）

上游驱动 `drivers/platform/x86/bitland-mifs-wmi.c` 按 7.1+ 内核写就，直接
编 7.0.14 报两个错：

### 1.1 `wmidev_invoke_method()` 多了一个参数

```
7.0.14: int wmidev_invoke_method(wdev, instance, method_id, in, out);
7.1  : int wmidev_invoke_method(wdev, instance, method_id, in, out, min_size);
```

`min_size` 是固件应答的最小长度校验。回移植用宏统一两种形态：

```c
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
#define WMI_INVOKE(wdev, in, out) \
        wmidev_invoke_method((wdev), 0, 1, (in), (out), sizeof(struct bitland_mifs_output))
#else
#define WMI_INVOKE(wdev, in, out) \
        wmidev_invoke_method((wdev), 0, 1, (in), (out))
#endif
```

**并且**在旧内核分支补上等价校验：把固件应答先拷进一个清零的栈上结构
（`min(length, 32)` 字节），不足 4 字节头部（return_code + function）就返回
`-ENOMSG`。这比上游更严格一点：即使固件返回 30 或 36 字节，所有字段读取
仍在界内，不会越界。

### 1.2 `struct wmi_driver` 没有 `.min_event_size`

7.1 的 WMI 核心会替驱动校验事件长度、并在无数据时直接丢弃事件；
7.0.14 的核心却可能把 **NULL buffer** 或任意短 buffer 交给 `notify_new`
（见 `drivers/platform/wmi/core.c` v7.0.14）：

```c
if (!obj) { driver->notify_new(&wblock->dev, NULL); return; }
ret = wmi_unmarshal_acpi_object(obj, &buffer);   /* 无长度校验 */
```

上游驱动直接 `buffer->data` 解引用，在 7.0.14 上会空指针崩溃。回移植在
`notify_new` 开头补了：

```c
if (!buffer || !buffer->data ||
    buffer->length < sizeof(struct bitland_mifs_event))
        return;
```

`.min_event_size` 字段本身也用同一 `LINUX_VERSION_CODE` 宏门控，
7.1+ 上恢复上游原样。

### 1.3 其它小差异

- `LED_FUNCTION_KBD_BACKLIGHT` 在 7.0.14 位于 `dt-bindings/leds/common.h`，
  显式 include（7.1+ 上同样无害）；
- `ATTRIBUTE_GROUPS(laptop)` 宏展开成显式的 `attribute_group` 定义，
  两版内核都成立。

以上改完后模块在 7.0.0-38-generic 上**一次编译通过**，
`nm -u` 检查全部 39 个未定义符号都能在 `Module.symvers` / 内核核心里解析。

## 2. 协议一致性核对（Windows 逆向 vs 内核驱动）

Windows 侧请求缓冲布局（`MiBridge.h` 逆向结果）：

```
in[1] = 250/251   (MethodType: Get/Set)
in[3] = 8         (MethodName: SystemPerMode)
in[4] = 0..3      (值)
```

内核驱动用小端 16 位字段表达同一布局：

```
__le16 operation = 0xFA00  → 字节序 00 FA  → in[0]=0x00, in[1]=0xFA(=250) ✓
__le16 function  = 0x0800  → 字节序 00 08  → in[2]=0x00, in[3]=0x08(=8)    ✓
payload[0]        = 值      → in[4]                                        ✓
```

应答侧同理：`return_code`(bytes 0–1) + `function`(bytes 2–3) + `data`(byte 4 起)，
`data[0]` 恰好就是 Windows 读的 `OutData[4]`。**两套实现互为印证。**

一处已知差异：SYS 风扇转速。Windows 版 `MiBridge.h` 读 `buf[8..9]`，
内核驱动与内核文档读 `data[6]`（即 `buf[10..11]`）。以内核驱动为准
（它经过硬件验证），Linux 版因此报告正确偏移。

## 3. 用户态设计

```
        ┌─────────────┐   sysfs    ┌──────────────────────┐
        │  gcclite     │ ────────► │ platform_profile      │
        │  gcclite-gui │   hwmon   │ hwmon(温度/风扇RPM)    │
        └──────┬───────┘           │ fan_boost/gpu_mode/…  │
               │ pkexec            └──────────┬───────────┘
        ┌──────▼───────┐  wmidev_invoke_method │ ACPI-WMI
        │ gcclite-write │ ◄──── 白名单校验 ────┘
        └──────────────┘
```

- CLI 与 GUI **共用同一模块**（GUI 用 importlib 加载 `gcclite`），
  行为与错误提示天然一致；
- 写路径三级降级：直写（root）→ pkexec 特权助手 → power-profiles-daemon；
- `EOPNOTSUPP`（errno 95，固件因供电拒绝高性能档）被翻译成中文可操作提示，
  这是用户最常见的“失败”。

模块名与驱动名不同这一坑也记录在案：模块叫 `bitland_mifs_wmi`（下划线，
`/sys/module`），注册到 WMI 总线的驱动叫 `bitland-mifs-wmi`（连字符，
`/sys/bus/wmi/drivers/`）。CLI 曾因此查不到驱动属性，已修正并有测试覆盖。

## 4. Secure Boot 现实约束

本机 `mokutil --sb-state` = **SecureBoot enabled**，且
`/var/lib/shim-signed/mok/` 为空（从未注册过任何 MOK）。
因此模块必须：

1. 用 OpenSSL 生成 2048 位密钥（10 年期，Ubuntu 约定路径）；
2. `sign-file sha512` 签名 `.ko`；
3. `mokutil --import` 注册 + 重启进 MOK 菜单 Enroll —— **需要用户一次性
   物理操作**，这是无法绕过的固件级流程，install.sh 已把它引导成
   “设密码 → 重启 → 再跑一次”三步。

密钥落在 Ubuntu DKMS 的默认搜索路径，内核升级时 DKMS 会自动用它重签。

## 5. 事件 GUID 冲突（已知，无害）

事件 GUID `46C93E13-…`（`HID_EVENT20`）已被内核自带的 `redmi-wmi`
驱动占用（Redmibook 与本机共用 Bitland ODM 的 GUID）。
控制 GUID `B60BFB48` 空闲，不受影响——**性能模式/风扇/温度全部走控制 GUID**，
因此本移植完整可用；受影响的只有“热键事件走本驱动”这一可选增强
（现有热键仍由 redmi-wmi 正常处理，输入设备 `Redmibook WMI keys` 在
`/proc/bus/input/devices` 可见）。

## 6. 真实安装中踩到的坑（都已修复并写进脚本）

这些是实际跑 `install.sh` 时才暴露的问题，离线测试覆盖不到：

### 6.1 OpenSSL 3 输出的是 PEM，`mokutil` 只认 DER

`openssl req -x509 -out MOK.der` **不保证**输出 DER 编码——首次安装生成的
`MOK.der` 实际是 PEM，`mokutil --import` 直接报
`is not a valid x509 certificate in DER format`。
（`sign-file` 两种都能读，所以模块签名看似正常，掩盖了问题。）

修法：先输出 `.pem` 再 `openssl x509 -outform DER` 显式转换；
并加了一段幂等修复——若 `MOK.der` 解析为 DER 失败但 PEM 成功，就地转换。

### 6.2 `mokutil --test-key` 在“未注册”时也返回 0

这是个会误导自动化的坑：未注册时它**打印** `is not enrolled` 到 stdout，
但**退出码仍是 0**。脚本原先用退出码判断，于是误判为“已注册”并直接
报错退出。修法：匹配输出文本中的 `is enrolled`（注意 `is not enrolled`
也包含子串 `is enrolled`，必须先排除否定形式）。

### 6.3 读 EFI 变量需要 root

`mokutil --test-key` / `--list-new` 在普通用户下拿不到输出。
`gcclite doctor` 现在把这种情况报成「MOK 状态未知（需 root）」，
而不是错误地报「未注册」。

### 6.4 `sig_key` 是证书**序列号**，不是 SHA1 指纹

排查签名链时容易搞混：`modinfo -F sig_key` 输出的是证书序列号
（如 `45:98:84:D7:…`），而 `mokutil --list-new` 显示的是 SHA1 指纹
（`de:6c:fc:ab:…`）。两者天然不同，**不能直接比对**。
正确校验方式是比对序列号或 `openssl x509 -serial` 与 `sig_key`。

顺带一个 `sign-file` 的行为：它**不会**覆盖已签名的模块，
必须先 `make clean` 重新构建出未签名模块再签，否则旧签名会残留。

### 6.5 固件在 Set 操作上不回填 `function` 字段

上游驱动会校验应答的 `function` 是否与请求一致，不一致则返回 `-ENOMSG`。
本机固件实测：**Set 操作的应答里 `function` 恒为 0**。
结果是每一次写入都「成功但报错」（`leds …: Setting an LED's brightness failed (-42)`），
模式切换也带一个假错误。

修法：只在固件确实返回了非零 `function` 时才做一致性校验。

```c
if (result.function && result.function != input->function)
        return -ENOMSG;
```

这是本移植里唯一一处**与上游语义有意不同**的地方，属于对特定固件行为的适配。

### 6.6 主板固件会静默跳过 shim 的 MOK 菜单

本机（Lecoo Bellator N176，蓝天 ODM）重启时**不弹**蓝色 MOK 菜单：
`mokutil --import` 写入的 `MokNew` 会被消耗掉却不注册，两次重启都是如此
（`MokTimeout` 设为 60 秒也无效）。

绕过办法：临时创建一个**直接引导 `mmx64.efi`（MOK Manager）** 的 UEFI 启动项，
重启后即进入管理界面；注册成功后删掉该引导项。

```bash
# 注意 ESP 在 nvme1n1p2（Ubuntu 盘），不是 Windows 盘 nvme0n1
sudo efibootmgr --create --disk /dev/nvme1n1 --part 2 \
     --label "MOKManager" --loader '\EFI\ubuntu\mmx64.efi'
# 重启 → 进 MOK Manager 完成 Enroll → 之后删掉：
sudo efibootmgr -b <编号> -B
```

验证注册成功的正确方式：**`mokutil --test-key` 有运行时缓存延迟**，
刚注册完可能仍报 not enrolled，但 `mokutil --list-enrolled` 已能看到，
且 `modprobe` 会直接成功：

```bash
sudo mokutil --list-enrolled | grep -A1 'gcc-lite'
sudo modprobe bitland_mifs_wmi && echo OK
```

### 6.7 DKMS 3.x 的两个陷阱

**陷阱一：`$(CURDIR)` 让 .ko 落到错误位置。**
DKMS 执行 `make -C /usr/src/<pkg> build`，此时 `$(CURDIR)` 是 `/usr/src/<pkg>`；
但 DKMS 实际把源码复制到 `/var/lib/dkms/<pkg>/<ver>/build` 并**只在那里找产物**。
结果：编译日志显示成功、`exit code: 0`，DKMS 却报
`binary package for bitland-mifs-wmi: 1.0.0 not found`。

修法：`Makefile` 增加 `MODULE_SRC` 变量（默认 `$(CURDIR)`），
`dkms.conf` 的 `MAKE[0]` 显式传入 DKMS 的扁平构建路径：

```
MODULE_SRC=/var/lib/dkms/${PACKAGE_NAME}/${PACKAGE_VERSION}/build
```

> 注意**不要**用 `$$` 转义或 `${dkms_tree}` 拼路径：DKMS 的配置层会先吃掉一层 `$`，
> 实测 `$${PWD:-...}` 被展开成了 PID（`10501{PWD:-10501(pwd)}`）。
> dkms 3.4.3 的布局是扁平的，直接写死上面这个路径最稳。

**陷阱二：残留的 .ko 会让 Kbuild 跳过编译。**
`install.sh` 早期版本把 `driver/*.ko` 连同源码一起 `cp` 进 `/usr/src`，
Kbuild 看到产物已存在就跳过编译，再次触发「找不到产物」。
现在只复制源文件（`Makefile`/`dkms.conf`/`*.c`），绝不带构建产物。

**其它实测结论：**

- `DEST_MODULE_LOCATION` 在 dkms 3.4.3 仍是**必需**指令，删掉会报
  `No 'DEST_MODULE_LOCATION' directive specified` —— 尽管它同时打印
  Deprecated 提示；
- DKMS 会自动用 `/var/lib/shim-signed/mok/MOK.{priv,der}` 签名
  （dkms 3.x 走 `kmodsign`），签名信息在 `modinfo` 里可见，无需手动补签；
- DKMS 装到 `updates/dkms/*.ko.zst`（压缩），`extra/` 下的手动副本会被
  自动归档；脚本现在会主动清掉 `extra/` 的旧副本避免混淆；
- `dkms install` 后 `depmod` 索引有短暂延迟，立即 `modprobe` 可能失败。
  脚本改为「depmod → modprobe，失败则重试 3 次」，避免把索引延迟
  误判成 Secure Boot 拒绝而错误触发 MOK 流程。

### 6.8 `--test-key` 的缓存坑（第二次踩到）

`mokutil --test-key` 读的是运行时缓存 `MokListRT`，**刚注册完仍可能报
`is not enrolled`**，此时 `--list-enrolled` 已经能看到证书、`modprobe`
也已经成功了。`install.sh` 与 `gcclite doctor` 全部改用
`--list-enrolled` 判断，彻底避开这个假阴性。

## 7. 已验证 / 未验证清单

**已验证（本机，非 root）**

- 驱动在 7.0.0-38-generic 编译通过，符号全部可解析；
- MOK 密钥生成 + `sign-file` 签名流程可用（用一次性测试密钥验过，
  签名信息正确出现在 `modinfo` 输出，之后重编译还原为未签名交付态）；
- `gcclite` 14 个单元测试全绿（伪造 sysfs 覆盖全部读路径 + 直写路径 +
  驱动缺失降级）；
- `gcclite-gui` 在本机 XWayland 会话实际启动渲染 3.5 秒无任何报错；
- 驱动未加载时 CLI/GUI 优雅降级（真实环境实测）。

**已在真机完成（2026-10-15，sudo 安装实操 + 重启验证）**

- `install.sh` 全程跑通：MOK 密钥生成 → 驱动编译 → sha512 签名 → 装到
  `/lib/modules/7.0.0-38-generic/extra/` → `depmod` 识别；
- MOK 证书经 `mmx64.efi` 引导项注册成功（见 §6.6），
  `mokutil --list-enrolled` 可见 `CN=gcc-lite module signing`；
- **`modprobe bitland_mifs_wmi` 成功**，驱动自动绑定
  `/sys/bus/wmi/drivers/bitland-mifs-wmi/B60BFB48-…`；
- `platform_profile` 与 hwmon 均正常注册，读到**真实硬件数据**：
  CPU 温度 62–72 °C、CPU/GPU/SYS 三路风扇转速；
- **四档模式实测切换成功**，硬件真实响应：

  | 请求 | 回读 `platform_profile` | CPU 风扇 | SYS 风扇 | CPU 温度 |
  |---|---|---|---|---|
  | `low-power`（静音） | low-power | 2175 RPM | 4045 RPM | 62 °C |
  | `performance`（狂暴） | performance | 2642 RPM | 5543 RPM | 72 °C |

- `gcclite doctor` 全绿「一切正常 🎉」；
- `gcclite-gui` 实测渲染正常，四个模式按钮 + 传感器面板工作正常；
- `dmesg` 无任何 bitland 相关报错（§6.5 修完后 LED 报错消失）；
- **DKMS 安装成功**（§6.7 修完后）：`dkms status` = installed，
  产物在 `/lib/modules/…/updates/dkms/bitland-mifs-wmi.ko.zst`，
  由 dkms 自动以 MOK 密钥签名；**全新清理后重跑 `install.sh` 一次通过**，
  且不再误报 MOK 注册需求（§6.8）。

**唯一未能离线验证的环节**

若 `sudo dmesg | grep bitland` 出现 `MiInterface` 返回非 0x8000 之类的日志，
说明固件应答与预期不符——请把输出发给作者。
