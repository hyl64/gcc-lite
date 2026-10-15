#!/bin/bash
# ============================================================================
#  gcc-lite (Linux) 安装脚本 —— Lecoo 斗战者 N176 / Ubuntu
# ----------------------------------------------------------------------------
#  用法：  sudo bash linux/scripts/install.sh
#
#  做六件事：
#    1. 检查依赖（内核头文件 / gcc / openssl / mokutil）
#    2. 生成 Secure Boot MOK 密钥（Ubuntu 约定路径 /var/lib/shim-signed/mok）
#    3. 编译并签名 bitland-mifs-wmi 内核驱动
#    4. 安装模块（有 dkms 就走 DKMS，否则装到当前内核 extra/）
#    5. 安装 gcclite CLI、GUI、特权助手与 polkit 规则
#    6. 尝试加载驱动；若因 Secure Boot 被拒则引导 MOK 注册 + 重启
#
#  可重复执行（幂等）。
# ============================================================================
set -euo pipefail

LINUX_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER_DIR="$LINUX_DIR/driver"
SCRIPTS_DIR="$LINUX_DIR/scripts"

MODNAME="bitland_mifs_wmi"
DRVDIR_NAME="bitland-mifs-wmi"
WMI_GUID="B60BFB48-3E5B-49E4-A0E9-8CFFE1B3434B"

KREL="$(uname -r)"
KDIR="/lib/modules/$KREL/build"
MOK_DIR="/var/lib/shim-signed/mok"
MODULE_DST_DIR="/lib/modules/$KREL/extra"

c_green='\033[32m'; c_yellow='\033[33m'; c_red='\033[31m'; c_bold='\033[1m'; c_off='\033[0m'
step() { printf "\n${c_bold}${c_green}==>${c_off} ${c_bold}%s${c_off}\n" "$*"; }
warn() { printf "${c_yellow}    ! %s${c_off}\n" "$*"; }
die()  { printf "${c_red}    ✗ %s${c_off}\n" "$*" >&2; exit 1; }
info() { printf "    %s\n" "$*"; }

[[ $EUID -eq 0 ]] || die "请用 sudo 运行：sudo bash $0"

# ---------------------------------------------------------------------------
step "1/6 检查依赖"
# ---------------------------------------------------------------------------
missing=()
need_cmd() { command -v "$1" >/dev/null 2>&1 || missing+=("$2"); }
need_cmd gcc "gcc (apt install build-essential)"
need_cmd make "make (apt install build-essential)"
need_cmd openssl "openssl (apt install openssl)"
need_cmd python3 "python3 (Ubuntu 自带)"
[[ -d $KDIR ]] || missing+=("linux-headers-$KREL (apt install linux-headers-$(uname -r))")
[[ -d /sys/bus/wmi/devices/$WMI_GUID ]] || die "本机未发现 MICommonInterface WMI 设备 ($WMI_GUID) —— 该机型固件可能不带此接口"

