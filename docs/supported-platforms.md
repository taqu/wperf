# wperf Supported Platforms

Phase 0 platform baseline. Claims are based on source code, build configuration, and Windows API usage. Unknown items are marked **Needs verification**.

---

## Platform Support Matrix

| Platform | Architecture | Status | Notes |
|----------|--------------|--------|-------|
| Windows 11 | x64 | Confirmed — development environment | Primary development target |
| Windows 10 | x64 | Needs verification | README claims support; not independently tested |
| Windows 8.1 | x64 | Not supported | `GetIfTable2` requires Windows Vista+; `CreateDXGIFactory1` requires Windows Vista+; no practical reason to support |
| Windows 7 | x64 | Not supported | End of life; no test infrastructure |
| Windows 11 | x86 | Not supported | CMakeLists.txt does not configure x86 |
| Windows 10 | x86 | Not supported | CMakeLists.txt does not configure x86 |
| Linux | any | Not supported | Windows-only APIs throughout |
| macOS | any | Not supported | Windows-only APIs throughout |

---

## Compiler and Toolchain

| Property | Value | Notes |
|----------|-------|-------|
| Compiler | MSVC | Required; GCC/Clang not tested or configured |
| Minimum toolset | v143 (Visual Studio 2019) | Specified in GitHub Actions workflow |
| C++ standard | C++20 | Required (`CMAKE_CXX_STANDARD_REQUIRED ON`) |
| Build system | CMake | Minimum version 4.2.0 |

Cross-compilation is not supported. The build system generates Visual Studio project files via CMake and requires MSVC.

---

## Windows SDK

| Property | Value | Notes |
|----------|-------|-------|
| SDK version used in CI | 10.0.26100.0 | Confirmed from GitHub Actions runner |
| Declared minimum (`WINVER`) | 0x0601 (Windows Vista) | Set in CMakeLists.txt; may not reflect actual API minimum |
| Declared minimum (`_WIN32_WINNT`) | 0x0601 (Windows Vista) | Set in CMakeLists.txt |

**Note**: The declared `WINVER`/`_WIN32_WINNT` value of Vista (0x0601) is inconsistent with the SDK version used (10.0.26100.0). The actual minimum OS is higher than Vista due to APIs such as `GetIfTable2` (Vista+) and `IDXGIAdapter1`/`GetDesc1` (Windows 8+). The minimum Windows version claim of Windows 10 in the README is the most defensible statement based on API usage.

---

## GPU Requirements

| Property | Value | Notes |
|----------|-------|-------|
| GPU required for GPU metrics | DirectX 11 capable | Required for DXGI adapter enumeration |
| GPU required to run | No | GPU metrics section absent or zero when no DXGI adapter found |
| Software adapters | Excluded | `DXGI_ADAPTER_FLAG_SOFTWARE` adapters are skipped |
| Maximum GPU adapters displayed | 8 | Hard limit in `ResourceMonitor` slab allocation |

GPU metric availability depends on driver support for the PDH GPU Engine performance counters (`\GPU Engine(*)\Utilization Percentage`). These counters require Windows Display Driver Model (WDDM) 2.0 or later, which is standard on Windows 10 with modern drivers.

**Needs verification**: Behavior on systems with no physical GPU (virtual machines, integrated-only systems without WDDM 2.0 support).

---

## Privilege Requirements

| Scenario | Privilege Level |
|----------|----------------|
| Normal monitoring (CPU, RAM, GPU, disk, network) | Standard user |
| Memory purge (own processes and accessible user processes) | Standard user |
| Memory purge (system and protected processes) | May require elevation; silently fails otherwise |
| GPU PDH counters | Standard user (expected); **Needs verification** on all driver configurations |

The application does not contain a UAC manifest requesting elevation. It runs as a standard user by default.

---

## Known Limitations

- **x86 not supported**: Build system does not configure a 32-bit target.
- **Windows version claims unverified below Windows 11**: Windows 10 support is asserted in the README but not independently tested.
- **GPU metrics require WDDM 2.0+**: Absent on older hardware or in some virtual machine configurations.
- **Settings file location**: `wperf.ini` is written adjacent to `wperf.exe`. If the executable is installed in a write-protected directory (e.g., `Program Files`), settings persistence will fail silently. **Needs verification** in a typical installation scenario.
- **No installer**: The executable is distributed as a standalone file. No setup or uninstall mechanism exists.
