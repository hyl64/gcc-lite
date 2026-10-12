# Reverse-engineering tools

Scripts used to recover the `MICommonInterface` protocol from the vendor binary
`斗战者控制台.dll`. Kept for documentation and reproduction — they are **not**
needed to build or run GCC Lite.

## Toolchain used

| Tool | Purpose |
|------|---------|
| [REA](https://github.com/morluto/rea) | MCP/CLI reverse-engineering agent (managed .NET analysis) |
| `dnfile` (pip) | .NET metadata parsing (enums, strings, IL) |
| MSVC `csc` / Roslyn | compiling C# probes |
| Ghidra 12.1.4 | tried, but **cannot analyse managed .NET PE** on Windows |

## Files

### Metadata extraction (Python + dnfile)

| File | What it does |
|------|--------------|
| `extract_enums.py` | Pulls enum literal values from the Constant table — this is how the `WMIMethodName` / `WMISystemPerMode` / `WMIFanType` values were recovered |
| `extract_strings.py` | Reads the .NET user-string heap (found the WMI class/instance path strings) |
| `resolve_types.py` | Resolves TypeRef / TypeDef rows to type names |
| `disasm_excmethod.py` | Disassembles `ExcMethod` (the actual hardware call site) |
| `disasm_getvaluebykey.py` | Disassembles the generic WMI query helper |
| `disasm_packed.py` | Disassembles `_MakeMethodPrams` — **this revealed the 32-byte buffer layout** |

### Runtime probes (PowerShell / Python)

| File | What it does |
|------|--------------|
| `probe_readonly.py` | Read-only probe of all MiInterface Get methods |

### C++ / C# diagnostics

| File | What it does |
|------|--------------|
| `diag.cpp` … `diag6.cpp` | Step-by-step WMI invocation diagnostics (HRESULT tracing, path-escaping matrix, parameter-construction matrix) |
| `mini*.cpp` | Minimal reproducers (PDH buffer sizing, RAPL unit conversion) |
| `MiClient.cs`, `Exact.cs`, `Esc.cs`, `PathProbe.cs`, `Devices.cs` | C# probes replicating the vendor's exact call sequence |
| `MiApiTest.cs` | MI API (CIM) probe |

## Key results

1. **No kernel driver involved.** The vendor app's entire P/Invoke surface is
   8 imports, all `user32` keyboard hooks plus `kernel32!GetSystemPowerStatus`.
   Hardware control is 100 % ACPI-WMI.

2. **The 32-byte request buffer** (from `_MakeMethodPrams` IL):

   ```
   buf[1] = methodType   // 250 Get / 251 Set
   buf[3] = methodName
   buf[4] = value
   ```

   Off-by-one here (using `buf[2]`) yields `0x8004100F WBEM_E_NOT_SUPPORTED`.

3. **The WMI path** must use single quotes and single backslashes:

   ```
   root\WMI  +  MICommonInterface.InstanceName='ACPI\PNP0C14\MIFS_0'
   ```

## Reproducing

```powershell
pip install dnfile

# recover enum values
python extract_enums.py

# see the buffer layout
python disasm_packed.py
```

Both scripts expect the vendor binary at `bin/GCC.dll`
(a copy of `斗战者控制台.dll`; not redistributed here for licensing reasons).
