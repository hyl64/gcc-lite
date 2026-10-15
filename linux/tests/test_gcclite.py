#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gcclite 离线单元测试（纯标准库 unittest）

用法：
    python3 linux/tests/test_gcclite.py

通过 GCCLITE_SYSROOT 指向临时伪造的 sysfs 树，
在不动真实 /sys 的前提下覆盖 gcclite 的全部读路径与直写路径。
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent          # …/gcc-lite
CLI = REPO / "gcclite"

CONTROL_GUID = "B60BFB48-3E5B-49E4-A0E9-8CFFE1B3434B"
DRIVER_DIR = "bus/wmi/drivers/bitland-mifs-wmi"


def sh(args, root):
    """以伪造 sysfs 运行 CLI，返回 (rc, stdout, stderr)。"""
    env = dict(os.environ, GCCLITE_SYSROOT=str(root))
    proc = subprocess.run(
        [sys.executable, str(CLI)] + args,
        capture_output=True, text=True, env=env, timeout=30,
    )
    return proc.returncode, proc.stdout, proc.stderr


class FakeSysfs:
    """搭一个最小可用的伪 sysfs 树。"""

    def __init__(self, base):
        self.base = Path(base)
        self.profile = self.base / "class/platform_profile/platform-profile-0/profile"
        self.choices = self.base / "class/platform_profile/platform-profile-0/choices"
        self.hwm = self.base / "class/hwmon/hwmon9"
        self.dev = self.base / DRIVER_DIR / CONTROL_GUID
        self.make()

    def make(self):
        (self.base / "module/bitland_mifs_wmi").mkdir(parents=True, exist_ok=True)
        (self.base / "bus/wmi/devices" / CONTROL_GUID).mkdir(parents=True, exist_ok=True)
        self.dev.mkdir(parents=True, exist_ok=True)
        self.profile.parent.mkdir(parents=True, exist_ok=True)
        self.hwm.mkdir(parents=True, exist_ok=True)
        (self.base / "class/power_supply/ADP1").mkdir(parents=True, exist_ok=True)

        self.write(self.profile, "balanced\n")
        self.write(self.choices, "low-power balanced balanced-performance performance\n")
        self.write(self.hwm / "name", "bitland_mifs\n")
        self.write(self.hwm / "temp1_input", "65000\n")
        for i, (rpm, label) in enumerate(
                ((2400, "CPU"), (2600, "GPU"), (0, "SYS")), start=1):
            self.write(self.hwm / f"fan{i}_input", f"{rpm}\n")
            self.write(self.hwm / f"fan{i}_label", f"{label}\n")
        self.write(self.base / "class/power_supply/ADP1/type", "Mains\n")
        self.write(self.base / "class/power_supply/ADP1/online", "1\n")
        self.write(self.dev / "gpu_mode", "hybrid\n")
        self.write(self.dev / "kb_mode", "cyclic\n")
        self.write(self.dev / "fan_boost", "\n")

    def write(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def read(self, path):
        return Path(path).read_text(encoding="utf-8").strip()


class TestReads(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="gcclite-test-")
        self.fs = FakeSysfs(self.tmp)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_status_reports_everything(self):
        rc, out, _ = sh(["status"], self.tmp)
        self.assertEqual(rc, 0)
        for needle in ("已加载", "已绑定控制接口", "均衡模式",
                       "静音模式、均衡模式、性能模式、狂暴模式",
                       "65 °C", "2400 RPM", "2600 RPM", "已接入"):
            self.assertIn(needle, out, f"status 输出缺少 {needle!r}:\n{out}")

    def test_mode_get(self):
        rc, out, _ = sh(["mode"], self.tmp)
        self.assertEqual(rc, 0)
        self.assertIn("balanced", out)
        self.assertIn("均衡模式", out)

    def test_list_matches_protocol(self):
        rc, out, _ = sh(["list"], self.tmp)
        self.assertEqual(rc, 0)
        # 与 Windows 逆向出的四档一一对应
        self.assertIn("low-power", out)
        self.assertIn("balanced-performance", out)
        self.assertIn("performance", out)

    def test_fan_and_temp(self):
        rc, out, _ = sh(["fan"], self.tmp)
        self.assertEqual(rc, 0)
        self.assertIn("CPU: 2400 RPM", out)
        rc, out, _ = sh(["temp"], self.tmp)
        self.assertEqual(rc, 0)
        self.assertIn("65 °C", out)
        self.assertIn("bitland_mifs", out)

    def test_gpu_mode_and_kb_mode(self):
        rc, out, _ = sh(["gpu-mode"], self.tmp)
        self.assertEqual(rc, 0)
        self.assertEqual(out.strip(), "hybrid")
        rc, out, _ = sh(["kb-mode"], self.tmp)
        self.assertEqual(rc, 0)
        self.assertEqual(out.strip(), "cyclic")

    def test_invalid_mode_rejected(self):
        rc, _, err = sh(["mode", "warp-9"], self.tmp)
        self.assertEqual(rc, 2)
        self.assertIn("未知模式", err)

    def test_mode_not_in_choices_rejected(self):
        # 固件只报 low-power 一档时，请求其它档应被前置拦截
        self.fs.write(self.fs.choices, "low-power\n")
        rc, _, err = sh(["mode", "balanced"], self.tmp)
        self.assertEqual(rc, 1)
        self.assertIn("不支持", err)


class TestWrites(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="gcclite-test-")
        self.fs = FakeSysfs(self.tmp)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_set_mode_by_english_name(self):
        rc, out, _ = sh(["mode", "fullspeed"], self.tmp)
        self.assertEqual(rc, 0, out)
        self.assertIn("狂暴模式", out)
        self.assertEqual(self.fs.read(self.fs.profile), "performance")

    def test_set_mode_by_chinese_alias(self):
        for alias, expected in (("静音", "low-power"),
                                ("均衡", "balanced"),
                                ("性能", "balanced-performance"),
                                ("狂暴", "performance"),
                                ("全速", "performance")):
            with self.subTest(alias=alias):
                rc, out, _ = sh(["mode", alias], self.tmp)
                self.assertEqual(rc, 0, out)
                self.assertEqual(self.fs.read(self.fs.profile), expected)

    def test_set_kb_mode(self):
        rc, out, _ = sh(["kb-mode", "fixed"], self.tmp)
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.fs.read(self.fs.dev / "kb_mode"), "fixed")

    def test_invalid_kb_mode_rejected(self):
        rc, _, err = sh(["kb-mode", "disco"], self.tmp)
        self.assertEqual(rc, 2)

    def test_fan_boost_off(self):
        rc, out, _ = sh(["fan-boost", "off"], self.tmp)
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.fs.read(self.fs.dev / "fan_boost"), "0")


class TestNoDriver(unittest.TestCase):
    """驱动未加载时的降级行为。"""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="gcclite-nodrv-")
        # 只有 WMI 设备，没有驱动 / platform_profile
        (Path(self.tmp) / "bus/wmi/devices" / CONTROL_GUID).mkdir(parents=True)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_mode_get_fails_cleanly(self):
        rc, _, err = sh(["mode"], self.tmp)
        self.assertEqual(rc, 1)
        self.assertIn("驱动未就绪", err)

    def test_doctor_finds_gap(self):
        rc, out, _ = sh(["doctor"], self.tmp)
        self.assertEqual(rc, 1)
        self.assertIn("[✗]", out)
        self.assertIn("bitland-mifs-wmi 模块已加载", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
