# Security Policy

Please report suspected security issues privately to the project maintainer
before opening a public issue. Include the affected version, Windows version,
reproduction steps, and impact; do not attach confidential data.

wperf does not automatically elevate, enable `SeDebugPrivilege`, close remote
handles, or use `DUPLICATE_CLOSE_SOURCE`. Force Terminate is an explicit,
identity-validated GUI action and may lose unsaved data. See
[docs/security.md](docs/security.md) for the complete security model.
