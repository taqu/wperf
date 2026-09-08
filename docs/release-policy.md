# wperf Release Policy

Initial release policy for `wperf`. Defined in Phase 0 as a planning baseline. Implementation of CI, packaging, and release infrastructure is deferred to later phases.

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

No version constant, executable version resource, or `--version` support currently exists. This is a known gap to be addressed before the v0.1.0 release.

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
- Future on-demand Lock Inspector feature
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

The exact format may be refined when packaging is implemented.

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
| Project-owned code builds cleanly at /W4 /WX /permissive- | Implemented (Phase 4) |
| No known release-blocking issues | Pending |
| Version constant present in source code | Planned |
| Version resource embedded in executable (VERSIONINFO) | Planned |
| Documentation updated and accurate | In progress |
| Release artifact validated on a clean Windows 10 or Windows 11 environment | Planned |
| Release candidate tested before final tag | Planned |
| SHA-256 checksum generated and attached to GitHub Release | Planned |

---

## Lock Inspector Policy

The future Lock Inspector feature must follow the core lightweight design policy:

- Activated on demand only (no background polling while inactive)
- Does not continuously enumerate system handles
- Does not add a background thread while inactive
- Focuses initially on identifying processes blocking file or directory modification/deletion
- Uses Windows Restart Manager as the primary detection mechanism
- On-demand native handle scan may exist as a fallback
- Process termination is a later, explicit user action
- Arbitrary remote handle closing is out of scope for v0.1.0
- Explorer shell extension integration, if added later, should prefer an external command invocation over a permanently loaded shell-extension DLL

---

## Release Process (Intended — Not Yet Implemented)

1. Confirm all quality gate items are met.
2. Update version constant in source and version resource in `.rc`.
3. Create a signed git tag: `v0.1.0`
4. Push the tag to trigger CI release workflow.
5. CI builds the Release executable and uploads it to GitHub Release.
6. Generate SHA-256 checksum and attach to release notes.
7. Validate the artifact on a clean machine before marking the release final.
8. If a defect is found, create a patch release (`v0.1.1`) rather than overwriting the published artifact.