if ((${#missing[@]})); then
    warn "缺少依赖："
    printf '        %s\n' "${missing[@]}"
    die "先安装以上软件包再重跑"
fi
info "内核 $KREL，工具链齐全 ✓"

MOKUTIL_AVAILABLE=0
command -v mokutil >/dev/null 2>&1 && MOKUTIL_AVAILABLE=1

SECURE_BOOT=0
if [[ $MOKUTIL_AVAILABLE -eq 1 ]]; then
    if mokutil --sb-state 2>/dev/null | grep -qi 'enabled'; then
        SECURE_BOOT=1
        info "Secure Boot：已启用 —— 模块必须签名并注册 MOK"
    else
        info "Secure Boot：未启用 —— 模块签名可跳过"
    fi
fi

# ---------------------------------------------------------------------------
step "2/6 生成 MOK 签名密钥（$MOK_DIR）"
# ---------------------------------------------------------------------------
if [[ ! -f $MOK_DIR/MOK.der || ! -f $MOK_DIR/MOK.priv ]]; then
    mkdir -p "$MOK_DIR"
    chmod 700 "$MOK_DIR"
    # 先产生 PEM，再显式转成 DER —— mokutil 只认 DER 编码，
    # 而 openssl req -out 的输出格式不可依赖（见下方幂等修复）。
    openssl req -new -x509 -newkey rsa:2048 -keyout "$MOK_DIR/MOK.priv" \
        -out "$MOK_DIR/MOK.pem" -nodes -days 3650 \
        -subj "/CN=gcc-lite module signing/" 2>/dev/null
    openssl x509 -in "$MOK_DIR/MOK.pem" -outform DER -out "$MOK_DIR/MOK.der"
    rm -f "$MOK_DIR/MOK.pem"
    info "已生成新的 10 年期签名密钥"
fi

# 幂等修复：某些 openssl 版本会按扩展名猜测或以 PEM 输出，
# 导致 MOK.der 实际是 PEM，mokutil --import 会拒绝。就地转成 DER。
if ! openssl x509 -in "$MOK_DIR/MOK.der" -inform DER -noout 2>/dev/null; then
    if openssl x509 -in "$MOK_DIR/MOK.der" -inform PEM -noout 2>/dev/null; then
        openssl x509 -in "$MOK_DIR/MOK.der" -outform DER -out "$MOK_DIR/MOK.der.new"
        mv "$MOK_DIR/MOK.der.new" "$MOK_DIR/MOK.der"
        info "已把 MOK.der 从 PEM 转换为 DER 格式"
    else
        die "MOK.der 既不是有效 DER 也不是有效 PEM，请删除 $MOK_DIR 后重跑"
    fi
fi

chmod 600 "$MOK_DIR/MOK.priv"
chmod 644 "$MOK_DIR/MOK.der"
info "MOK.der (DER) / MOK.priv 就绪 ✓"

# ---------------------------------------------------------------------------
step "3/6 编译并签名内核驱动"
# ---------------------------------------------------------------------------
( cd "$DRIVER_DIR" && make clean >/dev/null 2>&1 || true
  make KDIR="$KDIR" ) | sed 's/^/    /'
[[ -f $DRIVER_DIR/bitland-mifs-wmi.ko ]] || die "模块编译失败"

if [[ $SECURE_BOOT -eq 1 ]]; then
    "$KDIR/scripts/sign-file" sha512 "$MOK_DIR/MOK.priv" "$MOK_DIR/MOK.der" \
        "$DRIVER_DIR/bitland-mifs-wmi.ko"
    info "模块已签名 (sha512) ✓"
else
    info "跳过签名（Secure Boot 未启用）"
fi

# ---------------------------------------------------------------------------
step "4/6 安装内核模块"
# ---------------------------------------------------------------------------
if command -v dkms >/dev/null 2>&1; then
    # DKMS：内核升级后自动重编译 + 自动用 MOK 密钥重签名
    dkms remove "$DRVDIR_NAME/1.0.0" --all >/dev/null 2>&1 || true
    DKMS_SRC="/usr/src/$DRVDIR_NAME-1.0.0"
    rm -rf "$DKMS_SRC"
    mkdir -p "$DKMS_SRC"
    # 只拷源码 —— 绝不把 .ko/.o 等构建产物带进来：
    # Kbuild 一旦看到现存产物就会跳过编译，DKMS 随后找不到新 .ko 而失败。
    for f in Makefile dkms.conf bitland-mifs-wmi.c bitland-mifs-wmi-upstream.c; do
        [[ -f "$DRIVER_DIR/$f" ]] && cp "$DRIVER_DIR/$f" "$DKMS_SRC/"
    done
    dkms add -m "$DRVDIR_NAME" -v 1.0.0 >/dev/null
    dkms build -m "$DRVDIR_NAME" -v 1.0.0 >/dev/null
    # --force：清掉先前手动 install 到 extra/ 的同名模块
    dkms install -m "$DRVDIR_NAME" -v 1.0.0 --force >/dev/null

    # DKMS 已按 MOK 密钥自动签名（dkms 3.x 的 kmodsign 流程）。
    # 这里校验一次，签名缺失才补签 —— 不盲目重签，避免破坏 dkms 记录。
    DKMS_KO="/lib/modules/$KREL/updates/dkms/bitland-mifs-wmi.ko"
    _ko_real="$DKMS_KO"
    [[ -f "$DKMS_KO.zst" ]] && _ko_real="$DKMS_KO.zst"
    if [[ $SECURE_BOOT -eq 1 && -e $_ko_real ]]; then
        if modinfo -F signer "$_ko_real" >/dev/null 2>&1; then
            info "DKMS 已用 MOK 密钥签名 ✓"
        else
            warn "DKMS 产物缺签名，补签一次"
            if [[ $_ko_real == *.zst ]]; then
                zstd -d -f "$_ko_real" -o "$DKMS_KO"
                "$KDIR/scripts/sign-file" sha512 "$MOK_DIR/MOK.priv" "$MOK_DIR/MOK.der" "$DKMS_KO"
                zstd -q -f --rm "$DKMS_KO"
            else
                "$KDIR/scripts/sign-file" sha512 "$MOK_DIR/MOK.priv" "$MOK_DIR/MOK.der" "$_ko_real"
            fi
        fi
    fi

    # 清掉早期手动安装的副本，避免与 DKMS 版本并存造成混淆
    rm -f "$MODULE_DST_DIR/$DRVDIR_NAME.ko"
    depmod -a "$KREL"
    info "已通过 DKMS 安装（内核升级后自动重建，无需再操作）✓"
else
    mkdir -p "$MODULE_DST_DIR"
    install -m 0644 "$DRIVER_DIR/bitland-mifs-wmi.ko" "$MODULE_DST_DIR/"
    depmod -a "$KREL"
    warn "未安装 dkms —— 内核升级后需重跑本脚本"
    warn "（建议：apt install dkms 后重跑，可自动跟随内核升级）"
    info "模块已安装到 $MODULE_DST_DIR ✓"
fi

# ---------------------------------------------------------------------------
step "5/6 安装用户态组件"
# ---------------------------------------------------------------------------
install -D -m 0755 "$LINUX_DIR/gcclite"      /usr/local/bin/gcclite
install -D -m 0755 "$SCRIPTS_DIR/gcclite-write" /usr/local/libexec/gcclite-write
install -D -m 0644 "$SCRIPTS_DIR/45-gcclite.rules" \
    /usr/share/polkit-1/rules.d/45-gcclite.rules
[[ -f $LINUX_DIR/gcclite-gui ]] && install -m 0755 "$LINUX_DIR/gcclite-gui" /usr/local/bin/gcclite-gui || true
install -D -m 0644 "$SCRIPTS_DIR/gcc-lite-gui.desktop" \
    /usr/local/share/applications/gcc-lite-gui.desktop 2>/dev/null || true
info "gcclite CLI / 特权助手 / polkit 规则已安装 ✓"

# ---------------------------------------------------------------------------
step "6/6 加载驱动"
# ---------------------------------------------------------------------------
# DKMS 刚装完时模块索引可能还没稳定，先重建一次再加载并重试，
# 避免把「索引未就绪」误判成「Secure Boot 拒绝」而触发 MOK 流程。
depmod -a "$KREL" 2>/dev/null || true
loaded=0
for attempt in 1 2 3; do
    if modprobe "$MODNAME" 2>/tmp/gcclite-modprobe.err; then
        loaded=1
        break
    fi
    sleep 1
    depmod -a "$KREL" 2>/dev/null || true
done

if [[ $loaded -eq 1 ]] || lsmod | grep -q "^$MODNAME "; then
    info "驱动已加载 ✓"
    if [[ -d /sys/bus/wmi/drivers/$DRVDIR_NAME/$WMI_GUID ]]; then
        printf "\n${c_green}${c_bold}🎉 安装完成！${c_off}\n\n"
        printf "    试试：  gcclite status\n"
        printf "            gcclite mode 静音     # 切静音\n"
        printf "            gcclite mode 狂暴     # 切全速（需圆孔电源适配器）\n\n"
    else
        warn "模块已加载但未绑定 WMI 设备，排查：dmesg | grep bitland"
    fi
    exit 0
fi

# 加载失败 —— 绝大多数是 Secure Boot 签名未注册
err="$(cat /tmp/gcclite-modprobe.err 2>/dev/null || true)"
if [[ $SECURE_BOOT -eq 1 ]]; then
    MOK_ENROLLED=0
    if [[ $MOKUTIL_AVAILABLE -eq 1 ]]; then
        # 用 --list-enrolled 判断，而不是 --test-key：
        #   * --test-key 在“未注册”时退出码仍是 0，只能匹配输出文本；
        #   * 且它读的是运行时缓存 MokListRT，刚注册完可能仍是旧值，
        #     会误报“未注册”。--list-enrolled 直接读固件变量，更可靠。
        if mokutil --list-enrolled 2>/dev/null | grep -qi 'gcc-lite'; then
            MOK_ENROLLED=1
        fi
    fi

    if [[ $MOK_ENROLLED -eq 1 ]]; then
        # 密钥没问题 —— 失败是别的原因，别让用户白跑一趟 MOK 流程
        warn "MOK 密钥已注册，但驱动仍加载失败：$err"
        printf "\n    签名链路是好的，问题在别处。请依次检查：\n"
        printf "      sudo dmesg | grep -iE 'bitland|module|PKCS'\n"
        printf "      modinfo bitland_mifs_wmi | grep -E 'filename|signer'\n"
        printf "      sudo dkms status -m %s\n" "$DRVDIR_NAME"
        die "把以上输出反馈给开发者"
    fi

    # 已经提交过注册请求但还没重启 —— 别重复 import
    if [[ $MOKUTIL_AVAILABLE -eq 1 ]] && mokutil --list-new 2>/dev/null | grep -qi 'gcc-lite'; then
        printf "\n${c_yellow}${c_bold}⏸ MOK 注册请求已提交，等待重启确认${c_off}\n\n"
        printf "    重启电脑：reboot\n"
        printf "    开机进入蓝色 “MOK Management” 菜单 → ${c_bold}Enroll MOK${c_off}\n"
        printf "    → Continue → 输入你设的一次性密码 → Reboot\n\n"
        printf "    回到桌面后再跑一次：${c_bold}sudo bash %s${c_off}\n" "$0"
        exit 1
    fi
fi

printf "\n${c_yellow}${c_bold}⏸ 还差最后一步：把签名密钥注册进固件（只需一次）${c_off}\n\n"

if [[ $MOKUTIL_AVAILABLE -ne 1 ]]; then
    warn "未安装 mokutil：apt install mokutil 后执行"
    printf "        sudo mokutil --import %s/MOK.der\n\n" "$MOK_DIR"
    exit 1
fi

# 让用户设一个一次性密码（重启时在 MOK 菜单里输入）。
# 可用 GCCLITE_MOK_PASSWORD 环境变量跳过交互（用于自动化）。
MOK_PW="${GCCLITE_MOK_PASSWORD:-}"
if [[ -z $MOK_PW ]]; then
    printf "    1) 为 MOK 设一个一次性密码（重启进 MOK 菜单时要用，随便起）：\n\n"
    read -r -s -p "       密码: " MOK_PW; echo
    read -r -s -p "       再输一次: " MOK_PW2; echo
    [[ "$MOK_PW" == "$MOK_PW2" ]] || die "两次输入不一致"
    [[ -n $MOK_PW ]] || die "密码不能为空"
else
    printf "    1) 使用 GCCLITE_MOK_PASSWORD 提供的密码进行注册\n\n"
fi

printf '%s\n%s\n' "$MOK_PW" "$MOK_PW" | mokutil --import "$MOK_DIR/MOK.der" \
    || die "mokutil --import 失败，可手动执行后重试"
unset MOK_PW MOK_PW2

printf "\n    2) ${c_bold}重启${c_off}电脑：reboot\n"
printf "       开机时进入蓝色 “MOK Management” 菜单 →\n"
printf "       选 ${c_bold}Enroll MOK${c_off} → View / Continue → 输入刚才的密码 → Reboot\n\n"
printf "    3) 回到桌面后再执行一次：\n\n"
printf "       ${c_bold}sudo bash %s${c_off}\n\n" "$0"
printf "    （或直接 sudo modprobe $MODNAME，然后 gcclite status 验证）\n"
printf "\n    备注：%s\n" "${err:-modprobe 被固件拒绝}"
