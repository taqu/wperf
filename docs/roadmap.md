You are working on the Windows desktop performance monitoring tool `wperf`.

Phase 0 established the repository baseline and release scope.
Phase 1 established a reproducible Windows build environment.
Phase 2 established the top-level README and user-facing documentation.
Phase 3 introduced basic GitHub Actions CI.
Phase 4 established the compiler warning and static quality baseline.
Phase 5 established the automated test infrastructure.
Phase 6 implemented the Lock Inspector core using Windows Restart Manager.
Phase 7 exposed Lock Inspector through a CLI with human-readable and JSON output.
Phase 8 added the native handle-scan fallback for deeper inspection.
Phase 9 added the Lock Inspector GUI.
Phase 10 added explicit and safe process-control actions.
Phase 11 integrated Lock Inspector into the existing wperf right-click menu.
Phase 12 hardened UX, error handling, race handling, and cleanup.
Phase 13 completed the dedicated security review.
Phase 14 finalized release-facing documentation.

The application is now feature-complete and documentation-complete for the intended v0.1.0 scope.

This phase focuses on release automation and reproducible packaging.

Do not add new product features.
Do not change Lock Inspector behavior.
Do not add Explorer integration.
Do not add installer work.
Do not add code signing yet.
Do not publish a final GitHub Release yet.

# Phase 15 — Release CI and Packaging

## Goal

Create a reproducible release pipeline that builds, tests, packages, and validates the Windows x64 distribution artifact for `wperf`.

The main objectives are:

1. create a dedicated release workflow,
2. reuse the canonical build established in Phase 1,
3. run the mandatory test suite before packaging,
4. package only required runtime files,
5. generate a deterministic release archive,
6. generate a SHA-256 checksum,
7. expose release artifacts from CI,
8. validate the archive contents,
9. keep release automation separate from normal CI,
10. prepare the project for versioning, RC validation, and final GitHub Release.

This phase creates release artifacts.

It does not publish the final v0.1.0 GitHub Release.

---

# Core Principle

The release pipeline must automate the already-established local build and test process.

Do not create a CI-only build path.

Conceptually:

    canonical local build
        ↓
    canonical tests
        ↓
    staging directory
        ↓
    ZIP package
        ↓
    SHA-256
        ↓
    CI artifact

The release workflow should not compensate for undocumented build assumptions.

If release CI requires machine-specific workarounds, fix the underlying build configuration instead.

---

# Source of Truth

Read at minimum:

    README.md
    CHANGELOG.md
    docs/build.md
    docs/testing.md
    docs/release-policy.md
    docs/project-baseline.md
    .github/workflows/ci.yml
    current build configuration

Use the exact canonical build and test commands established by previous phases.

Do not duplicate outdated commands into the release workflow.

---

# 1. Inspect Existing Automation

Inspect:

    .github/workflows/

Determine whether any release/package workflow already exists.

Also inspect:

    scripts/
    cmake/
    packaging/
    dist/

or equivalent project directories.

Reuse working infrastructure where practical.

Do not create duplicate packaging systems.

---

# 2. Create a Dedicated Release Workflow

Create:

    .github/workflows/release.yml

unless an equivalent workflow already exists.

Keep:

    ci.yml

for normal PR/push validation.

The release workflow should be dedicated to producing release-ready artifacts.

Do not overload `ci.yml` with final packaging logic unless the repository already intentionally uses one workflow for both.

---

# 3. Initial Release Trigger Policy

Phase 15 must support safe validation before final release publication.

Prefer one or both of:

    workflow_dispatch
    version-tag trigger

For example, conceptually:

    workflow_dispatch:

and optionally:

    push:
      tags:
        - "v*"

However, do not automatically publish a GitHub Release in Phase 15.

If tag-triggered builds are added, they should only build/package/upload workflow artifacts for now.

Final GitHub Release creation belongs to Phase 18.

---

# 4. Use a Windows Runner

