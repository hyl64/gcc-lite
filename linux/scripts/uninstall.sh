#!/bin/bash
# ============================================================================
#  gcc-lite (Linux) 卸载脚本
# ----------------------------------------------------------------------------
#  用法：sudo bash linux/scripts/uninstall.sh
# ============================================================================
set -euo pipefail

MODNAME="bitland_mifs_wmi"
DRVDIR_NAME="bitland-mifs-wmi"
KREL="$(uname -r)"

c_green='\033[32m'; c_bold='\033[1m'; c_off='\033[0m'
step() { printf "\n${c_bold}${c_green}==>${c_off} ${c_bold}%s${c_off}\n" "$*"; }

[[ $EUID -eq 0 ]] || { echo "请用 sudo 运行" >&2; exit 1; }

step "卸载内核模块"
modprobe -r "$MODNAME" 2>/dev/null || true
if command -v dkms >/dev/null 2>&1; then
    dkms remove "$DRVDIR_NAME/1.0.0" --all >/dev/null 2>&1 || true
fi
rm -f "/lib/modules/$KREL/extra/$DRVDIR_NAME.ko" \
      "/lib/modules/$KREL/updates/dkms/$DRVDIR_NAME.ko"
rm -rf "/usr/src/$DRVDIR_NAME-1.0.0"
depmod -a "$KREL" 2>/dev/null || true

step "卸载用户态组件"
rm -f /usr/local/bin/gcclite \
      /usr/local/bin/gcclite-gui \
      /usr/local/libexec/gcclite-write \
      /usr/share/polkit-1/rules.d/45-gcclite.rules \
      /usr/local/share/applications/gcc-lite-gui.desktop

step "完成"
echo "    MOK 密钥（/var/lib/shim-signed/mok）保留未动，"
echo "    如需彻底清除：sudo mokutil --delete /var/lib/shim-signed/mok/MOK.der && 重启"
