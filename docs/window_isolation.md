# Window streaming (several apps, one window each) — Windows only

Window mode turns an application into its own independent stream: Sunshine captures **that app's window
only** (with Windows.Graphics.Capture `CreateForWindow`), so nothing from any other window can ever appear in
it, even when other windows are on top of it on the host. Several window-mode apps can be streamed **at the
same time**, each by its own client session, and each stream ends when its window is closed.

It is off by default; enable it per application.

## How it works

* Each window-mode app gets its own process manager, so starting one never stops another, and never reverts
  the display configuration or resets gamepads used by the others.
* Each captured window gets its own capture thread, display and encoder session. The stream size follows the
  window; if the window is resized the stream is rebuilt for the new size.
* Input from a client is delivered to its window: before mouse, keyboard or touch input is injected, the
  window is brought to the front. The window is kept where it was found so click coordinates stay correct
  (drag it while streaming and Sunshine puts it back; resizing is fine).
* If the window is closed (a 1.5 s grace period allows a splash screen to be replaced by the main window), that
  session ends and the app is cleaned up. Launching the same app again re-attaches to the running app.

## `apps.json` fields

| Field          | Type    | Default | Meaning                                                                                 |
| -------------- | ------- | ------- | --------------------------------------------------------------------------------------- |
| `window-mode`  | boolean | `false` | Stream this app's window on its own.                                                    |
| `window-match` | string  | `""`    | Case-insensitive title fragment for apps that hand off to another process (launchers).  |

```json
{
  "name": "Notepad (own window)",
  "cmd": "notepad.exe",
  "window-mode": true
}
```

## Limits

* The client must be able to run several streams at once (one launch per app). Stock Moonlight opens one
  stream at a time; a custom client such as a VR client is needed to show several windows together.
* Audio is not separated: every stream carries the host's normal audio output.
* Using two clients on two windows at the same moment makes Windows switch the front window back and forth for
  each input; keyboard focus follows the most recent input.
* A minimized window cannot be captured, so Sunshine restores it automatically.
* Only the Windows.Graphics.Capture backend is used for window streams, and the encoder must be a hardware or
  software encoder with parallel encoding (all default ones).
* "Cancel" in a client (Moonlight's quit) ends all window apps.

## VR composed mode (window-only)

When `vr_compose` is enabled, the VR display path is **window-only**. Sunshine captures only the HWNDs selected by `/vrcompose` using Windows.Graphics.Capture. The normal desktop capture path is not used as a fallback if VR composition cannot start; the session fails instead. This prevents the desktop or unrelated windows from leaking into the VR stream.

The composed image is an atlas containing only the selected windows. Empty areas are cleared to black; they never contain the host desktop.
