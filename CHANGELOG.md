# Changelog

All notable project changes are recorded here. The project has not produced a
tagged release yet.

## Unreleased

### Added

- UE 5.8.1 source shading-model installer, rollback and smoke-test workflows.
- CPU numerical validation, image-space comparison and paired GPU reporting.
- SM5/SM6 isolated shader compilation and disassembly checks.
- Windows and Linux GitHub Actions validation.
- Compatibility, licensing, asset-provenance and release-gate documentation.

### Changed

- Replaced the six-fetch `E_avg` reference path with a validated two-fetch fast
  path while retaining a reference mode for paired profiling.
- Updated GitHub Actions to Node 24-compatible action versions.
- Reduced the tracked stb dependency to the two used headers and its license.

### Removed

- Unreal generated data, Visual Studio caches, native build outputs and
  generated project files from Git tracking.
