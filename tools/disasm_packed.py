"""Full IL dump of _MakeMethodPrams, GetValue, SetValue with all operands resolved."""
import dnfile, struct

PATH = r"bin\GCC.dll"
dn = dnfile.dnPE(PATH)

typerefs = {}
for i, t in enumerate(dn.net.mdtables.TypeRef.rows):
    ns = str(getattr(t.TypeNamespace, "value", "") or "")
    nm = str(getattr(t.TypeName, "value", "") or "")
    typerefs[i+1] = (ns + "." + nm) if ns else nm

memberrefs = {}
for i, mr in enumerate(dn.net.mdtables.MemberRef.rows):
    nm = str(getattr(mr.Name, "value", "") or "")
    parent = "?"
    try:
        cls = mr.Class
        row = getattr(cls, "row", None)
        if row is not None:
            tn = str(getattr(row, "TypeName", "") or ""); tn = str(getattr(tn, "value", tn))
            ns = str(getattr(row, "TypeNamespace", "") or ""); ns = str(getattr(ns, "value", ns))
            parent = (ns + "." + tn) if ns else tn
        else:
            rid = getattr(cls, "row_index", None)
            tbl = str(getattr(cls, "table", "") or "")
            if "TypeRef" in tbl: parent = typerefs.get(rid, "?")
    except Exception:
        pass
    memberrefs[i+1] = f"{parent}::{nm}"

methoddefs = {}
for i, m in enumerate(dn.net.mdtables.MethodDef.rows):
    methoddefs[i+1] = str(getattr(m.Name, "value", "") or "")

us = dn.net.user_strings
def us_at(off):
    try:
        o = us.get(off); return o.value if hasattr(o, "value") else None
    except Exception:
        return None

def rva_to_off(rva):
    for s in dn.sections:
        va = s.VirtualAddress
        sz = max(s.Misc_VirtualSize, s.SizeOfRawData)
        if va <= rva < va + sz:
            return s.PointerToRawData + (rva - va)
    return None

def read_il(rva):
    off = rva_to_off(rva)
    with open(PATH, "rb") as f:
        f.seek(off)
        hdr = f.read(1)[0]
        if (hdr & 0x3) == 0x2:
            return f.read(hdr >> 2)
        f.seek(off)
        fat = f.read(12)
        flags = struct.unpack_from("<H", fat, 0)[0]
        hsize = (flags >> 12) * 4
        codesize = struct.unpack_from("<I", fat, 4)[0]
        f.seek(off + hsize)
        return f.read(codesize)

OPS1 = {0x00:"nop",0x01:"break",0x02:"ldarg.0",0x03:"ldarg.1",0x04:"ldarg.2",0x05:"ldarg.3",
        0x06:"ldloc.0",0x07:"ldloc.1",0x08:"ldloc.2",0x09:"ldloc.3",0x0a:"stloc.0",0x0b:"stloc.1",
        0x0c:"stloc.2",0x0d:"stloc.3",0x14:"ldnull",0x15:"ldc.i4.m1",0x16:"ldc.i4.0",0x17:"ldc.i4.1",
        0x18:"ldc.i4.2",0x19:"ldc.i4.3",0x1a:"ldc.i4.4",0x1b:"ldc.i4.5",0x1c:"ldc.i4.6",0x1d:"ldc.i4.7",
        0x1e:"ldc.i4.8",0x25:"dup",0x26:"pop",0x2a:"ret",0x58:"add",0x59:"sub",0x5a:"mul",
        0x5b:"div",0x5c:"div.un",0x5f:"and",0x60:"or",0x61:"xor",0x62:"shl",0x63:"shr",
        0x64:"shr.un",0x66:"not",0x67:"conv.i1",0x68:"conv.i2",0x69:"conv.i4",0x6a:"conv.i8",
        0x6b:"conv.r4",0x6c:"conv.r8",0x6d:"conv.u4",0x6e:"conv.u8",0x2b:"br.s",0x2c:"brfalse.s",
        0x2d:"brtrue.s",0x2e:"beq.s",0x2f:"bge.s",0x30:"bgt.s",0x31:"ble.s",0x32:"blt.s",
        0x33:"bne.un.s",0x34:"bge.un.s",0x35:"bgt.un.s",0x36:"ble.un.s",0x37:"blt.un.s",
        0x38:"br",0x39:"brfalse",0x3a:"brtrue",0x3b:"beq",0x3c:"bge",0x3d:"bgt",0x3e:"ble",
        0x3f:"blt",0x40:"bne.un",0x41:"bge.un",0x42:"bgt.un",0x43:"ble.un",0x44:"blt.un",
        0x46:"ldind.i1",0x47:"ldind.u1",0x48:"ldind.i2",0x49:"ldind.u2",0x4a:"ldind.i4",
        0x4b:"ldind.u4",0x5e:"ldind.ref",0x6f:"callvirt",0x28:"call",0x73:"newobj",0x74:"castclass",
        0x75:"isinst",0x8c:"box",0x79:"unbox",0xa5:"unbox.any",0x7b:"ldfld",0x7c:"ldflda",
        0x7d:"stfld",0x8e:"ldlen",0x9a:"ldelem.ref",0x9c:"stelem.i4",0xa2:"stelem.ref",
        0xd0:"ldtoken",0x72:"ldstr",0x80:"stsfld",0x7e:"ldsfld",0xde:"leave.s"}
