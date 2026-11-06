# Fork notes

This fork of [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine) adds three things on top of upstream.
Everything is opt-in; with default settings Sunshine behaves exactly like upstream.

| Change | Where | Default |
| ------ | ----- | ------- |
| VR window/app endpoints (`/vrwindows`, `/vrapps`, `/vrlaunch`, `/vrcompose`, `/vrfocus`) | `src/platform/windows/vr_windows.*`, `vr_compose.*`, `src/nvhttp.cpp` | endpoints always on (Windows); window-only capture off (`vr_compose`) |
| Per-window streams, several at once (`window-mode`) | `src/platform/windows/window_host.*`, `display_wgc.cpp`, `src/process.*`, `src/video.cpp`, `src/stream.cpp`, [docs/window_isolation.md](docs/window_isolation.md), [docs/vr_window_only.md](docs/vr_window_only.md) | off per app |
| Remote-priority mouse policy (`local_mouse_policy`) | `src/input_arbiter.*`, `src/platform/windows/local_input_guard.*`, [docs/configuration.md](docs/configuration.md) | `allow` |

## Commit layout (easy to rebase onto upstream)

1. `Import upstream LizardByte/Sunshine snapshot`
2. `Add VR window/app endpoints (sunshine-vr-patch)`
3. `Add per-window streams and optional remote-priority mouse policy`
4. `VR window-only: make it opt-in and block desktop fallback with software encoders`

The same changes are in `patches/` as `git format-patch` files (`0003` is the latest fix).

## Using it as a GitHub fork

```bash
# Option A: fork upstream on GitHub, then apply the patches
git clone https://github.com/<you>/Sunshine.git && cd Sunshine
git submodule update --init --recursive
git am /path/to/patches/*.patch

# Option B: push this repository as-is
git remote add origin https://github.com/<you>/Sunshine.git
git push -u origin main

# Track upstream later
git remote add upstream https://github.com/LizardByte/Sunshine.git
git fetch upstream && git rebase upstream/master
```

Note: the archive does not contain git submodules (`third-party/*`); run
`git submodule update --init --recursive` after cloning. Build instructions are in [docs/building.md](docs/building.md).

## Status

The new C++ was written without access to a Windows toolchain. The portable helpers
(`window_policy`) were compiled and run; the Windows-only files and the web UI changes still need a
build and a manual test on Windows before release.
