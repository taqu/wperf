# Testing wperf

## Framework and initial audit

Before Phase 5, the repository had no test framework, test executables, CTest
registration, automated test scripts, or manual smoke-test scripts to preserve.
CI only built and checked application artifacts; all behavior was untested by automation.

The canonical framework is doctest 2.4.12, vendored with its MIT license under
`tests/third_party/doctest`. Its single header keeps Windows/MSVC integration
small and allows offline builds. CTest runs the `wperf_tests` console executable
as one entry, `wperf.unit`, containing 58 independently named doctest cases,
including Lock Inspector core, CLI, deep-scan, and GUI presentation coverage.
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

## Phase 7 CLI validation (2026-09-09)

Clean configuration: `cmake --fresh -S . -B build-phase7 -A x64 -DWPERF_BUILD_INTEGRATION_TESTS=ON`.
Debug and Release builds passed with `/W4 /WX /permissive- /utf-8`.
`ctest --test-dir build-phase7 -C Debug --output-on-failure --no-tests=error`
and the equivalent Release command passed all four CTest entries: 40 unit
cases (seven new CLI cases), CLI contract tests, CLI resource tests and the
three existing core integration cases.

`wperf.cli_contract` is included by default, so the existing CI workflow runs it
in both configurations without workflow changes. `wperf.cli_resources` uses the
existing `WPERF_BUILD_INTEGRATION_TESTS` option, alongside the core integration
tests; real Restart Manager tests remain opt-in pending hosted-runner validation.
The two CLI entries cover eight scenario groups, including strict JSON parsing,
field types, held/released files, Unicode paths with spaces, output streams and
bounded process lifetime. PowerShell (pwsh or Windows PowerShell) is required
when tests are enabled. Production adds no JSON dependency.

The development sandbox blocks Restart Manager session creation with Windows
error 29. Both complete suites passed when rerun outside the sandbox. Initial
sandbox CMake compiler detection also failed; a fresh outside-sandbox configure
restored the standard MSVC flags. GitHub-hosted CI: **NOT VERIFIED**.

A hidden no-argument Release startup smoke test found the normal wperf window
and closed it through WM_CLOSE with exit code 0. Visual rendering and menu
interaction were not manually verified. The app has no existing tray icon.

## Phase 8 native handle scan validation (2026-09-09)

Configured a new build tree with:

```powershell
cmake -S . -B build-phase8 -A x64 -DWPERF_BUILD_INTEGRATION_TESTS=ON
cmake --build build-phase8 --config Debug --parallel
cmake --build build-phase8 --config Release --parallel
ctest --test-dir build-phase8 -C Debug --output-on-failure --no-tests=error
ctest --test-dir build-phase8 -C Release --output-on-failure --no-tests=error
```

Debug and Release builds: **PASS**, with `/W4 /WX /permissive- /utf-8` unchanged.
Both test suites: **PASS**, all five CTest entries. The mandatory unit suite now
has 58 cases, including 12 path/native/merge/deep-CLI cases plus GUI and
process-control identity cases. The existing
integration executable now has seven cases (three RM, four native); a new
`wperf.cli_deep` entry checks deep CLI Unicode human/JSON output, source/resource
merging, complete/partial exit codes and released handles with a real JSON parser.
Existing normal CLI tests also passed. Native integration remains opt-in through
`WPERF_BUILD_INTEGRATION_TESTS`; no additional workflow was added. Deterministic
native unit tests and extended CLI argument tests run in the existing default CI.

Phase 9 tests cover `--lock-ui` with and without a Unicode initial path,
process/resource row expansion and sorting, empty/partial status text, and
concise native error presentation. Raw Win32 creation, resizing, focus, picker,
and close-during-scan behavior remain manual smoke tests; no GUI automation
framework was added. Phase 10 adds unit coverage for PID/creation-time identity
matching, invalid identities, and self-termination refusal, plus four opt-in
controlled process integration cases (graceful close, force termination, stale
identity, and already-exited handling). These use disposable wperf GUI children
and are excluded from default CI because interactive windows are not stable on
hosted runners.

Functional checks passed: held exact file, directory itself and descendant file,
released/no-match case, Unicode/spaces, extended paths, partial inaccessible
process handling and a synchronous pipe reader. Injected tests cover inaccessible
processes and raced handles without requiring protected-process access. All
created test resources/handles are cleaned up by their owners on normal completion.
Windows API integration tests ran outside the development sandbox because
Restart Manager needs session bookkeeping that it blocks.

Representative Release observation from the final suite (not a benchmark):
594 ms, 295,976 system handles, 20,131 File candidates, 13,009 resolved handles,
17 skipped processes, one matching process. Six observed scans ranged from
562 to 640 ms. Global activity naturally changes these counts. Native skipped
handle counts are conservative and include non-disk File objects and metadata
reopen failures; these scans correctly report partial coverage.

Development initially exposed long waits when querying duplicated synchronous
handles directly. The final backend uses independent metadata-only asynchronous
reopens and a single temporary cancellation watchdog. Controlled pipe-read tests
now pass. There is still no hard driver-independent wall-clock guarantee; see
[lock-inspector.md](lock-inspector.md) for bounds and limitations.

Normal desktop startup/monitoring/settings/shutdown were not manually retested
in Phase 8; the desktop startup path is unchanged and scanning is only dispatched
for explicit deep inspection. The existing application has no tray icon.
UNC/SMB live access and full reparse/alias equivalence: **NOT VERIFIED**.
GitHub-hosted CI: **NOT VERIFIED**.
