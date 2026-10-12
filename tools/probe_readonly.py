"""
只读探针：通过 root\WMI : MICommonInterface.MiInterface 读取硬件状态。
不执行任何 Set（写）操作。
"""
import subprocess

NS = r"root\WMI"
CLS = "MICommonInterface"
INST = r"ACPI\PNP0C14\MIFS_0"

METHOD_NAMES = {
    8: "SystemPerMode", 9: "GPUMode", 10: "KeyboardType", 11: "FnLock",
    12: "TPLock", 13: "CPUGPUSYSFanSpeed", 19: "SystemAcType",
    20: "MaxFanSpeedSwitch", 21: "MaxFanSpeed", 22: "CPUThermometer",
    23: "CPUPower",
}
GET = 250


def build_in_data(method_name, value=0):
    buf = bytearray(32)
    buf[2] = GET
    buf[3] = method_name
    buf[4] = value
    return list(buf)


def query(method_name):
    """Return OutData byte list, or None."""
    in_data = build_in_data(method_name)
    in_lit = ",".join(str(b) for b in in_data)
    ps = f"""
$ErrorActionPreference='Stop'
$in = [byte[]]@({in_lit})
$out = New-Object byte[] 32
$o = Get-CimInstance -Namespace '{NS}' -ClassName '{CLS}' -Filter "InstanceName='{INST}'"
$r = Invoke-CimMethod -InputObject $o -MethodName 'MiInterface' `
      -Arguments @{{ InData = $in; OutData = $out }}
$bytes = @()
if ($r.OutData) {{ $bytes = $r.OutData }}
"OK:" + (($bytes | ForEach-Object {{ $_.ToString() }}) -join ',')
"""
    p = subprocess.run(["pwsh", "-NoProfile", "-Command", ps],
                       capture_output=True, text=True, timeout=60)
    out = (p.stdout or "").strip()
    if out.startswith("OK:"):
        s = out[3:].strip()
        if not s:
            return None
        return [int(x) for x in s.split(",") if x.strip()]
    print("   stderr:", (p.stderr or "").strip()[:200])
    return None


print(f"Target: {NS} : {CLS}  instance {INST}\n")
print(f"{'Method':<22}{'Id':<5}{'OutData[0:8]':<28}{'值'}")
print("-" * 70)

for mid, name in METHOD_NAMES.items():
    r = query(mid)
    if r is None:
        print(f"{name:<22}{mid:<5}{'<no data>':<28}")
        continue
    head = ",".join(str(b) for b in r[:8])
    val = r[4] if len(r) > 4 else None
    print(f"{name:<22}{mid:<5}{head:<28}{val}")
