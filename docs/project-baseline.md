# wperf Project Baseline

Repository baseline. Last updated 2026-09-09 (Phase 8 native handle fallback).

---

## Project Summary

`wperf` is a lightweight Windows desktop performance overlay. It renders live system metrics (CPU, RAM, GPU, disk, network) as a borderless dark-themed window positioned at the bottom of the Z-order (behind all open windows). Configuration and memory purge are available via a right-click context menu.

The project is a single-executable Windows GUI application with no external runtime dependencies beyond the Windows SDK.

---

## Repository Structure

```
wperf/
├── .github/
│   └── workflows/
│       └── release.yml          # GitHub Actions: build and upload on version tag push
├── .gitignore
├── _clang-format                # Chromium-based clang-format style configuration
├── CMakeLists.txt               # CMake build definition (minimum version 4.2.0)
├── LICENSE                      # MIT License
├── README.md                    # User-facing readme
├── wperf.md                     # Phase 0 audit specification
├── doc/
│   └── ss00.jpg                 # Desktop overlay screenshot
├── docs/                        # Phase 0 documentation (this directory)
├── include/
│   ├── resource_monitor.h       # ResourceMonitor class interface
│   └── purge_memory.h           # Memory purge utility interface
├── resource/
│   ├── icon.ico                 # Application icon
│   ├── resource.h               # Resource identifiers (Visual Studio generated)
│   ├── resource.rc              # Resource script
│   └── resource.aps             # Visual Studio binary resource editor state
└── src/
    ├── main.cpp                 # Entry point, window management, UI rendering (~998 lines)
    ├── resource_monitor.cpp     # System metrics collection (~340 lines)
    └── purge_memory.cpp         # Memory purge implementation (~37 lines)
```

**Confirmed**: The `build/` directory is excluded by `.gitignore`. CMake-generated files (`.sln`, `.vcxproj`) are not tracked.

---

## Architecture

### High-Level Overview

```
main.cpp (WinMain)
  ├── ResourceMonitor        ← metrics collection (PDH, DXGI, WinAPI)
  ├── Window rendering       ← double-buffered GDI
  ├── Settings dialog        ← INI file persistence
  └── Memory purge dialog    ← on-demand, batched EmptyWorkingSet
```

### Lock Inspector core (Phase 6)

`include/lock_inspector.h` exposes synchronous `InspectLocks(path)` with
project-owned process records and structured status/native errors. The static
CMake target `wperf_lock_inspector` compiles `src/lock_inspector.cpp`, links
`Rstrtmgr.lib`, and is linked by the application and tests. A private
`src/lock_inspector_internal.h` contains the narrow backend test seam.

Discovery uses Restart Manager sessions with RAII cleanup and bounded list
retries. Phase 7 adds `wperf.exe --lock <absolute-path> [--json]` and `--help` through a small CLI frontend. Phase 8 adds explicit `--deep`: Restart Manager first, then a bounded native handle snapshot and result merge. Directory handles/descendants, Unicode path normalization and partial results are supported; protected processes remain limited. Native scanning is dormant unless requested. Phase 9 adds a native Win32 GUI at `wperf.exe --lock-ui [path]`; it uses one temporary worker per explicit scan and does not initialize the desktop monitor. Explorer integration, process termination, and remote handle closing remain absent.
Files and directories are accepted as absolute wide paths; directory discovery
is limited and does not recurse. Inactive inspection adds zero threads, timers,
polling, or process scans. See [lock-inspector.md](lock-inspector.md).

### Key Design Properties

- The desktop monitor is single-threaded and driven by `SetTimer`; Lock Inspector GUI scans use one temporary worker only while requested.
- Borderless popup window (`WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX`).
- Positioned at `HWND_BOTTOM` (behind all windows) or `HWND_TOPMOST` per user setting.
- Updates suspended when window is minimized (`IsIconic` check).
- Background process priority set at startup (`PROCESS_MODE_BACKGROUND_BEGIN`).
- No system tray icon. Interaction via right-click context menu only.
- All fixed arrays stored in a single 4 KB virtual allocation slab (`VirtualAlloc`).

### Core Design Policy

Features requiring non-trivial CPU usage, memory usage, handle enumeration, process inspection, or other expensive system operations must be activated on demand and must not introduce unnecessary background polling when inactive. The memory purge feature follows this policy. Lock Inspector follows the same policy; its native scan and temporary I/O watchdog run only with explicit deep inspection.

---

## Build System

**Confirmed**

| Property | Value |
|----------|-------|
| Build tool | CMake |
| Minimum CMake version | 4.2.0 |
| Compiler | MSVC (Visual C++) |
| Minimum toolset | v143 (Visual Studio 2019) |
| C++ standard | C++20 (required) |
| Architecture | x64 |
| Windows SDK | 10.0.26100.0 (configured in GitHub Actions) |
| Unicode | Yes (`UNICODE`, `_UNICODE`) |
| Windows version target | Vista+ (`WINVER=0x0601`, `_WIN32_WINNT=0x0601`) |

