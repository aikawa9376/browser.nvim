# JSON Lines IPC

Lua and `browserd` exchange one JSON object per line. Standard output is
reserved for IPC; daemon diagnostics go to standard error and CEF writes its
log below the configured profile directory.

## Browser lifecycle

`create` allocates daemon state, uploads the transparent anchor, and starts an
asynchronous CEF windowless browser. It includes both pixel and placement
geometry:

```json
{
  "type": "create",
  "browser_id": 1,
  "url": "https://example.com",
  "cols": 80,
  "rows": 24,
  "width": 800,
  "height": 480,
  "fps": 60,
  "dirty_rects": true,
  "max_dirty_rects": 32,
  "full_frame_threshold": 0.5,
  "anchor_image_id": 101,
  "anchor_placement_id": 102,
  "browser_image_id": 103,
  "browser_placement_id": 104
}
```

Two control messages extend the base specification because image placement and
CEF visibility are separate states:

- `attach`: recreate the relative placement. `full_frame=true` re-uploads the
  anchor and the last CEF frame so terminal eviction cannot leave a blank
  restored buffer. If the first frame has not arrived, CEF is invalidated and
  placement is created from the first `OnPaint()` callback.
- `detach`: delete only the browser placement using Kitty's lowercase `d=i`;
  image data and daemon/browser state remain alive.

`visibility` maps to CEF `WasHidden()`. `focus` maps independently to
`CefBrowserHost::SetFocus()`. `resize` updates both pixel dimensions and the
`c`/`r` placement geometry, then calls `WasResized()`. `destroy` closes the CEF
browser and frees browser and anchor images. `shutdown` waits for
`OnBeforeClose()` for all browsers before quitting CEF's message loop.

## Browser control

Navigation and viewport messages are `navigate`, `back`, `forward`, `reload`,
`stop`, `scroll`, and `scroll_to`. Commands received before CEF completes
browser creation are queued. Back and forward operations are serialized across
loads so rapid opposite commands cannot race Chromium's history state.

Link hint messages are:

```json
{"type":"hints_start","browser_id":1}
{"type":"hints_input","browser_id":1,"key":"a"}
{"type":"hints_cancel","browser_id":1}
```

The daemon responds with `mode_changed`, `hints_ready`, `hint_activated`,
`hints_cancelled`, or `hints_empty`. A focused form target changes the mode to
`insert`; a clicked target returns it to `normal`.

Browser Normal Mode exposes a persistent terminal-cell-sized spatial cursor.
For Normal Mode, `previous_grapheme`, `next_grapheme`, `up`, and `down` move one
cell even across empty page areas; the operation names remain stable for IPC
compatibility. Word operations jump to DOM words, while line-edge operations
move to viewport edges. `visual_cursor_start` begins a one-grapheme selection
at the nearest rendered text position:

```json
{"type":"cursor_move","browser_id":1,"operation":"next_word"}
{"type":"cursor_activate","browser_id":1}
{"type":"visual_cursor_start","browser_id":1}
```

`cursor_activate` clicks the actionable ancestor under the cursor. Text links
and buttons are activated when they overlap the spatial cell. If no actionable target exists,
the daemon emits `cursor_activate_unavailable`.

DOM Visual Mode then uses `visual_move`, `visual_yank`, and `visual_cancel`.
Move operations are
`previous_grapheme`, `next_grapheme`, `previous_word`, `next_word`, `up`,
`down`, `line_start`, `line_end`, and `swap`. A successful yank emits:

```json
{"type":"visual_yank","browser_id":1,"text":"selected text"}
```

The earlier hint-based entry remains available through `visual_start` with
`max_hints` followed by `visual_hint_input`; the Lua default uses the persistent
cursor path.

Browser Insert Mode accepts:

```json
{"type":"input_cursor_start","browser_id":1}
{"type":"input_start","browser_id":1}
{"type":"input_text","browser_id":1,"text":"日本語"}
{"type":"input_key","browser_id":1,"key":"Backspace","shift":false,"control":false,"alt":false}
{"type":"input_cancel","browser_id":1}
```

`input_cursor_start` asks the injected DOM controller to focus the editable
element under the Normal cursor. It emits `cursor_input_focused` and changes to
`insert`, or emits `cursor_input_unavailable` without changing mode.
`input_start` is retained for an element already focused by another operation,
such as a form hint.

Supported key names are `Enter`, `Backspace`, `Delete`, `Tab`, `Left`,
`Right`, `Up`, `Down`, `Home`, `End`, and lowercase letters combined with
modifiers. Text is UTF-8 in JSON and converted to CEF's UTF-16 character events
inside browserd.

Hint, visual, and insert messages are state checked. Navigation, reload,
renderer termination, buffer leave, and explicit cancellation restore normal
mode and clean up injected DOM overlays or input focus.

## Rendering updates

The initial frame is a complete BGRA-to-RGBA conversion and Kitty upload. CEF
dirty rectangles are clipped to the viewport and overlapping rectangles are
merged. More than `max_dirty_rects`, or a merged area above
`full_frame_threshold`, selects the full-frame path. Otherwise browserd
extracts only each converted region into POSIX shared memory and sends a Kitty
root-frame edit at its pixel `x`/`y` position. Popup paint rectangles are
translated into view coordinates and composited through the same retained
framebuffer.

## Daemon events

At startup, `browserd` emits `terminal_metrics`. It prefers the pixel fields
from `TIOCGWINSZ`, accepts `BROWSER_CELL_WIDTH`/`BROWSER_CELL_HEIGHT`
overrides, and otherwise reports the Phase 1 fallback of 10x20 pixels.

`page_ready` is emitted with `ready=false` when main-frame navigation starts
and with `ready=true` only after the new renderer context has installed the
browser bridge, cursor, hint, and Visual Mode scripts. UI clients gate DOM
commands on this event rather than the broader `loading` event; subresources
and advertising frames may keep loading after the main document is interactive.
When back/forward cache restores an existing renderer context, browserd asks
that context to repeat the handshake after navigation finishes. History-command
serialization therefore follows CEF navigation loading, not DOM readiness.

`created`, `destroyed`, `url_changed`, `title_changed`, `loading`, `page_ready`,
`mode_changed`, hint/visual results, and `error` events include a `browser_id`
when they concern one browser. A
`frame_ready` event is emitted only when the CEF view first paints or changes
pixel dimensions; it is not emitted for every frame. Lua schedules all editor
mutations outside the stream callback.

stdout remains JSON Lines only. CEF logs are written below the configured
profile directory, and pixel bytes travel directly from browserd memory to
Kitty through POSIX shared memory.
