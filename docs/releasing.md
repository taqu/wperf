# Releasing wperf

This is the maintainer procedure for releases after the first structured
release. Keep the primary branch buildable and use the existing tag workflow;
do not replace assets or move an existing public tag.

## Version and release type

The canonical version is the `project(... VERSION ...)` value in
`CMakeLists.txt`. Generated Windows metadata, `--version` output, and
packaging checks derive from it. Use semantic versioning:

- patch (`0.1.1`) for bug, security, compatibility, or packaging fixes;
- minor (`0.2.0`) for meaningful backward-compatible features while pre-1.0;
- major (`1.0.0`) only for a deliberate stability/product milestone.

RC tags use `vX.Y.Z-rc1`, `vX.Y.Z-rc2`, and so on. Final tags use `vX.Y.Z`.

## Procedure

1. Create a focused branch and PR. Run Debug/Release builds and mandatory tests.
2. Merge only after normal CI passes and review is complete.
3. Decide patch versus minor scope and update `CHANGELOG.md`.
4. For a candidate, configure with `-DWPERF_VERSION_SUFFIX=-rc1`, run tests,
   verify `--version` and Windows metadata, and package/extract the ZIP.
5. For the final version, remove the suffix, verify the same checks, and use
   the exact Release workflow output.
6. Create an annotated `vX.Y.Z` tag on the validated commit and push it.
7. The release workflow builds/tests, validates the version, creates the ZIP
   and SHA-256 sidecar, and publishes both to GitHub Releases.
8. Download the published files, verify the checksum, run `--version`, and
   record the source commit, tag, workflow run, and hash.

Never force-update a public tag or silently replace an asset. If a released
defect is found, reproduce it, add a regression test, fix it on a branch, and
issue a new patch release through the same process.

## Post-release maintenance

Keep expensive secondary work on demand: no new resident polling, timers, or
background workers without a documented justification. Security-sensitive
changes require focused review and regression tests for identity validation,
process rights, native buffer bounds, and Lock Inspector worker lifetime.