**Build commands (canonical):**
```
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
```

See `docs/build.md` for the complete build guide.

**Output:** `build/Debug/wperf.exe`, `build/Release/wperf.exe`

**Compiler diagnostics (all configurations):** `/W4 /WX /permissive- /utf-8`

**Release compiler flags:** `/O2 /Oi /Gy /GL` (maximum optimization, whole-program optimization)

**Release linker flags:** `/OPT:REF /OPT:ICF /LTCG` (dead code elimination, identical COMDAT folding, link-time code generation)

**x86 support:** Not configured. CMakeLists.txt has no x86 target. Assumed unsupported.

---

## External Dependencies

Production dependencies remain Windows SDK libraries. Tests additionally use the vendored doctest 2.4.12 header and MIT license; no download is needed at build time.

| Library | Purpose | Source |
|---------|---------|--------|
| `gdi32` | Window rendering, fonts, drawing | Windows |
| `msimg32` | `AlphaBlend` for transparency effects | Windows |
| `iphlpapi` | Network interface enumeration (`GetIfTable2`) | Windows |
| `sapi` | Speech API | Windows (see note below) |
| `comctl32` | Common controls, visual styles | Windows |
| `dxgi` | GPU adapter enumeration via DXGI | Windows / DirectX |
| `pdh` | Performance Data Helper counters (disk, GPU) | Windows |
| `Rstrtmgr` | Lock Inspector core discovery (Phase 6) | Windows |

**Note**: `msimg32` and `sapi` were originally linked but no call sites exist in the source. Both were removed from `CMakeLists.txt` in Phase 1. Confirmed by build and dumpbin dependency check.

---

## Runtime Dependencies

| Component | API Used | Notes |
|-----------|----------|-------|
| CPU monitoring | `GetSystemTimes()` | Standard; no elevation needed |
| RAM monitoring | `GlobalMemoryStatusEx()` | Standard; no elevation needed |
| GPU monitoring | `CreateDXGIFactory1`, `IDXGIAdapter1`, PDH GPU Engine counters | Requires DirectX 11 GPU; gracefully degrades |
| Disk I/O monitoring | PDH `\PhysicalDisk(_Total)\Disk Read/Write Bytes/sec` | Standard PDH query |
| Network monitoring | `GetIfTable2()` | Standard; filters loopback and disconnected interfaces |
| UI rendering | GDI (`CreateCompatibleDC`, `BitBlt`, `DrawTextW`, etc.) | Double-buffered; dark theme |
| Fonts | Segoe UI 11pt and 14pt with ClearType | Requires Segoe UI font installed |
| Settings storage | `GetPrivateProfileIntW` / `WritePrivateProfileStringW` | Writes `wperf.ini` next to executable |
| Tray / notification area | Not used | No system tray icon |
| Memory purge | `EnumProcesses`, `EmptyWorkingSet` | On-demand; standard user; may silently fail on protected processes |
| DPI awareness | `SetProcessDPIAware` | High DPI support |
| Window management | Win32 `CreateWindowExW`, `TrackPopupMenu` | Standard |

---

## Privilege Requirements

**Confirmed**: The application does not request administrator privileges. No UAC manifest is present.

- All monitoring APIs run under a standard user account.
- Memory purge (`EmptyWorkingSet`) uses `PROCESS_QUERY_INFORMATION | PROCESS_SET_QUOTA`. Opening handles to system-owned or protected processes will silently fail.
- Running as administrator would allow more complete memory purge coverage, but is not required.

**Needs verification**: Whether any GPU PDH counters require elevated access on specific Windows configurations.

---

## Current Features

| Feature | Status |
|---------|--------|
| CPU % | Confirmed |
| RAM % + used/available GB | Confirmed |
| GPU per-adapter 3D utilization % | Confirmed |
| GPU per-adapter VRAM used | Confirmed |
| Disk read/write throughput | Confirmed |
| Network download/upload speed | Confirmed |
| Live clock in header | Confirmed |
| Window position persistence | Confirmed |
| Update interval setting (250–60,000 ms) | Confirmed |
| Always-on-top toggle | Confirmed |
| On-demand memory purge | Confirmed |
| Right-click context menu | Confirmed |
| System tray icon | Not present |
| Lock Inspector | Core, CLI, native deep scan, and GUI implemented; process control and Explorer integration absent |

---

## Settings and Storage

Settings are stored in `wperf.ini` located in the same directory as `wperf.exe`.

| INI Key | Section | Default | Description |
|---------|---------|---------|-------------|
| `PositionX` | `[Window]` | 0 | Window X position (pixels) |
| `PositionY` | `[Window]` | 0 | Window Y position (pixels) |
| `UpdateInterval` | `[Settings]` | 1000 | Metric refresh rate in ms |
| `AlwaysOnTop` | `[Settings]` | 0 | 1 = topmost, 0 = behind all windows |

---

## Current Test Status

Phase 5 uses doctest 2.4.12 with CTest. The initial audit found no existing
unit/integration tests, test executables, harnesses, or smoke-test scripts to
maintain or integrate. CI previously performed build/artifact checks only.

