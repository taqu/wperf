# wperf v0.1.0 Roadmap

## Project Direction

`wperf` is a lightweight Windows desktop-resident performance monitor.

The project should preserve the following principle:

- Keep idle CPU and memory usage minimal.
- Expensive functionality must run only on demand.
- Do not introduce unnecessary background polling, persistent worker threads, or continuous process/handle scanning.
- Lock Inspector should remain a focused utility, not evolve into a full Task Manager replacement.

For v0.1.0, Explorer shell integration is intentionally omitted.

The Lock Inspector will be opened only from the existing wperf right-click menu.

The intended interaction is:

    wperf tray icon
        ↓ right click

    Settings
    Lock Inspector...
    Purge Memory
    ----------------
    Exit

    Lock Inspector...
        ↓
    GUI
        ↓
    Browse File / Browse Folder
        ↓
    Inspect / Deep Scan
        ↓
    optionally:
        Close Normally
        Force Terminate

No Explorer registry integration, shell verb, shell-extension DLL, or COM integration is planned for v0.1.0.

---

# Foundation

## Phase 0 — Repository Audit and Release Baseline

Establish the current repository and release baseline.

Main work:

- repository structure audit,
- current architecture documentation,
- build requirements,
- external/runtime dependencies,
- supported platform baseline,
- test/CI/release infrastructure audit,
- v0.1.0 scope definition,
- release policy.

Main output:

    docs/project-baseline.md
    docs/supported-platforms.md
    docs/release-policy.md

No functional changes.

---

## Phase 1 — Reproducible Windows Build

Establish a clean and reproducible Windows x64 build.

Goals:

- build from a clean checkout,
- canonical Debug build,
- canonical Release build,
- no undocumented IDE-only steps,
- predictable output paths,
- documented runtime dependencies.

Main output:

    docs/build.md

This build path becomes the source of truth for CI.

---

## Phase 2 — README and User Documentation

Make the GitHub repository understandable to a new user.

Goals:

- improve `README.md`,
- document current features,
- document supported platforms,
- document basic usage,
- communicate the lightweight design policy,
- link deeper technical documentation.

Do not document planned features as already implemented.

---

## Phase 3 — Basic GitHub Actions CI

Add minimal Windows build CI.

Expected flow:

    Pull Request / Push
        ↓
    Checkout
        ↓
    Configure
        ↓
    Build Debug
        ↓
    Build Release
        ↓
    Verify artifacts

Target:

    windows-latest
    MSVC
    x64

No release automation yet.

---

## Phase 4 — Compiler Diagnostics and Static Quality

Establish a compiler-quality baseline.

Primary target:

    /W4
    /permissive-

Clean meaningful compiler warnings in project-owned code.

Evaluate `/WX`, but enable it only if the build is stable enough.

Static analysis may be added only if it provides clear value without excessive complexity.

Main output:

    docs/code-quality.md

---

## Phase 5 — Test Infrastructure

Establish reliable automated testing.

Goals:

- choose one test framework,
- integrate tests with the canonical build,
- run tests through CI,
- add a small initial set of deterministic, high-value tests,
- avoid hardware-dependent and timing-sensitive mandatory tests.

Focus on project-owned logic such as:

- formatting,
- configuration,
- conversions,
- path utilities,
- error handling.

Main output:

    docs/testing.md

---

# Lock Inspector

## Phase 6 — Restart Manager Core

Implement the first Lock Inspector backend using Windows Restart Manager.

Architecture:

    path
      ↓
    Lock Inspector Core
      ↓
    Restart Manager
      ↓
    structured result

Goals:

- inspect a file or directory,
- return process PID/name,
- structured errors,
- no UI dependency,
- no process control,
- no background work.

Relevant APIs:

    RmStartSession
    RmRegisterResources
    RmGetList
    RmEndSession

No `RmShutdown` or `RmRestart`.

---

## Phase 7 — Lock Inspector CLI

Expose the core through a command-line interface.

Example:

    wperf.exe --lock "C:\project\build"

Machine-readable mode:

    wperf.exe --lock "C:\project\build" --json

Goals:

- human-readable output,
- JSON output,
- stable exit codes,
- Unicode paths,
- short-lived execution,
- avoid starting the normal desktop monitor in CLI mode.

The CLI remains read-only.

---

## Phase 8 — Native Deep Scan

Add an optional native handle-scan backend for cases Restart Manager cannot detect.

Possible entry point:

    wperf.exe --lock "C:\project\build" --deep

