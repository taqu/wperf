# Changelog

## [Unreleased]

### Added

- Lightweight Windows performance overlay for CPU, RAM, GPU, disk, and network metrics.
- On-demand Lock Inspector with Restart Manager scans, optional native Deep Scan, CLI/JSON output, and a Win32 GUI.
- Explicit, identity-validated Close Normally and Force Terminate actions in the GUI.
- Notification-area menu integration for Settings, Lock Inspector, Purge Memory, and Exit.
- CMake/MSVC build, unit tests, opt-in Windows integration tests, and release documentation.

### Security

- No automatic elevation, SeDebugPrivilege enablement, remote handle closing, or arbitrary process control.
