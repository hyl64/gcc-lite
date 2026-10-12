"""Enumerate TypeRef/TypeDef tables with correct dnfile API."""
import dnfile

PATH = r"bin\GCC.dll"
dn = dnfile.dnPE(PATH)

print("=== TypeRef rows (Management 相关) ===")
for i, t in enumerate(dn.net.mdtables.TypeRef.rows):
    ns = str(getattr(t.TypeNamespace, "value", "") or "")
    nm = str(getattr(t.TypeName, "value", "") or "")
    if any(s in nm for s in ("Management", "ObjectQuery", "ManagementScope",
                             "ManagementSearcher", "ManagementObject")):
        print(f"  row {i+1} (0x01{i+1:06x})  {ns}.{nm}")

print()
print("=== TypeRef rows (全部前 60) ===")
for i, t in enumerate(dn.net.mdtables.TypeRef.rows):
    if i >= 60: break
    ns = str(getattr(t.TypeNamespace, "value", "") or "")
    nm = str(getattr(t.TypeName, "value", "") or "")
    print(f"  0x01{i+1:06x}  {ns}.{nm}")
