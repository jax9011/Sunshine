# VR window-only mode (Windows)

Server side of a window-only VR workflow. It is meant for a custom Moonlight VR client that shows each
window as its own panel. A stock Moonlight client shows the windows packed side by side in one image.

It is **off by default**. Enable it with `vr_compose = enabled` (see [configuration](configuration.md#vr_compose)).

## Guarantees

- Each selected window (HWND) is captured on its own with Windows.Graphics.Capture. A window that overlaps
  another on the PC never appears inside it.
- The encoded image is an atlas made only from the selected windows. Unused space is black.
- Desktop or monitor capture is **never** used in this mode. If the window capture cannot start, or the
  encoder is not a hardware (DXGI) encoder, the stream fails instead of falling back to the desktop.
- Dialogs and popups owned by a window are drawn inside the slot of that window.
- The atlas grows from 1x to 3x the screen size per side when many windows are open. Windows that still do
  not fit are dropped, newest first. At most 16 windows are composed.

## Control endpoints (HTTPS, paired clients only)

| Endpoint | Purpose |
| -------- | ------- |
| `/vrwindows` | Lists windows, their desktop rectangles and their atlas rectangles (`ax`, `ay`, `aw`, `ah`). |
| `/vrcompose?ids=<hwnd,hwnd,...>` | Selects a fixed set of windows. `ids=` (empty) returns to automatic selection of all eligible windows. |
| `/vrfocus?id=<hwnd>` | Chooses the window that receives input. |
| `/vrapps`, `/vrlaunch?id=<n>` | Lists and starts Start Menu apps. Only ids from the last `/vrapps` scan are accepted. |

Absolute pointer input is translated from atlas coordinates to the focused window's real position.

## Limits

- A minimized window cannot be captured and is skipped.
- Input brings the focused window to the front on the host, so windows are not isolated for input.
- Audio is not separated.
- All windows share one encoded image, so resolution per window falls as the number of windows grows.
- A VR client that cuts the atlas into panels is not part of this repository.

For one independent stream per application, see [window_isolation.md](window_isolation.md).
