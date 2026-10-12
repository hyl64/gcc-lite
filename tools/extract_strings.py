"""Extract US (user string) heap entries referenced by the WMI service methods, plus
the full method list of the app to locate the WMI invocation site."""
import dnfile

PATH = r"bin\GCC.dll"
dn = dnfile.dnPE(PATH)

us = dn.net.user_strings
# dnfile exposes user strings via get_us or heap; try common accessors
print("=== US heap accessors ===")
print([a for a in dir(us) if not a.startswith("_")][:30])

# Try to read all user strings by row
try:
    items = us.get(0)  # may not work
except Exception as e:
    items = None

# Offsets we care about (from IL anchors), user-string heap offsets
WANT_OFFSETS = [0x01, 0x0d, 0x33, 0x45, 0xaf, 0xc7, 0xd5, 0xe5]

print("\n=== 尝试按偏移读取 US 堆 ===")
for off in WANT_OFFSETS:
    for probe in (off, off * 1, off | 0x70000000, off << 0):
        try:
            v = us.get(probe)
            if v:
                print(f"  offset {off} (probe {probe}) -> {v!r}")
                break
        except Exception:
            continue

print("\n=== 直接扫描 US 堆原始内容 ===")
try:
    raw = us._data
    print("US heap size:", len(raw))
except Exception as e:
    print("no _data:", e)