OPS_FE = {0x01:"ceq",0x02:"cgt",0x04:"clt",0x06:"ldftn",0x09:"ldarg",0x0a:"ldarga",
          0x0c:"starg",0x0e:"ldloc",0x0f:"ldloca",0x11:"stloc",0x15:"initobj",0x16:"constrained"}

def dump(name, rva):
    il = read_il(rva)
    print(f"\n{'='*70}\n{name}  (rva={rva}, size={len(il)})\n{'='*70}")
    i, n = 0, len(il)
    while i < n:
        op = il[i]
        if op == 0xfe and i+1 < n:
            op2 = il[i+1]
            nm = OPS_FE.get(op2, f"fe{op2:02x}")
            if op2 in (0x09,0x0a,0x0c,0x0e,0x0f,0x11,0x15):
                val = struct.unpack_from("<H", il, i+2)[0]
                print(f"  +{i:4d} {nm:14s} {val}")
                i += 4; continue
            i += 2; continue
        nm = OPS1.get(op)
        if nm == "ldstr" and i+5 <= n:
            tok = struct.unpack_from("<I", il, i+1)[0]
            print(f"  +{i:4d} ldstr          {us_at(tok & 0xFFFFFF)!r}")
            i += 5; continue
        if nm in ("call","callvirt","newobj","ldtoken","ldfld","ldflda","stfld","ldsfld","stsfld",
                  "castclass","isinst","box","unbox","unbox.any","initobj") and i+5 <= n:
            tok = struct.unpack_from("<I", il, i+1)[0]
            rid = tok & 0xFFFFFF; tbl = (tok >> 24) & 0xFF
            target = memberrefs.get(rid, "?") if tbl == 0x0A else \
                     ("MDEF:" + methoddefs.get(rid, "?")) if tbl == 0x06 else \
                     ("TYPEDEF:" + str(rid)) if tbl == 0x02 else \
                     ("TYPESPEC:" + str(rid)) if tbl == 0x1B else f"0x{tok:08X}"
            extra = ""
            if nm in ("call","newobj") and tbl == 0x0A and rid in memberrefs:
                s = memberrefs[rid]
                if "op_Implicit" in s or "op_Explicit" in s: extra = "   <-- 可能含隐式转换"
            print(f"  +{i:4d} {nm:14s} {target}{extra}")
            i += 5; continue
        if nm in ("ldc.i4", "ldc.i4.s") and i+5 <= n:
            v = struct.unpack_from("<i", il, i+1)[0]
            print(f"  +{i:4d} {nm:14s} {v}")
            i += 5; continue
        if nm in ("br.s","brfalse.s","brtrue.s","beq.s","bgt.s","blt.s","bne.un.s","ble.s","bge.s") and i+2 <= n:
            d = struct.unpack_from("<b", il, i+1)[0]
            print(f"  +{i:4d} {nm:14s} -> +{i+2+d}")
            i += 2; continue
        if nm in ("br","brfalse","brtrue","beq","bne.un","bgt","blt","leave.s") and i+5 <= n:
            d = struct.unpack_from("<i", il, i+1)[0]
            print(f"  +{i:4d} {nm:14s} -> +{i+5+d}")
            i += 5; continue
        if nm:
            print(f"  +{i:4d} {nm}")
        i += 1

for nm in ("_MakeMethodPrams",):
    for i, m in enumerate(dn.net.mdtables.MethodDef.rows):
        n2 = str(getattr(m.Name, "value", "") or "")
        if n2 == nm:
            dump(n2, m.Rva)
