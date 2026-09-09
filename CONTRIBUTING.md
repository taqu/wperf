# Contributing

Contributions should preserve wperf's small, event-driven runtime and Windows
x64 scope. Start with the prerequisites and canonical commands in
[docs/build.md](docs/build.md).

Maintainer release procedures are in [docs/releasing.md](docs/releasing.md).
Security reports should follow [SECURITY.md](SECURITY.md).

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure --no-tests=error
```

Pull requests should include focused changes, passing Debug and Release builds
and tests, and documentation updates for user-visible behavior. Keep `/W4`,
`/WX`, `/permissive-`, and `/utf-8` clean; see [docs/code-quality.md](docs/code-quality.md).
Do not add background polling, privilege escalation, or destructive behavior
without an explicitly reviewed design. Update the changelog for meaningful
user-facing changes.
