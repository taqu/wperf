# wperf Code Quality Baseline

Established in Phase 4. Applies to all project-owned source code.

---

## Compiler

Primary compiler: MSVC (Visual C++) via Visual Studio 2022, toolset v143.

---

## Warning Baseline

```
/W4           — level 4 warnings (comprehensive)
/WX           — warnings treated as errors
/permissive-  — strict C++ conformance mode
/utf-8        — source and execution charset UTF-8
```

All four flags are active in both Debug and Release configurations via `CMakeLists.txt`.

`/Wall` is not used as the project warning level. MSVC `/Wall` includes diagnostics on system and SDK headers that are not actionable in project code; `/W4` provides comprehensive coverage without that noise.

---

## Warnings as Errors

`/WX` is **enabled**.

The project-owned codebase must build with zero warnings in both Debug and Release. A new warning that cannot be immediately fixed must be resolved before merging.

---

## Conformance

`/permissive-` is **enabled**.

This disables non-conforming language extensions and enforces two-phase name lookup. The codebase builds cleanly under this setting.

---

## Third-Party and System Headers

The warning policy applies to project-owned code in `src/` and `include/`. MSVC does not emit `/W4` warnings from system or SDK headers under normal circumstances; no additional suppression of Windows SDK headers is currently required.

`wperf` has no vendored third-party libraries.

---

## Suppression Policy

Warnings should be fixed at the source where practical.

If a suppression is unavoidable:

- use the narrowest possible scope (`#pragma warning(push/pop)` around the affected lines),
- document the reason inline,
- do not use project-wide `/wdXXXX` flags to silence warnings that should be fixed.

No warning suppressions are currently present in the codebase.

---

## Static Analysis

Static analysis is **not currently enabled**. Evaluation deferred to a later phase.

Candidates for future consideration:
- MSVC `/analyze` (built-in Code Analysis)
- clang-tidy with a focused check set (`bugprone-*`, `performance-*`)

Neither is required for v0.1.0.

---

## Formatting

A `.clang-format` configuration file exists in the repository root (Chromium-based style with project customizations). Formatting is not currently enforced in CI.
