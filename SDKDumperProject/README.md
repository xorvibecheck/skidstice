# SDKDumperProject

Standalone DLL project that dumps loaded modules, exports, and heuristically-extracted RTTI types for a running process (Windows only). Writes output to `%LOCALAPPDATA%/SDK_DUMP`.

Outputs:
- `sdk_dump.json` — JSON listing modules, exports, and RTTI type hits.
- `signatures.txt` — placeholder signatures (function-name based) and simple hex prefixes.
- `rtti_types.txt` — list of found RTTI-like type names and addresses.
 - `auto_names.idc` — IDA IDC script to auto-name exports and discovered vtables.
 - `ghidra_names.txt` — simple address-to-name map suitable for Ghidra import.


Build (Windows + CMake):

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

Notes:
- This project uses Windows APIs (`EnumProcessModules`, PE parsing) and therefore must be built on Windows.
- The RTTI extraction is heuristic-based (scans for typical MSVC RTTI mangled names). For best accuracy, run the dumper in the target process and supply real signatures if you have them.
- The build links to `dbghelp` to perform MSVC demangling (UnDecorateSymbolName).

Usage:
- Inject or load `SDKDumper.dll` into the target process (Minecraft Bedrock). The DLL will run once on load and write the dump files to `%LOCALAPPDATA%/SDK_DUMP`.
