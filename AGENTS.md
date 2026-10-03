# Repository Guidelines

## Project Structure & Module Organization

`obs-ssp` receives ZCam audio/video in OBS Studio through Simple Stream Protocol (SSP).

- `src/`: OBS source registration, FFmpeg decoding, frame queues, mDNS discovery, and SSP client integration. `src/controller/` contains Qt camera controls; `src/util/` contains platform-specific process pipes.
- `ssp_connector/`: standalone `ssp-connector` process and its IPC protocol. Keep protocol changes consistent with `src/ssp-client-iso.*`.
- `lib/ssp/include/`: SSP headers; `thirdpty/mdns/`: mDNS submodule.
- `data/locale/`: translated strings in locale-named INI files; `images/`: README assets.
- `cmake/`, `CMakePresets.json`, and `buildspec.json`: build configuration and dependency versions. `.github/` contains CI, build, and packaging scripts.

## Build, Test, and Development Commands

Run commands from the repository root. Initialize dependencies with `git submodule update --init --recursive`.

- Windows: `pwsh -File .github/scripts/Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo` prepares dependencies, builds, and stages files in `release/RelWithDebInfo/`. Requires PowerShell 7 and Visual Studio 2022.
- Windows incremental build: `cmake --preset windows-x64`, then `cmake --build --preset windows-x64`.
- macOS: `cmake --preset macos`, then `cmake --build --preset macos`; uses Xcode and produces a universal build.
- Format changed C/C++ files with `clang-format -i src/path.cpp`; check with `clang-format --dry-run --Werror src/path.cpp`. Use version 13 to match CI.

The build requires libobs, Qt 6, FFmpeg, and fetched libssp. Linux presets exist, but connector library linkage currently covers Windows and macOS only.

## Coding Style & Naming Conventions

Use C++17 and follow neighboring code. `.clang-format` specifies tabs with width 8, an 80-column limit, and function braces on separate lines. Use PascalCase for classes, snake_case for C/OBS functions, and existing camelCase conventions in Qt controllers. Match adjacent filenames. Format CMake with `cmake-format`, using `.cmake-format.json` (two-space indentation); its CI check is currently disabled.

## Testing Guidelines

No repository-owned automated test suite or coverage threshold is configured. Build each affected platform and manually validate in OBS: add an SSP Source, connect to a camera, confirm audio/video, reconnect, remove the source, and close OBS. Check logs and connector shutdown. Record platform, OBS version, and reproduction steps.

## Commit & Pull Request Guidelines

Recent commits commonly use `type(scope): description`, such as `fix(ssp_connector): close faster` and `chore(deps): update dependencies`. Keep commits focused. PRs should explain behavior changes, link relevant issues, report build/manual checks, and include screenshots for source-property UI changes. Preserve passing formatting and build checks; exclude generated builds, dependency caches, and signing credentials.
