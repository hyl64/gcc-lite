"""Disassemble ExcMethod (rva=8912) with full member/string resolution."""
import dnfile
import struct

PATH = r"bin\GCC.dll"
TARGET_RVA = 8912

dn = dnfile.dnPE(PATH)

# ---- TypeRef / TypeDef maps ----
typerefs = {}
for i, t in enumerate(dn.net.mdtables.TypeRef.rows):
    ns = str(getattr(t.TypeNamespace, "value", "") or "")
    nm = str(getattr(t.TypeName, "value", "") or "")
    typerefs[i + 1] = (ns + "." + nm) if ns else nm

typedefs = {}
for i, t in enumerate(dn.net.mdtables.TypeDef.rows):
    ns = str(getattr(t.TypeNamespace, "value", "") or "")
    nm = str(getattr(t.TypeName, "value", "") or "")
    typedefs[i + 1] = (ns + "." + nm) if ns else nm

memberrefs = {}
for i, mr in enumerate(dn.net.mdtables.MemberRef.rows):
    nm = str(getattr(mr.Name, "value", "") or "")
    parent = "?"
    try:
        cls = mr.Class
        row = getattr(cls, "row", None)
        if row is not None:
            tn = str(getattr(row, "TypeName", "") or "")
            tn = str(getattr(tn, "value", tn))
            ns = str(getattr(row, "TypeNamespace", "") or "")
            ns = str(getattr(ns, "value", ns))
            parent = (ns + "." + tn) if ns else tn
        else:
            rid = getattr(cls, "row_index", None)
            tbl = str(getattr(cls, "table", "") or "")
            if "TypeRef" in tbl:
                parent = typerefs.get(rid, "?")
            elif "TypeDef" in tbl:
                parent = typedefs.get(rid, "?")
    except Exception:
        pass
    memberrefs[i + 1] = f"{parent}::{nm}"

# methoddef names for direct calls
methoddefs = {}
for i, m in enumerate(dn.net.mdtables.MethodDef.rows):
    nm = str(getattr(m.Name, "value", "") or "")
    methoddefs[i + 1] = nm

us = dn.net.user_strings
def us_at(off):
    try:
        o = us.get(off)
        return o.value if hasattr(o, "value") else None
    except Exception:
        return None

def rva_to_off(rva):
    for s in dn.sections:
        va = s.VirtualAddress
        sz = max(s.Misc_VirtualSize, s.SizeOfRawData)
        if va <= rva < va + sz:
            return s.PointerToRawData + (rva - va)
    return None

off = rva_to_off(TARGET_RVA)
with open(PATH, "rb") as f:
    f.seek(off)
    hdr = f.read(1)[0]
    if (hdr & 0x3) == 0x2:
        il = f.read(hdr >> 2)
    else:
        f.seek(off)
        fat = f.read(12)
        flags = struct.unpack_from("<H", fat, 0)[0]
        hsize = (flags >> 12) * 4
        codesize = struct.unpack_from("<I", fat, 4)[0]
        f.seek(off + hsize)
        il = f.read(codesize)

print(f"=== ExcMethod IL (size={len(il)}) ===")
i, n = 0, len(il)
while i < n:
    op = il[i]
    if op == 0x72 and i + 5 <= n:            # ldstr
        tok = struct.unpack_from("<I", il, i + 1)[0]
        print(f"  +{i:4d} ldstr     -> {us_at(tok & 0xFFFFFF)!r}")
        i += 5; continue
    if op in (0x28, 0x6f, 0x73) and i + 5 <= n:  # call/callvirt/newobj
        tok = struct.unpack_from("<I", il, i + 1)[0]
        rid = tok & 0xFFFFFF
        tbl = (tok >> 24) & 0xFF
        if tbl == 0x0A:
            name = memberrefs.get(rid, "?")
        elif tbl == 0x06:
            name = "METHODDEF:" + methoddefs.get(rid, "?")
        elif tbl == 0x1B:
            name = "METHODSPEC:" + str(rid)
        else:
            name = f"tok 0x{tok:08X}"
        kind = {0x28: "call", 0x6f: "callvirt", 0x73: "newobj"}[op]
        print(f"  +{i:4d} {kind:8s} -> {name}")
        i += 5; continue
    if op in (0x7b, 0x7c, 0x7d, 0x7e, 0x80, 0x8c, 0xa5, 0xd0, 0x20, 0x22, 0x38, 0x39, 0x44, 0x8d, 0xa4, 0xfe):
        i += 5; continue
    if op in (0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x45, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x1f):
        i += 2; continue
    i += 1
