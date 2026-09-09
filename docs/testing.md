# Testing wperf

## Framework and initial audit

Before Phase 5, the repository had no test framework, test executables, CTest
registration, automated test scripts, or manual smoke-test scripts to preserve.
CI only built and checked application artifacts; all behavior was untested by automation.

The canonical framework is doctest 2.4.12, vendored with its MIT license under
`tests/third_party/doctest`. Its single header keeps Windows/MSVC integration
small and allows offline builds. CTest runs the `wperf_tests` console executable
as one entry, `wperf.unit`, containing 18 independently named doctest cases.
Assertion checks remain active in Release and report expected/actual values.

## Build and run

From the repository root, these commands were validated with MSVC v143:

```powershell
cmake -S . -B build-phase5 -A x64
cmake --build build-phase5 --config Debug
ctest --test-dir build-phase5 -C Debug --output-on-failure --no-tests=error
cmake --build build-phase5 --config Release
ctest --test-dir build-phase5 -C Release --output-on-failure --no-tests=error
```

`build-phase5` was a fresh validation directory; use `build` instead for the
normal developer build. Tests are built by default. To disable them, configure
with `-DBUILD_TESTING=OFF`. The test target is `wperf_tests`, separate from
`wperf`; no framework code is included in the application.

## Coverage and categories

All current cases carry the CTest `unit` label. There are no integration tests.
Future tests that call Windows APIs should use a separate target and the
`integration` label so the fast deterministic gate remains clear.

The suite covers the production helpers in `include/app_logic.h`:

- GB memory display, binary throughput boundaries, rounding, zero, large and negative values.
- Settings defaults, stored integer clamping, dialog acceptance and fallback.
- CPU percentage calculation, idle/busy samples, zero denominator, inconsistent and large deltas.

The extraction preserves existing semantics: memory always displays GB; rates
select units before rounding; negative rates display in B/s; dialog parsing
accepts an integer prefix and retains the previous interval for invalid input.
Stored intervals clamp, whereas invalid dialog intervals are rejected.
No formatting or parsing corrections were made.

Settings tests use in-memory values only. Actual INI reads/writes, missing keys,
Windows' malformed INI handling, and persistence remain untested. There is no
generic path utility; executable-dependent INI path construction is deferred.
No Lock Inspector path logic was introduced.

## CI and isolation

`.github/workflows/ci.yml` runs the suite after each Debug and Release build,
before checking the corresponding application artifact. `--output-on-failure`
shows failed case names and assertions; `--no-tests=error` rejects an empty suite.

Local validation: Debug and Release builds and all 18 cases passed. CTest total
time was 0.13 seconds for Debug and 0.05 seconds for Release. Both application
artifacts were verified. GitHub-hosted CI: **NOT VERIFIED**.

The test executable does not read or write user settings, create temporary
files, launch the GUI, collect live metrics, manipulate processes, or change
system settings. It needs no interactive input or administrator privileges.
CTest writes its normal logs only inside the build directory.

## Remaining manual work

Live CPU/RAM/disk/network sampling, GPU enumeration and counters, desktop
rendering, window positioning, settings persistence, startup/shutdown, and
memory purge remain manual or future integration testing. There is no tray icon.
Hardware tests are excluded from mandatory CI. Future Lock Inspector testing
will be added with that feature. No coverage percentage is claimed.

To extend the suite, add named `TEST_CASE` blocks to `tests/test_app_logic.cpp`
(or add another source to `wperf_tests`). Exercise observable production logic
with explicit inputs; avoid live load assertions, sleeps, and Windows API mocks.
