# GCC Lite · Linux（Ubuntu）版

 Lecoo 斗战者 N176（Bellator N176）性能模式控制 —— [Windows 版 gcc-lite](../README.md) 的 Ubuntu 移植。

```
静音模式 (quiet)  ←→  均衡模式 (balanced)  ←→  性能模式 (performance)  ←→  狂暴模式 (fullspeed)
```

---

## 这是什么

Windows 上随机附带的「斗战者控制台」（433 MB .NET 程序）通过一个 ACPI-WMI 接口
（`MICommonInterface` / GUID `B60BFB48-3E5B-49E4-A0E9-8CFFE1B3434B`）控制
性能模式、风扇、键盘灯。`gcc-lite` 项目已把该协议完整逆向（见
[docs/PROTOCOL.md](../docs/PROTOCOL.md)）。

好消息：**Linux 内核 7.1 起已合入该接口的官方驱动 `bitland-mifs-wmi`**。
本项目把它**回移植到 Ubuntu 24.04 的 7.0 内核**并补齐用户态工具，于是：

- `gcclite mode 狂暴` —— 一条命令切换性能模式
- 风扇转速、CPU 温度进入标准 **hwmon**（`sensors` 命令直接可读）
- 性能模式注册为标准 **platform_profile**，GNOME 电源模式开关、
  `powerprofilesctl` 自动打通
- 键盘背光注册为标准 LED，可用亮度键控制

## 模式对照

| 命令 | 中文别名 | platform_profile | WMI 值 | 说明 |
|---|---|---|---|---|
| `gcclite mode quiet` | `静音` | `low-power` | 2 | 风扇最安静，功耗最低 |
| `gcclite mode balanced` | `均衡` | `balanced` | 0 | 日常默认档 |
| `gcclite mode performance` | `性能` | `balanced-performance` | 1 | 提高功耗墙 |
| `gcclite mode fullspeed` | `狂暴` `全速` | `performance` | 3 | 功耗墙与风扇全放开 |

> ⚠ **狂暴/性能档需要圆孔电源适配器（DC）供电**。用 Type-C(PD) 或电池供电时，
> 固件本身会拒绝切换（与 Windows 原版行为一致），`gcclite` 会给出明确提示。
> 这是 BIOS 限制，不是软件限制。

## 安装

```bash
cd gcc-lite/linux
sudo bash scripts/install.sh
```

脚本会：编译内核驱动 → 用 MOK 密钥签名 → 安装 DKMS → 安装 CLI/GUI/polkit →
尝试加载。

**本机 Secure Boot 已开启**（可用 `mokutil --sb-state` 查看），因此首次安装会
多出一次性步骤：

1. 脚本末尾会让你为 MOK 设一个**一次性密码**（自己起一个，重启时用）；
2. `reboot`，开机进入蓝色 **MOK Management** 菜单；
3. 选 **Enroll MOK** → Continue → 输入刚才的密码 → Reboot；
4. 回到桌面后再跑一次 `sudo bash scripts/install.sh`（或直接
   `sudo modprobe bitland_mifs_wmi`）。

之后内核升级由 DKMS 自动重编译，无需重复操作。

安装完成自检：

```bash
gcclite doctor      # 逐项检查驱动 / sysfs / hwmon / Secure Boot / MOK
```

## 使用

### 命令行 `gcclite`

```bash
gcclite                    # 状态总览（模式/温度/风扇/电源）
gcclite mode               # 查看当前模式
gcclite mode 狂暴           # 切狂暴（等价: gcclite mode fullspeed）
gcclite mode 静音
gcclite list               # 固件支持的档位
gcclite fan                # 风扇转速（CPU/GPU/SYS）
gcclite temp               # CPU 温度
gcclite fan-boost on|off   # 风扇全速开关（对应原版 MaxFanSpeed）
gcclite monitor 2          # 每 2 秒刷新一次
gcclite gpu-mode           # 查看显卡模式 (hybrid/discrete/uma)
gcclite kb-mode fixed      # 键盘灯效 (off/cyclic/fixed/custom)
```