`tests/CMakeLists.txt` builds `wperf_tests` by default (`BUILD_TESTING=OFF` disables
it). CTest registers `wperf.unit` with the `unit` label and a 30-second timeout.
The 56 doctest cases include 12 native/deep cases, eight CLI cases, three GUI presentation cases, the original 18 formatting/settings/CPU cases and
15 Lock Inspector cases covering validation, errors, conversion, deduplication,
races, retries, and session cleanup. Small inline helpers extracted into
`include/app_logic.h` are shared by production and tests.

```powershell
ctest --test-dir build -C Debug --output-on-failure --no-tests=error
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

Phase 6 clean validation results and commands are recorded in [testing.md](testing.md).
Mandatory unit tests use only in-memory inputs. Mandatory CLI contract tests launch the executable and check help, invalid usage and nonexistent-path errors with strict JSON parsing. The separate
`wperf_lock_integration_tests` target has seven controlled-resource cases (three Restart Manager and four native) and
requires `WPERF_BUILD_INTEGRATION_TESTS=ON`. It carries the `integration` label
and is excluded from default CI pending GitHub-runner verification.

Major gaps: INI persistence/missing-key handling, executable path construction,
UI interactions, live metrics and GPU hardware, startup/shutdown, and memory
purge. Raw Win32 Lock Inspector controls remain manually tested; presentation conversion, sorting, statuses, and GUI argument parsing are unit tested. Native handle and deep-CLI integration tests are opt-in pending hosted-runner verification. CLI contract tests run by default; held/released-resource CLI tests are opt-in with the existing integration option.
See [testing.md](testing.md) for exact validation commands and isolation details.

---

## Current CI Status

**Provider**: GitHub Actions

| Item | Status |
|------|--------|
| PR build verification | Present — `.github/workflows/ci.yml` |
| Main branch build verification | Present — `.github/workflows/ci.yml` |
| Debug build CI | Present — `.github/workflows/ci.yml` |
| Release build CI (tag-triggered) | Present — `.github/workflows/release.yml` |
| Test CI | Debug + Release unit suite, including Lock Inspector, in `ci.yml` |
| Lint / static analysis | Not present |
| Release artifact checksums | Not present |

**`ci.yml`** triggers on every push to `main` and on all pull requests. It runs on `windows-latest`, configures with `cmake -S . -B build -A x64`, builds and tests Debug and Release, and verifies both executables exist.

**`release.yml`** triggers on `v*.*.*` tag pushes. It builds Release and uploads `build/Release/wperf.exe` to the GitHub Release.

---

## Current Release Infrastructure

| Item | Status |
|------|--------|
| GitHub Actions release workflow | Present |
| Semantic versioning | Not implemented |
| Version resource in executable | Not present |
| Version constant in source | Not present |
| `--version` CLI support | Not present |
| Git release tags | Needs verification |
| Release artifact naming convention | Not defined |
| Release checksums | Not present |
| Installer | Not present |

---

## Documentation Gaps

| Document | Status |
|----------|--------|
| `docs/project-baseline.md` | Created in Phase 0 (this file) |
| `docs/supported-platforms.md` | Created in Phase 0 |
| `docs/release-policy.md` | Created in Phase 0 |
| Architecture overview | Covered in this file |
| Troubleshooting guide | Missing |
| Contribution workflow | Missing |
| Windows API reference | Missing |
| Lock Inspector design constraints | Covered in `wperf.md` |

The current README covers: features, settings, requirements, build instructions, and license. It does not cover: architecture, troubleshooting, or contribution workflow.

---

## Technical Risks

| Risk | Severity | Notes |
|------|----------|-------|
| No version metadata in executable | Medium | Users and support cannot determine installed version |
| `WINVER=0x0601` (Vista) but SDK 10.0.26100.0 | Low | Mismatch between declared and actual minimum; needs platform testing |
| GitHub-hosted Phase 6 CI unverified | Low | Workflow updated; remote execution still needs verification |
| Limited automated tests | Medium | Pure helpers covered; Windows integration and UI still need manual verification |
| `SetProcessDPIAware` (old API) | Low | Superseded by manifest-based DPI awareness; functional but not ideal |
| Memory purge silently fails on protected processes | Low | Expected behavior; no user-visible error reporting |
| GPU metrics absent without DirectX 11 GPU | Low | Code appears to degrade gracefully; not tested on systems without GPU |

---

## Unknowns Requiring Verification

1. ~~Whether `sapi` is actually used anywhere~~ — Confirmed unused; removed from build (Phase 1).
2. Git remote URL and release tag history.
3. Whether x86 builds are possible at all (no architecture guard in CMakeLists.txt; `-A x64` now required explicitly).
4. Behavior on systems without a DirectX 11-capable GPU.
5. GPU PDH counter availability requirements (elevation, driver version).
6. Whether `wperf.ini` adjacent to `wperf.exe` works correctly when installed to `Program Files` (write permission).
7. Actual runtime behavior on Windows 10 (only Windows 11 development environment confirmed).