Use:

    windows-latest

and the canonical MSVC/x64 toolchain established in Phase 1.

Do not add Linux or macOS release jobs.

Do not add x86 packaging unless x86 is explicitly supported for v0.1.0.

The intended artifact is:

    Windows x64

---

# 5. Use Minimal Workflow Permissions

The Phase 15 workflow should normally require only:

    contents: read

if it uploads only GitHub Actions workflow artifacts.

Do not grant:

    contents: write
    packages: write
    id-token: write

unless actually needed.

GitHub Release publishing is not part of this phase.

---

# 6. Avoid Secrets

The release packaging workflow should not require secrets.

Do not add:

    signing certificate secrets
    PATs
    release tokens
    deployment credentials

Code signing belongs to future work unless already required by repository policy.

---

# 7. Clean Checkout

The release job must start from a clean repository checkout.

Do not depend on:

    previous build directories
    local generated files
    cached binaries
    developer machine state

The workflow must configure from scratch.

---

# 8. Canonical Release Configure

Use the exact canonical Phase 1 configure command.

For example only:

    cmake -S . -B build -A x64

Use the actual current command.

Do not maintain a separate release-only CMake configuration unless genuinely required.

---

# 9. Build Release

Build the canonical Release configuration.

For example only:

    cmake --build build --config Release

Do not build an unofficial packaging-specific binary with different compiler flags.

The package must contain the same Release build developers can reproduce locally.

---

# 10. Keep Debug Validation Out of Release Packaging Unless Useful

Normal CI already validates Debug.

The release workflow's critical configuration is:

    Release

It is acceptable to skip Debug in `release.yml` if `ci.yml` already enforces it.

Do not make the release workflow unnecessarily slow by duplicating every normal CI job.

However, the Release build must be independently validated.

---

# 11. Run Mandatory Tests

Before creating a package, run the mandatory automated test suite against the Release build.

Use the canonical Phase 5 test command.

For example only:

    ctest --test-dir build -C Release --output-on-failure

Use the actual command.

Packaging must not proceed if mandatory tests fail.

Expected flow:

    Build Release
        ↓
    Tests
        ↓
    Package

not:

    Build
        ↓
    Package
        ↓
    Tests

---

# 12. Verify the Built Executable

Before packaging, explicitly verify the expected `wperf.exe` exists.

Fail the workflow if it does not.

Use the actual output path established by the build system.

Do not silently package an empty staging directory.

---

# 13. Determine Runtime Dependencies

Use the finalized Phase 14 documentation and actual build output to determine all files required at runtime.

The release package should include only necessary files.

At minimum, likely:

    wperf.exe
    LICENSE

Potentially:

    runtime DLLs
    README.txt or README.md

depending on the actual runtime dependency model.

Do not include build-only files.

---

# 14. Do Not Assume DLL Requirements

Verify runtime dependencies.

Do not blindly copy:

    all DLLs from build directory

or:

    entire dependency directories

into the package.

This can accidentally ship:

    compiler tools
    debug libraries
    tests
    unrelated libraries
    duplicate DLLs

Package only runtime-required files.

---

# 15. Exclude Debug Runtime Artifacts

Do not include:

    .pdb

in the normal public package unless the release policy explicitly intends to distribute symbols.

Do not include:

    .obj
    .ilk
    .lib
    CMake files
    test executables
    generated project files

The normal release package should be small.

If symbol distribution is useful later, create a separate artifact in a future phase.

---

# 16. Create a Clean Staging Directory

Create an explicit temporary staging directory.

For example conceptually:

    dist/wperf-v0.1.0-windows-x64/

Populate it only with files intended for the package.

Do not ZIP the entire build output directory.

The staging directory should represent the exact distribution contents.

---

# 17. Avoid Hard-Coding Final Version Logic Prematurely

Phase 16 will establish canonical version metadata.

Until then, structure packaging so that the version can be injected or derived cleanly.

