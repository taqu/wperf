# User Guide

Run `wperf.exe` to place the compact performance overlay on the desktop. The
normal build also adds a `wperf` notification-area icon.

## Tray and overlay

Right-click the overlay or tray icon for **Settings**, **Lock Inspector...**,
**Purge Memory**, and **Exit**. Left-clicking the tray icon toggles the overlay.
The tray menu is a launcher only; Lock Inspector does not scan until you select
an inspection command in its window.

Settings controls the update interval (250–60,000 ms) and Always-on-top mode.
Purge Memory trims accessible process working sets on demand and may skip
protected processes. Exit shuts down wperf and any open Lock Inspector window.

## Lock Inspector

Select **Lock Inspector...**, enter an absolute file or directory path, and
choose **Inspect** or the optional **Deep Scan**. Refresh repeats the last scan.
Process actions are explicit and described in [lock-inspector.md](lock-inspector.md).

The direct `wperf.exe --lock-ui [path]` entry point and read-only CLI remain
available. See [lock-inspector.md](lock-inspector.md) for CLI, JSON, security,
and coverage details.
