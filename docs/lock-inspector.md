# Lock Inspector

Lock Inspector discovery is read-only and on demand. Restart Manager is the default
low-cost backend. Phase 8 adds an explicit native handle scan for additional
coverage, including handles to a directory and files beneath it. Phase 9 adds a
small native Win32 frontend over the same API. Phase 10 adds explicit process
control; Explorer integration is not implemented.

## GUI usage

```powershell
.\wperf.exe --lock-ui
.\wperf.exe --lock-ui "C:\project\build"
```

The optional path populates the editable path field but does not scan
automatically. **Browse File** and **Browse Folder** use the Windows picker.
**Inspect** runs the normal Restart Manager scan, while **Deep Scan** explicitly
adds the native system-handle scan and may take longer. **Refresh** repeats the
most recently requested mode; before the first scan it uses normal mode.

Results use one row per process/resource so all matching resources from a deep
directory scan remain visible. Restart Manager-only rows may have a blank
Resource column because that backend does not provide a matching path. Empty
success says `No locking processes found.` and partial results display a single
non-modal permissions/limits notice. Backend failures retain a concise Windows
error code in the status line.

Each scan uses one temporary worker so the window remains responsive and scan
buttons are disabled until it finishes. There is no automatic refresh, timer,
polling, or scan while the window is idle. Closing during a scan destroys the
window immediately; the dedicated UI entry waits safely for the bounded worker
to finish and discards its result. The worker then terminates and its handle is
released. Closing an idle GUI exits immediately.

The GUI does not close remote handles, modify the target, retry deletion, or
request elevation. In normal desktop mode it is available from the wperf
notification-area menu; the direct `--lock-ui` entry point remains available.
Explorer integration is not implemented.

## Process control

Select exactly one process row to enable **Close Normally** and **Force
Terminate**. Actions are disabled while a scan or another action is active.
Close Normally best-effort sends `WM_CLOSE` to that process's top-level windows,
waits briefly, and then rescans the target. A process without a closable window
may remain running. Force Terminate uses the minimum process rights needed for
identity validation, termination, and a bounded exit wait; it always asks for
confirmation and warns that unsaved data may be lost. Both actions rescan using
the previous normal/deep mode, and the result—not the API call alone—is the
source of truth.

Before either action, wperf compares the selected PID and captured process
creation time with a fresh process identity. If the PID was reused, the action
is refused and the result is refreshed. wperf also refuses to control itself,
does not enable SeDebugPrivilege or elevation, and never closes remote handles.
Access-denied, exited, stale-identity, no-window, and timeout outcomes remain
non-fatal to the GUI. There is no bulk termination, service control, delete or
retry operation, and no destructive CLI command.

## CLI usage

```powershell
.\wperf.exe --help
.\wperf.exe --lock "C:\project\output.dll"
.\wperf.exe --lock "C:\作業 folder\output.dll" --json
.\wperf.exe --lock "C:\project\build" --deep
.\wperf.exe --lock "C:\project\build" --deep --json
```

Supply an existing absolute path. Normal mode runs Restart Manager only;
`--deep` runs Restart Manager first, then one native scan and merges the results.
There is no automatic scan on an empty normal result. Invalid input is rejected
before native enumeration. `--json` and `--deep` may appear before or after
`--lock <path>`. Duplicate/unknown flags, missing/empty paths, modifiers without
`--lock`, and combining help with other arguments are invalid usage.
No arguments starts the existing desktop monitor.

The executable keeps the GUI subsystem. CLI mode attaches to the parent console
when available and never allocates a console window. Console text is Unicode;
redirected stdout/stderr are UTF-8 without a BOM. Interactive cmd.exe may require
`start "" /wait wperf.exe --lock "C:\project\build" --deep` to wait for a GUI
executable. In PowerShell, a pipeline such as
`& .\wperf.exe --lock "C:\project\build" --deep --json | Out-String`
waits for completion; inspect `$LASTEXITCODE`. Windows Terminal follows its
hosted shell. Scripts may also redirect streams and explicitly wait for exit.

## Output and exit codes

Human results show the target, PID and available name, with matching resource
paths below native matches. Names are Restart Manager display names or native
executable filenames; unavailable names display `(name unavailable)`.
An empty complete result says `No locking processes found.`. A partial empty
result says no matching processes were found **in the inspected portion**.
Neither proves that deletion will succeed.

