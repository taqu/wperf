# Lock Inspector CLI

Phase 7 exposes the synchronous, discovery-only Windows Restart Manager core
through an on-demand CLI. There is no Lock Inspector GUI or Explorer menu.

## CLI usage

```powershell
.\wperf.exe --help
.\wperf.exe --lock "C:\project\output.dll"
.\wperf.exe --lock "C:\作業 folder\output.dll" --json
```

Supply an existing absolute path. Relative paths are rejected by the core.
`--json` may appear before or after `--lock <path>`. Duplicate flags, unknown
arguments, empty/missing paths, and combining `--help` with other flags are
invalid usage. No arguments starts the existing desktop monitor.

The executable retains the Windows GUI subsystem. CLI mode attaches to the
parent console when available and never allocates a console window. Console
text uses Unicode; redirected stdout/stderr use UTF-8 without a BOM. For shells
that do not wait for GUI executables, explicitly wait: in interactive cmd.exe,
use `start "" /wait wperf.exe --lock "C:\project\output.dll"`. In PowerShell,
a pipeline such as `& .\wperf.exe --lock "C:\project\output.dll" --json | Out-String`
waits for completion; inspect `$LASTEXITCODE`. Windows Terminal behavior follows
its hosted shell. Scripts can also launch with redirected streams and wait for
the process, as the integration tests do.

## Output and exit codes

Human output includes the target, PID and Restart Manager display name, or
`(name unavailable)`. Both formats preserve the core's PID/start-time ordering.
An empty successful list prints `No locking processes found.` and exits 0;
it does not prove that deletion is possible.

| Code | Meaning |
|------|---------|
| 0 | Inspection completed, with any process count; or help displayed |
| 1 | Inspection failed, or output could not be written |
| 2 | Invalid command-line usage |

Human successes/help go to stdout; human failures go to stderr. Parse errors
always use human text on stderr and leave stdout empty, even with `--json`.
JSON inspection results, including failures, go only to stdout:

```json
{"path":"C:\\project\\output.dll","status":"success","processes":[{"pid":8420,"name":"Example application"}]}
```

`path` and `status` are strings. Success always has a `processes` array, possibly
empty; each element has numeric `pid` and string `name` (possibly empty).
Names are display names, not necessarily executable filenames.

```json
{"path":"C:\\missing.txt","status":"error","error":{"category":"invalid_path","native_code":2,"stage":"validate_path","cleanup_code":0}}
```

Error categories are `invalid_path`, `access_denied`, `directory_unsupported`,
and `restart_manager_failure`. Stages are `none`, `validate_path`,
`start_session`, `register_resource`, `get_list`, and `end_session`.
`native_code` and `cleanup_code` are numeric Windows codes. Human errors retain
this context with a readable category. JSON escapes quotes, backslashes, control
characters and UTF-16 surrogate code units; other Unicode is emitted as UTF-8
when redirected. No discovery diagnostics are mixed into JSON stdout.

## API and architecture

`include/lock_inspector.h` declares `InspectLocks(const std::filesystem::path&)`.
The `wperf_lock_cli` library parses/formats; `wperf_lock_inspector` owns path
validation, discovery, error mapping, sorting and deduplication. The Windows
frontend uses `CommandLineToArgvW`, preserving wide paths without ANSI conversion.
For valid inspection arguments it invokes the core exactly once, prints, and
exits before common controls, windows, settings or monitoring initialization.
The existing global monitor constructor is empty and performs no sampling.

The core requires an existing absolute Windows path, rejects embedded nulls,
and registers exactly one resource without resolving symlinks/junctions or
scanning descendants. Records contain PID, display name and FILETIME start time;
they are sorted/deduplicated by PID/start time. Duplicate metadata prefers a
nonempty name, then the lexically first name. Start time is internal and is not
part of the CLI JSON schema. Processes can exit after the snapshot.

Each invocation uses `RmStartSession`, `RmRegisterResources`, `RmGetList`, and
`RmEndSession`, linked from Windows SDK `Rstrtmgr.lib`. A noncopyable RAII owner
ends every started session, including exception paths. Cleanup failure is
retained separately and becomes an inspection failure if it is the only error.
The list loop allows one size query and three fill attempts; persistent churn
returns `ERROR_MORE_DATA` and no partial list. Allocation failures in result
collection map to `ERROR_OUTOFMEMORY`.

## Limitations and resource policy

Restart Manager is the only backend and cannot detect every kind of lock.
Directories are accepted without recursion, but Restart Manager can reject them
at list retrieval with access denied, exposed as `directory_unsupported`.
Descendant-file locks are not searched. See the
[RmGetList contract](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmgetlist).
An empty list is not proof that a resource is unlocked.

There is no deep native handle scanning, Lock Inspector GUI, Explorer integration,
process termination, arbitrary handle closing, privilege elevation or retry-delete.
The command never opens or alters another process. Inactive additions are
**0 threads, 0 timers, 0 polling, 0 process scans**. No persistent inspection
session/cache exists. Normal desktop behavior is unchanged; the existing app
has no tray icon.

## Validation

Seven CLI unit cases cover parsing, conflicts, one-call dispatch, exit codes,
empty results, shared core ordering, names, structured errors and JSON escaping.
Two process-level CTest entries cover eight scenario groups: help, invalid usage,
missing-path human/JSON, and held/released-file human/JSON. PowerShell parses JSON
and verifies field types and Unicode round trips. Each child must exit within
20 seconds. Tests use their own temporary resources and require no arbitrary
running applications. The contract entry runs in mandatory CI; real Restart
Manager resource tests remain opt-in pending hosted-runner verification.
See [testing.md](testing.md) for commands and validation results.
