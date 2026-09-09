# Testing wperf

## Framework and initial audit

Before Phase 5, the repository had no test framework, test executables, CTest
registration, automated test scripts, or manual smoke-test scripts to preserve.
CI only built and checked application artifacts; all behavior was untested by automation.

The canonical framework is doctest 2.4.12, vendored with its MIT license under
`tests/third_party/doctest`. Its single header keeps Windows/MSVC integration
small and allows offline builds. CTest runs the `wperf_tests` console executable
as one entry, `wperf.unit`, containing 33 independently named doctest cases (18 baseline + 15 Lock Inspector).
Assertion checks remain active in Release and report expected/actual values.

## Build and run

From the repository root, these commands were validated with MSVC v143:

```powershell
cmake -S . -B build-phase6-validation -A x64
cmake --build build-phase6-validation --config Debug
ctest --test-dir build-phase6-validation -C Debug --output-on-failure --no-tests=error
cmake --build build-phase6-validation --config Release
ctest --test-dir build-phase6-validation -C Release --output-on-failure --no-tests=error
```

`build-phase6-validation` was a fresh validation directory; use `build` instead for the
normal developer build. Tests are built by default. To disable them, configure
with `-DBUILD_TESTING=OFF`. The test target is `wperf_tests`, separate from
`wperf`; no framework code is included in the application.

## Coverage and categories

The mandatory suite carries the CTest `unit` label and does not call live OS
inspection APIs. Fifteen Lock Inspector cases in `test_lock_inspector.cpp`
exercise production orchestration with a per-call attribute/Restart Manager
seam: input validation, native error context, Unicode preservation, process
conversion and deterministic deduplication, growing/shrinking lists, bounded
retries, and session cleanup (including exceptions and cleanup failures).

Three opt-in cases in `test_lock_inspector_integration.cpp` run as
`wperf.lock_integration`, with the `integration` label and a 60-second CTest
timeout. They create their own temporary resources and exercise actual Restart
Manager discovery; they do not depend on arbitrary running applications.

To include the real backend suite (verified locally):

```powershell
cmake -S . -B build-phase6-validation -A x64 -DWPERF_BUILD_INTEGRATION_TESTS=ON
cmake --build build-phase6-validation --config Debug
ctest --test-dir build-phase6-validation -C Debug --output-on-failure --no-tests=error
cmake --build build-phase6-validation --config Release
ctest --test-dir build-phase6-validation -C Release --output-on-failure --no-tests=error
```

Use `-L unit` or `-L integration` with CTest to run one category. The integration
option defaults to OFF, so the unchanged CI workflow automatically builds the
core and runs all 33 unit cases. Real-backend tests are excluded until their
reliability on GitHub-hosted Windows runners is verified. They require normal
Restart Manager session access, which this development sandbox blocks; running
outside that sandbox does not require administrator privileges.

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
Lock Inspector validates absolute input without adding generic path canonicalization.

## CI and isolation

`.github/workflows/ci.yml` runs the suite after each Debug and Release build,
before checking the corresponding application artifact. `--output-on-failure`
shows failed case names and assertions; `--no-tests=error` rejects an empty suite.

Phase 6 local validation: see the results recorded below. GitHub-hosted CI:
**NOT VERIFIED**.

Neither suite touches user settings, launches the GUI, collects live metrics,
controls another process, or changes system performance settings. Unit tests
use only in-memory inputs. Integration tests create and remove only their own
temporary files/directory and close their own file handle; the core itself does
not modify those resources. Restart Manager maintains temporary session
bookkeeping. No interactive input or administrator privileges are required.
CTest writes its normal logs only inside the build directory.

## Remaining manual work

Live CPU/RAM/disk/network sampling, GPU enumeration and counters, desktop
rendering, window positioning, settings persistence, startup/shutdown, and
memory purge remain manual or future integration testing. There is no tray icon.
Hardware tests are excluded from mandatory CI. Lock Inspector deep scanning,
user interfaces, and broader OS/driver scenarios remain untested. No coverage
percentage is claimed.

To extend the suite, add named `TEST_CASE` blocks to `tests/test_app_logic.cpp`
(or add another source to `wperf_tests`). Exercise observable production logic
with explicit inputs; avoid live load assertions, sleeps, and Windows API mocks.