Architecture:

    Restart Manager
        +
    Native Handle Scan

Main capabilities:

- exact file matching,
- directory descendant matching,
- NT device path handling,
- `\\?\` path normalization,
- case-insensitive Windows path matching,
- Unicode support,
- inaccessible-process tolerance,
- result merging and deduplication.

Potential APIs include:

    NtQuerySystemInformation
    DuplicateHandle
    GetFinalPathNameByHandleW

Important rules:

- one-shot scan only,
- no continuous handle enumeration,
- no remote handle closing,
- no automatic elevation.

---

## Phase 9 — Lock Inspector GUI

Add a small GUI frontend over the existing core.

Core flow:

    Path
    [ Browse ]

    [ Inspect ]
    [ Deep Scan ]

    Processes
    PID / Process

    Matching resources

    [ Refresh ]
    [ Close ]

Goals:

- file/folder selection,
- normal inspection,
- deep inspection,
- manual refresh,
- process/resource display,
- partial-result indication.

Important behavior:

    automatic refresh = NO
    periodic scan = NO

The GUI should be created only on demand.

---

## Phase 10 — Safe Process Control

Add explicit process-control actions to the GUI.

Preferred flow:

    detected process
        ↓
    Close Normally
        ↓
    bounded wait
        ↓
    Refresh
        ↓
    still present?
        ↓
    user may choose Force Terminate

Actions:

    Close Normally
    Force Terminate

Safety requirements:

- Force Terminate requires confirmation.
- Validate process identity before destructive action.
- Account for PID reuse using process creation time or equivalent identity.
- Use minimum process rights.
- Block accidental self-termination.
- No automatic elevation.
- No bulk termination.

Explicitly excluded:

    Close Handle
    DUPLICATE_CLOSE_SOURCE
    Kill All
    automatic UAC
    SeDebugPrivilege escalation

---

## Phase 11 — Existing Right-Click Menu Integration

Integrate Lock Inspector into the existing wperf right-click menu only.

Final menu concept:

    Settings
    Lock Inspector...
    Purge Memory
    ----------------
    Exit

Selecting:

    Lock Inspector...

creates or focuses the existing Lock Inspector GUI.

Recommended window policy:

    one Lock Inspector window per wperf process

If already open:

    focus / restore existing window

If closed:

    release Lock Inspector-specific runtime state

Required inactive behavior:

    Lock Inspector window: 0
    Lock Inspector workers: 0
    Lock Inspector timers: 0
    Lock Inspector polling: 0
    automatic handle scans: 0

Opening the right-click menu itself must not trigger inspection.

---

# Stabilization

## Phase 12 — UX and Error Handling Hardening

Stop feature expansion and harden the implementation.

Standardize states such as:

    success
    no match
    partial success
    invalid input
    access denied
    backend failure
    stale process identity

Review and harden:

- process exits during scan,
- process exits before action,
- PID reuse,
- handle reuse,
- file/directory disappearance,
- path changes,
- busy states,
- stale GUI selection,
- GUI close during scan,
- application exit during scan,
- CLI exit codes,
- stdout/stderr behavior,
- JSON error output,
- cleanup on all failure paths.

No new major features.

---

## Phase 13 — Security Review

Perform a dedicated security review across the entire Lock Inspector implementation.

### Native Handle Scanner

Review:

- `NtQuerySystemInformation`,
- native structure sizing,
- integer/buffer overflow,
- `DuplicateHandle`,
- minimum process rights,
- remote handle safety,
- handle races,
- path resolution.

### Process Control

Review:

- PID reuse,
- creation-time validation,
- stale identity,
- self-target protection,
- `WM_CLOSE`,
- `TerminateProcess`,
- protected processes,
- minimum privileges.

### Path Handling

Review:

- Unicode,
- `\\?\`,
- UNC,
- junctions,
- symlinks,
- component-boundary matching,
- TOCTOU conditions.

Explicitly confirm:

    PROCESS_ALL_ACCESS       not used unnecessarily
    DUPLICATE_CLOSE_SOURCE   not used
    SeDebugPrivilege         not automatically enabled
    automatic UAC            not implemented
    arbitrary remote handle closing not implemented

---

# Release Preparation

## Phase 14 — Documentation Finalization

Make all documentation release-ready.

Expected documentation:

    README.md
    LICENSE
    CHANGELOG.md
    CONTRIBUTING.md

    docs/
      build.md
      usage.md
      testing.md
      code-quality.md
      lock-inspector.md
      supported-platforms.md
      project-baseline.md
      release-policy.md

If useful, add:

    architecture.md

The final Lock Inspector documentation should describe:

    Opening:
      wperf right-click menu
        → Lock Inspector...

    Detection:
      Inspect
      Deep Scan

    Actions:
      Close Normally
      Force Terminate

    Not provided:
      Explorer integration
      arbitrary handle closing
      automatic elevation
      automatic scanning

---

## Phase 15 — Release CI and Packaging

Add a dedicated release workflow separate from normal CI.

Expected flow:

    version tag
        ↓
    Release build
        ↓
    tests
        ↓
    package
        ↓
    SHA-256
        ↓
    release artifact

Expected artifacts:

    wperf-v0.1.0-windows-x64.zip
    wperf-v0.1.0-windows-x64.zip.sha256

The ZIP should contain only required files, for example:

    wperf.exe
    LICENSE
    README.txt

plus any required runtime DLLs.

---

## Phase 16 — Versioning and Windows Metadata

Establish one canonical project version.

Initial release:

    0.1.0

Expose it through:

### Windows executable metadata

    File version:
    0.1.0.0

    Product version:
    0.1.0

### CLI

    wperf.exe --version

Expected:

    wperf 0.1.0

Optionally include build metadata such as:

    commit: abc1234

Keep all version sources synchronized.

---

## Phase 17 — Release Candidate

Create:

    v0.1.0-rc1

using the real release packaging path.

Validate on representative environments.

Target validation areas include:

    Windows 10 x64
    Windows 11 x64
    standard user
    administrator
    GPU available
    GPU unavailable/unsupported
    100% DPI
    125% / 150% DPI

Lock Inspector scenarios should include:

    file lock
    directory descendant lock
    Unicode path
    no-lock result
    deep scan
    process exit during scan
    Close Normally
    Force Terminate cancellation
    Force Terminate
    access denied
    close GUI during scan
    repeated open/close

Also verify the existing right-click menu:

    Settings
    Lock Inspector...
    Purge Memory
    Exit

---

## Phase 18 — GitHub Release

After RC validation and fixes, create:

    v0.1.0

Publish a GitHub Release.

Expected highlights may include:

    - Lightweight Windows performance monitor
    - CPU / RAM / GPU / Disk / Network monitoring
    - On-demand Lock Inspector
    - Restart Manager-based fast inspection
    - Optional native deep scan
    - Safe process-close controls

Publish:

    wperf-v0.1.0-windows-x64.zip
    SHA-256 checksum

---

## Phase 19 — Post-Release Workflow

Standardize ongoing development.

Recommended flow:

    feature branch
        ↓
    Pull Request
        ↓
    CI
        ↓
    review
        ↓
    main
        ↓
    version tag
        ↓
    release workflow

Optional repository improvements can be added here:

- Dependabot,
- issue templates,
- pull request template,
- maintenance/release documentation improvements.

---

# Final Roadmap Summary

    Foundation
    ------------------------------------------------
    Phase 0   Repository Audit
    Phase 1   Reproducible Build
    Phase 2   README / Documentation
    Phase 3   Basic CI
    Phase 4   Compiler Quality
    Phase 5   Test Infrastructure

    Lock Inspector
    ------------------------------------------------
    Phase 6   Restart Manager Core
    Phase 7   CLI
    Phase 8   Native Deep Scan
    Phase 9   GUI
    Phase 10  Safe Process Control
    Phase 11  Existing Right-Click Menu Integration

    Stabilization
    ------------------------------------------------
    Phase 12  UX / Error Hardening
    Phase 13  Security Review

    Release
    ------------------------------------------------
    Phase 14  Documentation Finalization
    Phase 15  Release CI / Packaging
    Phase 16  Versioning
    Phase 17  Release Candidate
    Phase 18  GitHub Release
    Phase 19  Post-Release Workflow

# Explicitly Removed from v0.1.0

The following are intentionally excluded:

    Explorer file/folder context-menu integration
    Explorer shell verbs
    Explorer registry registration
    Explorer COM extensions
    IExplorerCommand
    shell-extension DLLs
    --install-explorer-menu
    --uninstall-explorer-menu
    Windows 11 first-level context-menu integration
    persistent Explorer helper processes
    arbitrary remote handle closing
    DUPLICATE_CLOSE_SOURCE
    automatic UAC elevation
    automatic continuous lock scanning

The v0.1.0 Lock Inspector remains a small, explicit, on-demand extension of the existing lightweight wperf application.