Do not scatter:

    0.1.0

across many scripts/workflow lines.

Use one temporary release-version input or derived variable.

The packaging system should be ready for Phase 16 version-source unification.

---

# 18. Package Naming

The intended artifact naming convention is:

    wperf-v<version>-windows-x64.zip

For the initial release:

    wperf-v0.1.0-windows-x64.zip

For RCs later:

    wperf-v0.1.0-rc1-windows-x64.zip

Design the workflow so pre-release identifiers work naturally.

Do not special-case only `0.1.0`.

---

# 19. Create ZIP Deterministically Where Practical

Use a reliable archive creation mechanism available on Windows runners.

Possible approaches include:

    PowerShell Compress-Archive
    CMake/CPack if already appropriate
    another existing project mechanism

Choose the simplest robust option.

Do not add a heavy packaging dependency merely to create ZIP files.

---

# 20. ZIP Root Layout

Prefer a clean archive structure.

Two acceptable layouts are:

## Flat

    wperf.exe
    LICENSE
    README.md

or:

## Single top-level directory

    wperf-v0.1.0-windows-x64/
        wperf.exe
        LICENSE
        README.md

Choose one and document it.

Prefer the simpler format consistent with project conventions.

Do not produce deeply nested build paths inside the ZIP.

Bad:

    build/Release/bin/wperf.exe

---

# 21. Include License

The distribution package must contain the project license.

This is a hard requirement.

If:

    LICENSE

is missing, fail the release-readiness assessment.

Do not generate a package intended for public distribution without the correct license file.

---

# 22. Include User Documentation Only If Useful

It is acceptable to include:

    README.md

or a concise distribution-specific:

    README.txt

inside the archive.

Do not include the entire `docs/` tree automatically unless that is the intended distribution model.

The GitHub repository already contains full documentation.

Prefer a small package.

---

# 23. Do Not Include Source Code in Binary Package

The primary binary ZIP should not contain the entire source repository.

GitHub can provide source archives separately.

Keep the binary artifact focused on running `wperf`.

---

# 24. Generate SHA-256

Generate a SHA-256 checksum for the ZIP.

Expected companion file:

    wperf-v0.1.0-windows-x64.zip.sha256

The checksum file should unambiguously identify the archive.

A typical format may be:

    <hash>  wperf-v0.1.0-windows-x64.zip

Use a standard SHA-256 implementation available on Windows.

For example, PowerShell `Get-FileHash` is acceptable.

---

# 25. Verify the Checksum

Do not only generate the checksum.

Re-read or recompute it where practical to validate the generated value.

At minimum ensure:

- the checksum operation succeeds,
- the output hash is non-empty,
- it corresponds to the final ZIP after all archive creation is complete.

Never calculate the checksum before the archive is finalized.

---

# 26. Validate ZIP Contents

After creating the archive, inspect its contents.

Verify:

    required executable present
    LICENSE present
    runtime DLLs present if needed
    no Debug binary
    no test binaries
    no build-system garbage
    no local paths
    no secrets

Fail packaging if required files are missing.

---

# 27. Validate the Packaged Executable, Not Only Build Output

Where practical, extract the newly created ZIP into a fresh temporary directory and validate the executable from there.

This is important.

The test should validate the artifact users will receive, not merely:

    build/Release/wperf.exe

Expected:

    package
        ↓
    extract
        ↓
    run packaged executable

---

# 28. Packaged CLI Smoke Test

From the extracted package, run non-interactive commands where practical.

At minimum:

    wperf.exe --help

If supported and deterministic:

    wperf.exe --version

may be added after Phase 16.

Lock Inspector CLI smoke testing may also be used with a controlled path if appropriate.

Do not require interactive GUI automation for release packaging.

---

# 29. Validate Missing Runtime DLL Problems

Running the executable from the extracted staging/package directory should reveal accidental dependencies on DLLs that were present only in the build environment.

If startup fails because a required runtime DLL is missing:

