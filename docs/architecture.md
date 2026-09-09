# Architecture

The desktop application owns the monitor window, notification-area menu, and
on-demand utility windows:

```
wperf
├── performance monitor
├── tray/right-click UI
└── Lock Inspector
    ├── GUI / CLI frontends
    ├── Lock Inspector core
    │   ├── Restart Manager backend
    │   └── native handle backend (Deep Scan)
    └── ProcessController action path
```

Discovery and process control are separate concerns. The tray menu only opens
or focuses the GUI; it owns no scan results, process identity, or handle state.
Lock Inspector workers are temporary and exist only while an explicit scan or
process action is active. CLI-only modes return before normal monitor/tray
initialization.