### 图形界面 `gcclite-gui`

应用菜单里搜 “GCC Lite”，或直接运行 `gcclite-gui`。四个大按钮切模式，
实时显示温度/风扇/供电。普通用户点击即可切换（经 polkit 授权，见下）。

### 桌面集成

驱动注册 platform_profile 后，`power-profiles-daemon`（Ubuntu 自带）会自动
接管，映射关系：

| GNOME / powerprofilesctl | 实际模式 |
|---|---|
| 节能 power-saver | 静音 quiet |
| 平衡 balanced | 均衡 balanced |
| 性能 performance | 狂暴 fullspeed |

（`性能 performance` 这一档 GNOME 不显示，用 `gcclite mode performance` 设置。）

### 权限说明

模式切换需要写 sysfs（root）。安装后提供 `pkexec` 特权助手
`/usr/local/libexec/gcclite-write`，它**只接受 4 个白名单档位值**，
配套 polkit 规则允许「本机活动会话」免密切换——与
`power-profiles-daemon` 自身的权限级别相同。远程/非活动会话会弹管理员密码。

## 常见问题

**`gcclite mode 狂暴` 报「固件拒绝了这次切换」**
正在用 Type-C 或电池供电。换圆孔适配器即可（BIOS 限制）。

**`modprobe` 报 “Key was rejected by service”**
MOK 没注册成功。`mokutil --test-key /var/lib/shim-signed/mok/MOK.der`
检查；失败则 `sudo mokutil --import …` 后重启重试。

**切换没生效/状态不刷新**
`gcclite doctor` 看哪一项是 ✗；`dmesg | grep bitland` 看内核日志。

**想恢复出厂行为**
`sudo bash scripts/uninstall.sh`（MOK 密钥保留，彻底清除见脚本提示）。

## 与 Windows 版的对应关系

| Windows gcc-lite | Linux 版 |
|---|---|
| MiBridge（WMI COM 调用） | 内核驱动 `bitland-mifs-wmi`（同一 WMI 方法，协议逐字节一致） |
| PDH 性能计数器 | sysfs / hwmon / procfs |
| RAPL CPU 功耗（EnergyMeter） | `/sys/class/powercap/intel-rapl:0/energy_uj`（内核通用接口） |
| `SystemPerMode` Set | `platform_profile`（`gcclite mode`） |
| `MaxFanSpeedSwitch` | `gcclite fan-boost`（驱动 sysfs `fan_boost`） |
| 时段调度（Schedule.h） | 用 systemd timer / cron 调 `gcclite mode …` 即可 |
| Win32 GUI | `gcclite-gui`（GTK4） |

移植细节（内核 7.0 与 7.1 的两处 API 差异、防御性加固）见
[docs/PORTING.md](docs/PORTING.md)。

## 组件与安全

```
linux/
  driver/bitland-mifs-wmi.c     内核驱动（上游官方驱动的 7.0 回移植）
  gcclite                        CLI（纯 Python 标准库，零第三方依赖）
  gcclite-gui                    GUI（GTK4）
  scripts/install.sh             一键安装（编译+签名+DKMS+MOK）
  scripts/uninstall.sh           卸载
  scripts/gcclite-write          pkexec 特权助手（白名单严格校验）
  scripts/45-gcclite.rules       polkit 规则
  tests/test_gcclite.py          离线单元测试（14 例）
```

- 所有硬件写操作都走**厂商程序同款 ACPI-WMI 官方接口**，不碰 EC 直写、
  不碰未知 IOCTL，无内核内存映射。
- 驱动协议常量全部来自原版逆向（[docs/PROTOCOL.md](../docs/PROTOCOL.md)）与
  内核文档，无猜测值。
- `gcclite-write` 特权助手只接受 4 个档位字符串，无其它能力。

## 许可

驱动为 GPL-2.0-or-later（上游作者 Mingyou Chen），
其余部分 MIT（同本仓库根目录 LICENSE）。