- identify it,
- add it only if redistribution is valid and required,
- update runtime dependency documentation.

Do not copy random DLLs until it works.

---

# 30. Runtime Dependency Licensing

For every third-party DLL shipped, verify:

- redistribution is permitted,
- required notices/licenses are included if necessary.

Do not ship a dependency without checking its distribution requirements.

If this cannot be established, treat it as a release blocker.

---

# 31. Package Architecture Validation

Verify the distributed executable is the intended architecture:

    x64

Do not accidentally ship an x86 binary.

Use an available tool or build metadata to confirm where practical.

---

# 32. Release Build Type Validation

Ensure the packaged executable came from:

    Release

not:

    Debug
    RelWithDebInfo unless explicitly intended

Do not infer this only from directory name if another verification mechanism exists.

---

# 33. Prevent Stale Artifact Reuse

Packaging must use artifacts produced during the current workflow run.

Do not package:

    checked-in executable
    previous CI artifact
    stale local ZIP

unless explicitly designed and validated.

The expected source is the current Release build.

---

# 34. Upload Workflow Artifacts

Upload at minimum:

    wperf-v<version>-windows-x64.zip
    wperf-v<version>-windows-x64.zip.sha256

as GitHub Actions workflow artifacts.

Use clear artifact names.

These are CI artifacts, not yet GitHub Release assets.

Do not present them in README as official public releases.

---

# 35. Artifact Retention

Use a reasonable Actions artifact retention period if project policy supports configuring one.

Do not keep every development packaging artifact forever unnecessarily.

Default retention is acceptable if there is no reason to customize it.

---

# 36. Do Not Publish a GitHub Release Yet

Do not use:

    gh release create
    softprops/action-gh-release
    GitHub release upload APIs
    contents: write

in Phase 15.

The purpose of this phase is:

    produce validated release artifacts

not:

    publish the final release

Phase 18 handles publication.

---

# 37. No Automatic Tag Creation

The workflow must not create Git tags.

Tags should be explicit project/version control actions.

Release automation may react to a tag, but it should not silently generate one in this phase.

---

# 38. Support Manual Packaging

Where practical, provide a local packaging path equivalent to CI.

For example:

    scripts/package.ps1

or another project-native mechanism.

This is strongly useful because it allows release artifacts to be reproduced locally.

The script should:

    accept/derive version
    locate Release binary
    create staging directory
    copy runtime files
    create ZIP
    create SHA-256
    validate output

Do not create a script if the build system already provides an equally simple canonical packaging command.

---

# 39. Local/CI Packaging Parity

Prefer:

    local packaging script
        ↑
        reused by
        ↓
    GitHub Actions

rather than duplicating packaging logic in YAML and PowerShell separately.

For example:

    release.yml
        ↓
    scripts/package.ps1

This reduces drift.

Do not put large packaging logic directly into GitHub Actions YAML if a small reusable script is clearer.

---

# 40. Packaging Script Inputs

A reasonable interface might be:

    .\scripts\package.ps1 -Version 0.1.0

or:

    .\scripts\package.ps1 -Version 0.1.0-rc1

The script should reject malformed/empty versions.

Phase 16 may later change how version is derived.

Keep the interface simple.

---

# 41. Do Not Modify Source Version Yet Unless Required

Version metadata unification belongs to Phase 16.

Phase 15 may consume an explicit version string for naming artifacts.

Do not start changing:

    VERSION resource
    --version implementation
    compile-time version macros

unless the packaging workflow cannot reasonably be created without a temporary input.

---

# 42. Safe Staging Cleanup

Before packaging:

    remove previous staging directory for the same target

but only under a clearly project-owned temporary/output path.

Do not use dangerous recursive deletion against user-provided arbitrary paths.

Packaging cleanup should be tightly scoped.

---

# 43. Safe Output Cleanup

Do not delete:

    repository root
    build source directory
    arbitrary user path