| Code | Meaning |
|------|---------|
| 0 | Complete inspection with any process count, or help |
| 1 | Total inspection failure or failure to write otherwise successful output |
| 2 | Invalid command-line usage |
| 3 | Partial inspection; usable results may be present but coverage is incomplete |

Human results/help go to stdout. Failures and partial-result warnings go to
stderr. Parse errors always use human stderr, even with `--json`. JSON results,
including errors and partial results, use stdout only.

Normal-mode Phase 7 JSON is unchanged:

```json
{"path":"C:\\project\\output.dll","status":"success","processes":[{"pid":8420,"name":"Example application"}]}
```

Existing fields retain their types: string `path`/`status`, array `processes`,
numeric `pid`, string `name`. Normal errors retain `error.category`,
`native_code`, `stage`, and `cleanup_code`.

Deep mode adds process `source` (`restart_manager`, `native_handle_scan`, `both`)
and a string array `resources`. Restart Manager supplies no detailed resource
paths, so its resource array is empty unless merged with native matches.
Deep results also add:

- `complete`: boolean; false for partial results or errors.
- `native_scan`: `skipped_process_count`, `skipped_handle_count`, `limit_reached`,
  `native_code`, `nt_status`, and `stage`.
- `restart_manager`: `status`, `native_code`, `stage`, and `cleanup_code`.

Usable partial JSON keeps `status: "success"`, has `complete: false`, and exits
3. A total failure has `status: "error"` and exits 1. Automation using `--deep`
must inspect completeness or the exit code. Numeric `nt_status` preserves raw
NTSTATUS bits; `native_code` is a Windows error code. Detailed timing/handle
counts are available in the C++ result for tests, not emitted by the CLI.

Additional error category: `native_scan_failure`. Native stages are
`native_snapshot`, `native_resolve_target`, and `native_scan`; existing stages
remain unchanged. For early validation failures, native scanning is not run;
the reported code/stage describes validation. All JSON strings correctly escape
backslashes, quotes, controls, and UTF-16 surrogates.

## API and merge policy

`InspectLocks(path)` retains the Phase 6/7 behavior. The overload
`InspectLocks(path, LockInspectionOptions{true})` enables deep inspection.
The CLI calls the core once and returns before UI or monitoring initialization.
Path validation, discovery, normalization, ordering and merging belong to the
core library, not the CLI.

The API distinguishes `Success`, `PartialSuccess`, and backend failures.
Inaccessible processes, failed duplicates/reopens/queries, or a work limit
produce partial native results without discarding other matches. Skipped counts
are conservative: File objects also include pipes/devices which may be skipped
without being filesystem locks. Metadata failure alone leaves a usable PID.

A complete native scan can recover Restart Manager's expected
`DirectoryUnsupported` result. Other Restart Manager failures make the merged
result partial even if native scanning succeeds. If native scanning fails but
Restart Manager succeeded, its results survive as partial results. If both
backends fail, the result is a failure. Both backend errors and RM cleanup
errors remain available.

Merge uses PID primarily, combines resources/source, and sorts by PID/start time.
Known different start times are preserved as separate lifetimes and marked
partial rather than conflated after PID reuse. Unknown start times can merge
within this one inspection. Resource paths are sorted/deduplicated with ordinal
Unicode-insensitive comparison and a deterministic lexical tie-break.
No identities or snapshots persist after a call.

## Native implementation and bounds

The Windows-only backend dynamically resolves `NtQuerySystemInformation`,
`NtCreateFile`, and `RtlNtStatusToDosError` from `ntdll.dll`; missing APIs fail
cleanly. The locally isolated x64 `SystemExtendedHandleInformation` layout has
size/offset assertions. Returned byte lengths/counts are checked before reading.
The query starts at 1 MiB, allows eight attempts, and caps allocation at 64 MiB.
Native APIs/layouts can change across Windows versions; see Microsoft's
[NtQuerySystemInformation documentation](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation).

