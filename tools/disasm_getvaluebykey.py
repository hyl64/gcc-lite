"""Disassemble a specific method's IL by RVA and resolve member references."""
import dnfile
import struct

PATH = r"bin\GCC.dll"
TARGET_RVA = 44012   # GetValueByKey

dn = dnfile.dnPE(PATH)

# Build memberref & typeref name maps
typerefs = {}
for i, t in enumerate(dn.net.mdtables.TypeRef.rows):
    ns = str(getattr(t.TypeNamespace, "value", "") or "")
    nm = str(getattr(t.TypeName, "value", "") or "")
    typerefs[i + 1] = (ns + "." + nm) if ns else nm

memberrefs = {}
for i, mr in enumerate(dn.net.mdtables.MemberRef.rows):
    nm = str(getattr(mr.Name, "value", "") or "")
    cls = getattr(mr, "Class", None)
    parent = "?"
    try:
        row = getattr(cls, "row", None)
        if row is not None:
            tn = str(getattr(row, "TypeName", "") or "")
            tn = str(getattr(tn, "value", tn))
            ns = str(getattr(row, "TypeNamespace", "") or "")
            ns = str(getattr(ns, "value", ns))
            parent = (ns + "." + tn) if ns else tn
        else:
            rid = getattr(cls, "row_index", None)
            tbl = getattr(cls, "table", None)
            if tbl is not None and "TypeRef" in str(tbl):
                parent = typerefs.get(rid, "?")
    except Exception:
        pass
    memberrefs[i + 1] = f"{parent}::{nm}"

# US heap for strings
us = dn.net.user_strings

def us_at(off):
    try:
        o = us.get(off)
        return o.value if hasattr(o, "value") else str(o)
    except Exception:
        return None

# Find the method body at TARGET_RVA via the MethodDef that has that RVA
target = None
for i, m in enumerate(dn.net.mdtables.MethodDef.rows):
    if getattr(m, "Rva", None) == TARGET_RVA:
        target = m
        break
if target is None:
    print("method not found at rva", TARGET_RVA)
    raise SystemExit

print(f"=== 方法: {target.Name.value if hasattr(target.Name,'value') else target.Name}  rva={TARGET_RVA} ===")
try:
    impl = target.ImplFlags
    flg = target.Flags
    iv = getattr(impl, "value", impl)
    fv = getattr(flg, "value", flg)
    print(f"    impl_flags={iv}  flags={fv}")
except Exception as e:
    print("    (flags 读取跳过:", e, ")")

# Read IL bytes: need file offset for RVA. Use section headers.
pe = dn
def rva_to_off(rva):
    for s in pe.sections:
        va = s.VirtualAddress
        sz = s.Misc_VirtualSize or s.SizeOfRawData
        if va <= rva < va + max(sz, s.SizeOfRawData):
            return s.PointerToRawData + (rva - va)
    return None

off = rva_to_off(TARGET_RVA)
print(f"    file offset: {off}")
with open(PATH, "rb") as f:
    f.seek(off)
    hdr = f.read(1)[0]
    if (hdr & 0x3) == 0x2:      # tiny
        code_size = hdr >> 2
        il = f.read(code_size)
        print(f"    tiny header, code size={code_size}")
    else:                        # fat
        f.seek(off)
        fat = f.read(12)
        flags = struct.unpack_from("<H", fat, 0)[0]
        hsize = (flags >> 12) * 4
        maxstack = struct.unpack_from("<H", fat, 2)[0]
        codesize = struct.unpack_from("<I", fat, 4)[0]
        f.seek(off + hsize)
        il = f.read(codesize)
        print(f"    fat header, hsize={hsize} maxstack={maxstack} codesize={codesize}")

print()
print("=== IL 操作码序列（含字符串/引用解析）===")
# 简化：只扫描 ldstr(0x72) / call(0x28) / callvirt(0x6f) / newobj(0x73) / ldsfld 等
i = 0
n = len(il)
while i < n:
    op = il[i]
    if op == 0x72:      # ldstr
        if i + 5 <= n:
            tok = struct.unpack_from("<I", il, i + 1)[0]
            off2 = tok & 0x00FFFFFF
            s = us_at(off2)
            print(f"  +{i:4d} ldstr    0x{tok:08X}  -> {s!r}")
            i += 5; continue
    if op in (0x28, 0x6f, 0x73):   # call / callvirt / newobj
        if i + 5 <= n:
            tok = struct.unpack_from("<I", il, i + 1)[0]
            name = memberrefs.get(tok & 0x00FFFFFF, "?")
            kind = {0x28: "call", 0x6f: "callvirt", 0x73: "newobj"}[op]
            print(f"  +{i:4d} {kind:8s} 0x{tok:08X}  -> {name}")
            i += 5; continue
    if op == 0x7b or op == 0x7c or op == 0x7d:  # ldfld/stfld
        i += 5; continue
    if op in (0x20, 0x7e, 0x80, 0x8c, 0xa5, 0xd0):  # 4-byte operands
        i += 5; continue
    if op in (0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45):
        i += 2; continue
    if op in (0xdd, 0xde, 0xdf, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x29, 0x2a):
        i += 1; continue
    if op == 0xfe:
        i += 1; continue
    i += 1