because a variable was empty or malformed.

Validate output paths before recursive deletion.

This is especially important in PowerShell packaging scripts.

---

# 44. Quote All Paths

Build and packaging paths may contain spaces.

Ensure scripts correctly handle paths such as:

    C:\Users\Test User\source\wperf

Use PowerShell path APIs/quoted arguments correctly.

Do not rely on the repository living in a space-free directory.

---

# 45. Unicode Paths

Where practical, ensure packaging works when repository/output paths contain Unicode characters.

Do not introduce ANSI path handling.

The Windows toolchain and PowerShell should remain Unicode-safe.

---

# 46. Validate Package From a Clean Directory

Local validation should not rely on DLL search paths pointing into the build tree.

Extract the ZIP into a directory outside:

    build/

and run from there.

This is a key acceptance criterion.

---

# 47. No Environment Variable Dependency

The packaged executable should not depend on project-specific environment variables to start.

If it does, document and evaluate whether that is acceptable for release.

A normal portable binary should not require developer environment configuration.

---

# 48. Release Notes Are Not Finalized Here

Do not create the final GitHub Release notes in Phase 15.

`CHANGELOG.md` can remain the source for future release notes.

Final release publication belongs to Phase 18.

---

# 49. Update docs/release-policy.md

Update the release policy to document the actual packaging pipeline.

Include:

    release workflow location
    packaging command
    artifact naming
    archive contents
    checksum format
    validation steps

Update status approximately to:

    Release CI/packaging: COMPLETE

only after actual validation.

---

# 50. Add Release Build Documentation

Create:

    docs/releasing.md

if no equivalent release-maintainer document exists.

This should be practical and concise.

Document:

## Local Packaging

Exact commands.

## CI Packaging

How to invoke `release.yml`.

## Artifact Names

For example:

    wperf-v<version>-windows-x64.zip
    wperf-v<version>-windows-x64.zip.sha256

## Package Contents

Document exact expected contents.

## Validation

Document:

    build
    tests
    package
    checksum
    extraction smoke test

## Publishing

State clearly:

    GitHub Release publication is performed in a later release step.

Do not document unimplemented final automation as if it already exists.

---

# 51. Update docs/project-baseline.md

Record:

    release workflow exists
    packaging mechanism exists
    release artifacts can be generated
    checksum generation exists
    package extraction validation exists

Use actual status only.

---

# 52. Update README Only Minimally

Do not rewrite README.

If necessary, update only statements about distribution status.

Until Phase 18:

    official v0.1.0 GitHub Release does not exist yet

Do not add final download links.

---

# 53. CHANGELOG Status

Do not mark:

    v0.1.0 released

in CHANGELOG.

It remains:

    Unreleased

or equivalent until the actual final release.

Phase 15 is packaging infrastructure only.

---

# 54. CI Workflow Separation

Normal:

    ci.yml

should remain optimized for PR/push validation.

Release:

    release.yml

should handle:

    Release build
    tests
    packaging
    checksum
    artifact validation
    artifact upload

Do not make every normal pull request generate public-style release packages unless there is a clear reason.

---

# 55. Dependency Caching Is Optional

Do not add complicated caches merely to speed up the release job.

If existing CI already uses safe dependency caching, reuse it where appropriate.

Correctness and reproducibility take priority over small speed improvements.

---

# 56. Action Version Pinning

Use maintained GitHub Actions versions consistent with current project policy.

Do not use obviously deprecated action versions.

Avoid unnecessary third-party actions when PowerShell/built-in tooling is sufficient.

Keep the release supply chain small.

---

# 57. Artifact Upload Action

Use the established GitHub first-party artifact action where practical.

Do not introduce third-party release/upload actions in Phase 15.

Again, these are workflow artifacts, not GitHub Release assets.

---

# 58. Validate Workflow YAML

Check:

    syntax
    indentation
    trigger behavior
    permissions
    paths
    PowerShell quoting
    artifact names