A real inspector-owned target handle identifies the current File object type
index; that handle is excluded from results. Other object types are discarded
before process access. One successful snapshot is grouped by PID; each process
is opened once with `PROCESS_DUP_HANDLE`. One duplicate is handled at a time,
using only `DUPLICATE_SAME_ACCESS`. Optional metadata opens only matching PIDs
with `PROCESS_QUERY_LIMITED_INFORMATION`.

A duplicate shares the holder's synchronous file-object lock. To avoid waiting
behind a pending synchronous read, the backend reopens the existing object
through `NtCreateFile` with `FILE_OPEN`, `FILE_READ_ATTRIBUTES`, full sharing,
and no synchronous-I/O flags. No file is created or overwritten. This also
supports directories, which `ReOpenFile` rejected in local testing. Both local
handles are RAII-owned. Non-disk handles are rejected before querying paths.
See [NtCreateFile](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile)
and [DuplicateHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle).

Path resolution uses `GetFinalPathNameByHandleW` with `FILE_NAME_OPENED`, trying
NT volume names first, then DOS names. Each form allows three bounded path-buffer
attempts, up to 32,768 characters. Drive mappings are read once with
`QueryDosDeviceW`. `NtQueryObject` is not used. See
[GetFinalPathNameByHandleW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew).

Candidate work is capped at 100,000 entries, five seconds checked between calls,
and roughly one million retained resource characters (plus at most one final
path). Reaching a limit yields partial results. A single temporary watchdog
requests cancellation after 100 ms of a candidate's I/O; it uses
`CancelSynchronousIo` on the scanner's own thread and joins before return.
`THREAD_TERMINATE` is the access right required by that cancellation API; no
thread is terminated. Inactive scans create no watchdog.

**These are not hard wall-clock guarantees.** Filesystem drivers may ignore or
not support cancellation; target opening, RM calls and individual driver calls
can still block. No thread is abandoned, and no process is killed to enforce a
deadline. See [CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio).

## Paths, directories and permissions

Exact file matching and directory equality/descendants use component boundaries:
`C:\build` matches `C:\build\a.dll`, but never `C:\builder\a.dll`.
Deep mode examines open handles rather than enumerating directory contents.
Both target and candidate paths come from handles. Unicode, spaces, mixed case,
trailing separators, drive roots, `\\?\` paths and extended UNC prefixes are
normalized using wide APIs and `CompareStringOrdinal`, without ANSI conversion.
NT device prefixes use current drive mappings and component boundaries; standard
MUP prefixes normalize to UNC. Unmapped NT paths remain NT paths.

Junctions/symlinks generally follow the opened target. Full equivalence across
hard links, short names, alternative mount paths, reparse aliases and volume-GUID
names is not guaranteed. Per-directory case-sensitive semantics are not modeled.
UNC normalization is unit-tested; live SMB/network behavior is **NOT VERIFIED**.
Remote providers may deny metadata access or block during resolution.

Protected/elevated processes, restricted files and handles that disappear during
the snapshot/duplication/query race are skipped. No privileges are enabled, no
UAC prompt is requested, and failed reopens never fall back to an unsafe direct
query of the remote synchronous file object. Empty results are not proof that
all possible lock types were inspected.

## Resource and security policy

While inactive: **0 handle enumerations, 0 process scans, 0 added threads,
0 timers, 0 polling**. Normal inspection remains Restart Manager only. Deep
inspection uses temporary state and one watchdog, releases local resources and
returns; the CLI then exits. Normal desktop monitoring is intentionally unchanged;
the normal desktop app exposes Lock Inspector from the notification-area tray menu. The direct `--lock-ui` entry point remains available for automation and dedicated use.

Process termination is available only through explicit, identity-validated GUI
actions. Remote handle closing, `DUPLICATE_CLOSE_SOURCE`, Restart Manager
shutdown, Explorer integration, elevation, retry-delete and release packaging
are not added. Discovery and process control never modify another process's
handle table or file contents.

## Validation

The mandatory suite adds 12 native/merge/deep-CLI unit cases to the existing 40.
Four opt-in native integration cases cover held/released Unicode files,
directory handles and descendants, extended paths, and a pending synchronous
pipe read. A further opt-in CLI entry validates deep human/JSON output, merged
sources/resources, exit codes and released resources with a real JSON parser.
Real native tests remain opt-in pending GitHub-hosted runner verification.
See [testing.md](testing.md) for commands and local measurements.
