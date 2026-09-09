# Security Model

## Privilege Model

wperf runs as the launching user. It does not request UAC elevation or enable
`SeDebugPrivilege`. Standard-user access restrictions and protected/elevated
processes can therefore produce incomplete inspection results.

## Native Handle Inspection

Deep Scan is an explicit, bounded, read-only inspection. Handles are duplicated
only for metadata/path resolution and are closed by wperf's own RAII cleanup.
wperf never uses `DUPLICATE_CLOSE_SOURCE`, closes arbitrary remote handles, or
modifies another process's handle table.

## Process Identity

GUI actions capture PID and process creation time, then revalidate both before
acting. PID reuse is refused, and wperf refuses to control itself.

## Destructive Actions

Force Terminate requires an explicit confirmation and may lose unsaved data.
Close Normally is best effort and is not guaranteed to make a process exit.
There is no destructive Lock Inspector CLI command, bulk termination, service
control, delete/retry operation, or automatic action from the tray.

## Known Limitations

Results are snapshots and may become stale immediately. Protected processes,
disappearing handles, inaccessible paths, unusual reparse aliases, and remote
providers may limit coverage. Empty results do not prove that every possible
lock was inspected. Explorer integration is not implemented.