Use an existing linter if available.

Do not add a large dependency solely to lint one workflow.

---

# 59. Manual Workflow Trigger Validation

If repository access allows it, run:

    release.yml

through `workflow_dispatch`.

Use a development/pre-release version value if the workflow requires one.

Verify the actual GitHub-hosted job completes and produces the intended artifacts.

Do not claim remote PASS unless it actually ran.

---

# 60. Do Not Test Final Release Tag Yet Unless Desired

A manual workflow dispatch is sufficient for Phase 15 validation.

Do not create:

    v0.1.0

solely to test packaging.

The final tag belongs later.

If a temporary non-release test tag is used, clean it up carefully and document it.

Prefer manual dispatch.

---

# 61. Release Artifact Validation Checklist

The produced artifact must satisfy:

    ZIP exists
    checksum exists
    ZIP filename correct
    SHA-256 filename correct
    ZIP can be extracted
    wperf.exe present
    LICENSE present
    required runtime DLLs present
    no tests/build garbage present
    executable starts from extracted directory where practical
    CLI smoke test passes
    package is x64
    package uses Release binary

Treat failure in any required item as packaging failure.

---

# 62. Security Review of Packaging Script

Even though this is build automation, review the script for basic safety.

Check:

    recursive delete paths
    user-controlled version input
    path quoting
    file-copy destinations
    archive output path

Do not allow a malformed version such as:

    ..\..\

to escape the intended output directory.

Sanitize/validate version strings used in file paths.

---

# 63. Version Input Validation

Until Phase 16 provides the canonical version source, restrict package version input to a safe format compatible with intended versions.

Examples:

    0.1.0
    0.1.0-rc1

Reject path separators and dangerous filename characters.

Do not blindly interpolate arbitrary strings into output paths.

---

# 64. Reproducibility Expectations

Bit-for-bit identical ZIP files across runs are not required in this phase unless already easy to achieve.

The required reproducibility is:

    same source
    same canonical build
    same required package contents
    same naming rules
    same validation process

Document timestamps/archive metadata differences if relevant.

Do not overcomplicate packaging solely for byte-identical ZIP output.

---

# 65. Record Artifact Size

Record the generated ZIP size in the final report.

This helps identify accidental packaging regressions later.

Do not establish a strict size gate yet unless the project already has one.

A sudden unexpectedly huge package should be investigated.

---

# 66. Preserve Runtime Policy

Packaging changes must not modify runtime behavior.

Expected production runtime changes:

    new threads: 0
    new timers: 0
    new polling: 0
    new runtime features: 0

This phase should affect build/release infrastructure only.

---

# 67. Build Validation

Perform a clean local Release build using the canonical command.

Report:

    Release build: PASS / FAIL

Normal Debug validation should remain covered by existing CI, but running it locally is acceptable.

---

# 68. Test Validation

Run the mandatory Release tests before local packaging.

Report:

    Release tests: PASS / FAIL

Do not package a locally failing build.

---

# 69. Local Packaging Validation

Run the canonical local packaging process.

Report:

    staging: PASS / FAIL
    ZIP: PASS / FAIL
    checksum: PASS / FAIL
    archive validation: PASS / FAIL
    extraction: PASS / FAIL
    packaged CLI smoke test: PASS / FAIL / NOT VERIFIED

Use the actual generated paths.

---

# 70. GitHub-Hosted Validation

If possible, run the release workflow on GitHub.

Report:

    GitHub release workflow: PASS / FAIL / NOT VERIFIED

Also report whether both workflow artifacts were produced.

Do not confuse:

    workflow artifact

with:

    GitHub Release asset

---

# 71. Diff Review

The Phase 15 diff should primarily contain:

    .github/workflows/release.yml
    packaging script/configuration
    docs/releasing.md
    release-policy updates
    project-baseline updates

Potentially minimal README documentation adjustments.

