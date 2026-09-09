# Lock Inspector core

Phase 6 provides synchronous, discovery-only inspection through Windows Restart
Manager. It identifies processes associated with a resource that may prevent
modification or deletion. The core is implemented, but there is no supported
user-facing entry point yet: no GUI, CLI, or Explorer context menu.

## API

`include/lock_inspector.h` declares the project-owned boundary:

```cpp
wperf::LockInspectionResult InspectLocks(const std::filesystem::path& path);
```

Link the CMake target `wperf_lock_inspector`. Pass an existing absolute Windows
path, preferably constructed from a wide string. Spaces and Unicode are
preserved. Empty paths, embedded nulls, relative paths (including drive-relative
paths), and detectable missing/malformed resources produce structured errors.
The core checks attributes once, registers exactly the requested path, and does
not canonicalize it, resolve junctions/symlinks, or enumerate descendants.

The returned `processes` contain a PID, Restart Manager display name (possibly
empty), and process start time in Windows FILETIME ticks. Results are sorted by
PID and start time and deduplicated by that pair. Duplicate metadata prefers a
nonempty name, then the lexically first name. Different start times preserve
distinct identities even if a PID was reused. Results are a snapshot; processes
may already have exited by the time the caller uses them.

## Status and errors

Always check `status` before interpreting `processes`:

| Status | Meaning |
|--------|---------|
| `Success` | Inspection completed; the process list may be empty |
| `InvalidPath` | Invalid input or a missing/malformed path detected during validation/registration |
| `AccessDenied` | An identifiable access or privilege failure |
| `DirectoryUnsupported` | Restart Manager rejected a directory with `ERROR_ACCESS_DENIED` at list retrieval |
| `RestartManagerFailure` | Other backend failure, including retry exhaustion or allocation failure |

`stage` identifies validation, session start, registration, list retrieval, or
session cleanup; `nativeError` retains the Windows error code. Successful
results use `None` and zero. Failures do not return partial process lists.
`cleanupError` retains an `RmEndSession` failure separately, so it cannot hide an
earlier error. If cleanup alone fails, status becomes a failure at `EndSession`.
Allocation failures while collecting results map to `ERROR_OUTOFMEMORY`.

## Backend and resource policy

Each invocation uses `RmStartSession`, `RmRegisterResources`, `RmGetList`, and
`RmEndSession`, linked from Windows SDK `Rstrtmgr.lib`. A noncopyable RAII owner
calls `RmEndSession` for every successfully started session, including early
failure and exception paths. Native cleanup failure is reported, not silently
assumed to have succeeded.

The list loop allows one size query plus three fill attempts, resizing again
when `ERROR_MORE_DATA` reports a changed count. It uses only the final returned
count and discards partial records. Persistent churn returns a failure with
`ERROR_MORE_DATA`. There are no sleeps or application-level infinite retries;
individual synchronous Windows API calls can still take time.

The core never opens a process or modifies the target. It does not shut down,
restart, terminate, or close handles in another process. Restart Manager does
its own session bookkeeping; inspection is read-only with respect to the target
and associated applications.

Inactive resource additions: **0 threads, 0 timers, 0 polling, 0 process scans,
0 handle enumeration**. There is no global session or cross-call process cache.
The overlay does not invoke this API yet; existing monitoring behavior is unchanged.

## Limitations

An empty successful result is not proof that a file can be deleted. Restart
Manager does not detect every kind of lock. Directory input is accepted and
registered without recursion, but Windows documents `ERROR_ACCESS_DENIED` when
`RmGetList` encounters a registered directory. The core exposes this as
`DirectoryUnsupported`, not as an empty successful result. Descendant-file locks
are not searched. See [Microsoft's RmGetList contract](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmgetlist).

The names come directly from Restart Manager; no executable-path enrichment or
permission-sensitive process opening is attempted. See [RM_PROCESS_INFO](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/ns-restartmanager-rm_process_info).

Deep native handle scanning, GUI, Explorer integration, process termination,
handle closing, and retry-delete are not implemented. Broader path handling and
fallback discovery belong to later phases.

## Validation

Deterministic unit tests cover input validation, error context, conversion,
sorting/deduplication, list growth/shrinkage, bounded retries, and cleanup on
success, failure, and exceptions. A private per-call table replaces only the
attribute query and four Restart Manager calls in these tests.

Three opt-in integration cases exercise actual Windows APIs on temporary
resources owned by the test. They check a held Unicode filename containing
spaces, the test process's PID/name/start time, an empty list after closing its
handle, invalid/missing paths, and the directory limitation. The tests remove
their own files and directory without recursive deletion or process orchestration.
They require no administrator privileges, but Restart Manager session bookkeeping
must be permitted (the development sandbox blocks it with `ERROR_WRITE_FAULT`).

See [testing.md](testing.md) for commands and verification results. These tests
are opt-in until their reliability on GitHub-hosted Windows runners is verified.
