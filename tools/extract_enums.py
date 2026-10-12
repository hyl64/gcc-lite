"""Extract enum literal values for BLD.WMIOperation enums from GCC.dll (corrected dnfile API)."""
import dnfile

PATH = r"bin\GCC.dll"

WANT = {
    "WMIMethodName", "WMIFanType", "WMISystemPerMode", "WMIMethodType",
    "WMIResultState", "WMIGPUMode", "WMIEventType", "WMIEventName",
    "WMISystemACType", "WMIKeyboardType", "WMIRGBKeyboardMode",
    "WMIRGBKeyboardBrightnessLevel", "WMIUnKnow",
}

dn = dnfile.dnPE(PATH)

# field row_index -> (type_name, field_name)
field_table = dn.net.mdtables.Field
field_owner = {}
for t in dn.net.mdtables.TypeDef.rows:
    tname = str(getattr(t.TypeName, "value", t.TypeName))
    for idx in t.FieldList:
        # FieldList entries may be MDTableIndex or resolved rows
        rid = getattr(idx, "row_index", None)
        frow = idx.row if hasattr(idx, "row") else (
            field_table.rows[rid - 1] if rid else None)
        if frow is None:
            continue
        fname = str(getattr(frow.Name, "value", frow.Name))
        field_owner[getattr(frow, "row_index", rid)] = (tname, fname)

results = {}
for c in dn.net.mdtables.Constant.rows:
    p = c.Parent
    if getattr(p.table, "name", None) != "Field":
        continue
    rid = p.row_index
    if rid not in field_owner:
        continue
    tname, fname = field_owner[rid]
    if tname not in WANT:
        continue
    v = c.Value
    # HeapItemBinary: get the parsed value
    val = getattr(v, "value", None)
    if isinstance(val, (bytes, bytearray)):
        try:
            val = int.from_bytes(val, "little", signed=(c.Type in (6,)))
        except Exception:
            val = val.hex()
    results.setdefault(tname, []).append((fname, val))

if not results:
    print("No enum constants matched. Dumping all Constant->Field pairs:")
    for c in dn.net.mdtables.Constant.rows:
        p = c.Parent
        if getattr(p.table, "name", None) == "Field":
            rid = p.row_index
            if rid in field_owner:
                print("  ", field_owner[rid], "=", getattr(c.Value, "value", None))
else:
    for tname in sorted(results):
        print(f"=== {tname} ===")
        for fname, val in sorted(results[tname], key=lambda x: (x[1] if isinstance(x[1], int) else -1)):
            print(f"  {fname:<36} = {val}")
        print()
