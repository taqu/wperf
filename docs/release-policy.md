# wperf Release Policy

Release policy for `wperf` v0.1.0. The tag workflow builds, tests, packages,
and publishes the Windows x64 artifact; final release validation and version
metadata remain separate gates.

---

## Versioning

`wperf` uses [Semantic Versioning](https://semver.org/):

```
MAJOR.MINOR.PATCH
```

| Segment | Meaning |
|---------|---------|
| MAJOR | Breaking change to behavior or configuration format |
| MINOR | New feature, backward compatible |
| PATCH | Bug fix, backward compatible |

### Current Version Status

The canonical source version is `0.1.0` in `CMakeLists.txt`. It generates the
Windows version resource and CLI output (`wperf --version`); an optional
`WPERF_VERSION_SUFFIX` supports RC builds such as `-rc1`.

### First Structured Release

```
v0.1.0
```

### Pre-Release Naming

Pre-release candidates use the form:

```
v0.1.0-rc1
v0.1.0-rc2
```

---

## v0.1.0 Release Scope

### Included

- Existing lightweight wperf desktop performance overlay
- CPU, RAM, GPU, disk, and network monitoring
- Live clock display
- Window position and update interval persistence
- On-demand memory purge
- Always-on-top mode
- On-demand Lock Inspector: core, human/JSON CLI, explicit native deep scan, GUI, explicit process control, and tray launch implemented (Phases 6-11); Explorer integration pending
- Reproducible Release build (CMake + MSVC)
- Basic Windows CI (build verification)
- Essential user documentation
- GitHub Release with packaged artifact
- SHA-256 checksum for release artifact

### Explicitly Not Required for v0.1.0

- Linux support
- macOS support
- Remote monitoring
- Long-running process inspection or continuous handle scanning
- Task Manager replacement features
- Arbitrary handle closing
- Installer or auto-update system
- Plugin architecture
- Large architectural rewrite
- x86 support

---

## Release Artifact

### Target Artifact Name

```
wperf-v0.1.0-windows-x64.zip
```

The tag workflow produces this format directly.

### Archive Contents (minimum)

```
wperf.exe
LICENSE
```

Any required runtime files, if any are identified, must also be included.

### Version-Naming Convention

All release artifacts include the version tag and platform in the filename:

```
wperf-v{MAJOR}.{MINOR}.{PATCH}-windows-x64.zip
```

Pre-release artifacts use:

```
wperf-v{MAJOR}.{MINOR}.{PATCH}-rc{N}-windows-x64.zip
```

---

## Release Quality Gate

The following criteria must be met before tagging a final release.

| Criterion | Status |
|-----------|--------|
| Clean reproducible Release build from a fresh clone | Implemented (Phase 1) |
| Basic Windows CI passing (Debug + Release, PR + main) | Implemented (Phase 3) |
| Automated tests pass in Debug and Release | Implemented (Phase 5); required before release |
| No known release-blocking test failures | Required before release |
| Project-owned code builds cleanly at /W4 /WX /permissive- | Implemented (Phase 4) |
| No known release-blocking issues | Pending |
| Version constant present in source code | Implemented in CMake |
| Version resource embedded in executable (VERSIONINFO) | Implemented |
| `--version` matches canonical source | Implemented |
| Package name/checksum match validated release version | Implemented in tag workflow |
| Documentation updated and accurate | Complete for implemented features; visual tray validation remains manual |
| Controlled process actions manually validated before release | Implemented (Phase 10; opt-in integration coverage) |
| Release artifact validated on a clean Windows 10 or Windows 11 environment | Planned |
| Release candidate tested before final tag | Planned |
| SHA-256 checksum generated and attached to GitHub Release | Implemented in tag workflow |

---

## Lock Inspector Policy

Phases 6-11 implement the Restart Manager core, CLI, native
fallback (`wperf.exe --lock <absolute-path> [--deep] [--json]`). Restart Manager
remains the default; native scanning is explicit and on demand. Directory
descendant matching is supported, with partial results for inaccessible
processes/handles. The GUI is available through `wperf.exe --lock-ui [path]`.
Process control is explicit, identity-validated, confirmation-gated for force
termination, tray launch, and manually validated with controlled processes.
Explorer integration remains pending.
Remote handle closing is absent. See [lock-inspector.md](lock-inspector.md).

The Lock Inspector feature follows the core lightweight design policy:

- Activated on demand only (no background polling while inactive)
- Does not continuously enumerate system handles
- Does not add a background thread while inactive
- Focuses initially on identifying processes blocking file or directory modification/deletion
- Uses Windows Restart Manager as the primary detection mechanism
- On-demand native handle scan is implemented through explicit --deep; no automatic elevation
- Process termination is a later, explicit user action
- Arbitrary remote handle closing is out of scope for v0.1.0
- Explorer shell extension integration, if added later, should prefer an external command invocation over a permanently loaded shell-extension DLL

---

## Release Process

1. Confirm all quality gate items are met.
2. Update the `project(... VERSION ...)` value in `CMakeLists.txt`; the version
   header and `.rc` metadata are generated during configuration.
3. Create a signed git tag: `v0.1.0`
4. Push the tag to trigger the release workflow.
5. CI builds/tests Release, creates `wperf-vX.Y.Z-windows-x64.zip`, and uploads
   it with its `.sha256` checksum to the GitHub Release.
6. Validate the artifact on a clean machine before marking the release final.
7. If a defect is found, create a patch release (`v0.1.1`) rather than overwriting the published artifact.

## Post-release maintenance

The primary branch is `main` and should remain buildable and testable. Normal
work follows `feature/<name>`, `fix/<name>`, or `docs/<name>` branches through
PR review and the Debug/Release CI gate; release packaging is not required for
ordinary PRs.

Patch releases (`0.1.x`) are for bug, security, compatibility, or packaging
fixes. Meaningful new functionality uses the next minor version (`0.2.0` while
pre-1.0). Keep the canonical CMake version as `0.1.0` until release work
actually begins; do not automatically bump to `0.1.1`.

Hotfixes require a focused branch, regression test, normal CI, and the same RC,
tag, ZIP, checksum, and published-asset verification as any other release.
Security-sensitive changes require focused review and rechecks of process
identity, access rights, native bounds, and worker lifetime. Dependency updates
are intentional, reviewed, and covered by CI; Dependabot checks GitHub Actions
monthly without auto-merge.

Repository release-state note: the configured remote currently contains a
`v0.1.0` tag pointing to an older commit than this implementation. That tag and
its provenance must not be moved or rewritten; alignment of a future release
requires maintainer direction.