Avoid:

    Lock Inspector code changes
    monitoring changes
    security-model changes
    version-resource overhaul
    Explorer integration
    installer work
    GitHub Release publication
    broad refactoring
    mass formatting

---

# Out of Scope

Do not implement any of the following in Phase 15:

    new wperf functionality
    Explorer context-menu integration
    shell-extension DLL
    arbitrary remote handle closing
    retry-delete functionality
    automatic UAC elevation
    Lock Inspector redesign
    installer
    MSI/MSIX packaging
    code signing
    auto-update
    final version-source unification
    final v0.1.0 Git tag
    final GitHub Release creation
    GitHub Release asset upload
    final release date assignment

These belong to later phases or future releases.

---

# Expected Deliverables

Likely additions include:

    .github/workflows/release.yml
    scripts/package.ps1            if appropriate
    docs/releasing.md

and updates to:

    docs/release-policy.md
    docs/project-baseline.md

Potentially:

    README.md
    CHANGELOG.md

only for small release-status consistency corrections.

---

# Final Report

Provide a concise final report containing:

## Files Added

List newly created files.

## Files Modified

List modified files.

## Release Workflow

Report:

    workflow path
    runner
    trigger(s)
    permissions

## Canonical Release Build

Provide the exact configure/build commands used.

## Tests

Provide the exact mandatory test command run before packaging.

Report:

    Release tests: PASS / FAIL

## Packaging Mechanism

Report whether packaging is implemented through:

    scripts/package.ps1
    CMake/CPack
    workflow-native PowerShell
    other

Explain the actual chosen mechanism briefly.

## Version Input

Report how the package version is currently provided or derived.

Note that canonical version unification remains Phase 16 if applicable.

## Artifact Names

Report the exact generated names.

For example:

    wperf-v0.1.0-windows-x64.zip
    wperf-v0.1.0-windows-x64.zip.sha256

Use the actual test version if Phase 15 validation used a pre-release/dev version.

## ZIP Contents

List the exact files included in the binary archive.

## Runtime Dependencies

List every non-system runtime DLL included.

If none:

    non-system runtime DLLs: NONE

only if verified.

## Artifact Validation

Report:

    ZIP created: PASS / FAIL
    ZIP extractable: PASS / FAIL
    wperf.exe present: PASS / FAIL
    LICENSE present: PASS / FAIL
    unexpected build files absent: PASS / FAIL
    x64 binary verified: PASS / FAIL / NOT VERIFIED
    Release binary verified: PASS / FAIL
    packaged executable smoke test: PASS / FAIL / NOT VERIFIED

## Checksum

Report:

    SHA-256 generated: YES / NO
    checksum verified: YES / NO

Include the generated checksum value in the report if useful, but do not hard-code it into documentation.

## Package Size

Report:

    ZIP size: <value>

## Local Validation

Report:

    clean Release build: PASS / FAIL
    Release tests: PASS / FAIL
    local packaging: PASS / FAIL
    extraction validation: PASS / FAIL

## GitHub-Hosted Validation

Report:

    release workflow: PASS / FAIL / NOT VERIFIED
    ZIP workflow artifact produced: YES / NO / NOT VERIFIED
    checksum artifact produced: YES / NO / NOT VERIFIED

## GitHub Release Status

Explicitly confirm:

    final GitHub Release created: NO
    final v0.1.0 published: NO

This phase must not publish the final release.

## Safety

Report:

    secrets required: NO expected
    code signing introduced: NO
    installer introduced: NO
    Explorer integration introduced: NO

## Runtime Behavior Confirmation

Explicitly confirm that this phase introduced no intentional runtime behavior change to `wperf`.

## Remaining Release Work

List:

    Phase 16 — Versioning and Windows Metadata
    Phase 17 — Release Candidate
    Phase 18 — GitHub Release
    Phase 19 — Post-Release Workflow

## Release Blockers

Report:

    NONE

or list every issue that prevents moving to Phase 16.

Do not begin Phase 16.