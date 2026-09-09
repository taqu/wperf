# Building wperf

Canonical developer build guide. All commands are run from the repository root.

---

## Build Requirements

| Tool | Version | Status |
|------|---------|--------|
| Windows | 10 or 11, x64 | Windows 11 confirmed; Windows 10 not independently tested |
| Visual Studio | 2022 (MSVC v143) | Confirmed |
| MSVC toolset | 14.4x (v143) | Confirmed — v14.44.35225.0 tested |
| C++ standard | C++20 | Required |
| Windows SDK | 10.0.26100.0 or later | Confirmed |
| CMake | 4.2.0 or later | Confirmed — 4.2.0 tested |

Visual Studio 2019 (v142) may work but has not been verified. The GitHub Actions workflow targets v143.

CMake must be installed separately or provided by Visual Studio's optional CMake component.

---

## Configure

Run once per fresh checkout or after changing `CMakeLists.txt`:

```
cmake -S . -B build -A x64
```

This generates a Visual Studio solution in `build/`.

---

## Build Debug

```
cmake --build build --config Debug
```

Output: `build/Debug/wperf.exe`

---

## Build Release

```
cmake --build build --config Release
```

Output: `build/Release/wperf.exe`

Tests are built by default as the separate `wperf_tests` target. Configure with
`-DBUILD_TESTING=OFF` to build only the application. See [testing.md](testing.md)
for CTest commands, coverage, and CI details.

The Lock Inspector core is built as `wperf_lock_inspector` and linked with the
Windows SDK's `Rstrtmgr.lib`; no additional SDK installation or downloaded
dependency is needed. Real Restart Manager tests are opt-in with
`-DWPERF_BUILD_INTEGRATION_TESTS=ON` and require `BUILD_TESTING=ON`.

---

## Clean Build

To rebuild from scratch:

```
rmdir /s /q build
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
```

On Unix-style shells (Git Bash, WSL):

```
rm -rf build
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
```

---

## Runtime Dependencies

`wperf.exe` depends on the following DLLs at runtime:

| DLL | Source | Notes |
|-----|--------|-------|
| `GDI32.dll` | Windows | Graphics |
| `IPHLPAPI.DLL` | Windows | Network interface enumeration |
| `COMCTL32.dll` | Windows | Common controls |
| `dxgi.dll` | Windows / DirectX | GPU enumeration |
| `pdh.dll` | Windows | Performance counters |
| `KERNEL32.dll` | Windows | Core OS |
| `USER32.dll` | Windows | Window management |
| `VCRUNTIME140.dll` | Visual C++ Redistributable 2015–2022 | MSVC runtime |
| `api-ms-win-crt-*.dll` | Windows (Universal CRT) | C runtime forwarding stubs |

**No files need to be copied beside `wperf.exe`** on a standard Windows 10 or Windows 11 installation. The Universal CRT (`api-ms-win-crt-*.dll`) is part of Windows 10+. `VCRUNTIME140.dll` ships with Windows 10/11 and is also included with any Visual C++ 2015–2022 Redistributable installation.

If deploying to a minimal or stripped Windows environment that lacks the VC++ Redistributable, `VCRUNTIME140.dll` must be provided.

---

## Build Warnings

The following warnings are present in the current codebase and are non-blocking:

| Warning | File | Description |
|---------|------|-------------|
| C4267 | `src/purge_memory.cpp:22` | `size_t` to `DWORD` narrowing; value is bounded by `MaxProcesses` (1024), so no actual data loss |

---

## Troubleshooting

### CMake cannot find a compiler

Ensure Visual Studio 2022 is installed with the **Desktop development with C++** workload. Run `cmake` from a normal shell — the VS generator is auto-detected and does not require a developer command prompt.

### `generator platform` error on reconfigure

A stale `CMakeCache.txt` from a previous run without `-A x64` is present. Delete `build/` and reconfigure:

```
rmdir /s /q build
cmake -S . -B build -A x64
```

### Wrong architecture selected

Always pass `-A x64` explicitly. Without it, the default platform depends on the host environment, which may produce a 32-bit configuration on some setups.

### SDK version not found

CMake auto-selects the newest installed Windows SDK. If the SDK component is missing, install it from the Visual Studio Installer under **Individual components → Windows 10 SDK (10.0.26100.0)** or any later version.

### Settings file not writable

`wperf.ini` is written next to `wperf.exe`. If the executable is placed in a write-protected directory such as `C:\Program Files\`, the settings file cannot be created and settings will not persist. Run from a user-writable location or run as administrator.
