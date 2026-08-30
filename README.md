# browser.nvim

`browser.nvim` embeds a browser surface in an ordinary Neovim buffer. The
rendering path uses CEF off-screen rendering and the Kitty Graphics Protocol;
pixels never pass through Lua.

> [!IMPORTANT]
> The implementation covers Phases 1–7: browser-buffer lifecycle, real CEF
> rendering, a persistent spatial cursor in Browser Normal Mode, link hints,
> Browser Insert Mode, DOM visual selection, popup composition, and
> dirty-rectangle frame updates. Mouse input is intentionally outside the MVP.

## Requirements

- Linux
- Neovim 0.10+
- Kitty 0.31.0+
- CMake 3.21+ and a C++20 compiler
- `termguicolors`
- tmux 3.3+ with `allow-passthrough` set to `on` or `all` when tmux is used

```tmux
set -g allow-passthrough on
```

## Build

```bash
./scripts/fetch-cef.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CEF is not committed to Git. [`cef/manifest.env`](cef/manifest.env) pins
`151.3.11+gd08600e+chromium-151.0.7922.47` and its SHA-256; the fetch script
will never resolve a moving `latest` version. An existing distribution is also
supported:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCEF_ROOT=/absolute/path/to/cef_binary_...
```

On Linux the build stages `libcef.so`, Chromium helper libraries, ICU, PAK,
locale, and V8 snapshot files beside `browserd`, matching CEF's supported
runtime layout. The CEF archive and staged runtime are large and remain ignored
by Git. A CEF-free Phase 1 gradient regression build is available explicitly
with `-DBROWSER_ENABLE_CEF=OFF`.

The Lua plugin automatically finds `build/browserd/browserd` or
`browserd/build/browserd`. An installed `browserd` on `$PATH` is the final
fallback.

## Setup

```lua
{
  dir = "/path/to/browser.nvim",
  config = function()
    require("browser").setup({
      max_fps = 60,
    })
  end,
}
```

For a non-standard daemon location:

```lua
require("browser").setup({
  browserd = "/path/to/browserd",
  max_fps = 60,
  visual = {
    enabled = true,
    clipboard = false,
  },
  rendering = {
    dirty_rects = true,
    max_dirty_rects = 32,
    full_frame_threshold = 0.5,
  },
})
```

Run `:checkhealth browser` before the first manual test.

## Commands

```vim
:Browser [url]
:BrowserBack
:BrowserForward
:BrowserReload
:BrowserStop
:BrowserOpen
:BrowserClose
```

The browser buffer implements the intended lifecycle:

- `buftype=nofile`, `bufhidden=hide`, no swap file, and `filetype=browser`
- a single U+10EEEE Kitty anchor cell at the top-left
- one browser buffer to one daemon browser state
- relative placement removal on `BufWinLeave`
- the same CEF instance and page state while the buffer is hidden
- full-frame re-upload and placement recreation on `BufWinEnter`
- resource deletion only on `BufDelete`/`BufWipeout`
- duplicate display of one browser buffer is rejected
- daemon exit falls back to text; `r` restarts all retained browser states

## Keyboard operation

The default Browser Normal Mode mappings are:

| Key | Action |
| --- | --- |
| `h` / `l` | Move the spatial cursor left / right by one terminal cell |
| `b` / `w` | Jump the spatial cursor by DOM word |
| `j` / `k` | Move the spatial cursor down / up by one terminal cell |
| `0` / `$` | Move to the left / right edge of the viewport row |
| `<CR>` | Activate the link or button under the spatial cursor |
| `<C-d>` / `<C-u>` | Scroll half a viewport |
| `<C-f>` / `<C-b>` | Scroll one viewport |
| `gg` / `G` | Top / bottom |
| `H` / `L` | Back / forward |
| `r` | Reload |
| `o` | Edit the current URL and navigate |
| `f` | Start link hints |
| `v` | Start Browser Visual Mode near the spatial cursor |
| `i` | Enter Insert Mode when the spatial cursor overlaps an editable element |
| `yy` | Yank the current URL |

Browser Normal Mode keeps a terminal-cell-sized spatial cursor over the browser
surface, including completely empty page areas. It uses a solid yellow overlay
on ordinary content and a cyan outline when its cell overlaps a link or button,
so it remains visible on light and dark pages. `h/j/k/l` always move exactly one
cell; crossing a viewport edge scrolls the page by one cell. `w/b` remain
semantic DOM word jumps. When the cell overlaps an editable element, `i`
focuses it and enters both Browser and Neovim Insert Mode. `<C-w>` mappings are not overridden, so normal
Neovim window movement remains available. Link hints use labels generated from
`asdfghjkl`. Choosing an `input`, `textarea`, or `select` focuses it and enters
Browser Insert Mode.
Committed UTF-8 text and Enter, Backspace, Delete, Tab, Shift-Tab, arrows,
Home, End, Ctrl-A, Ctrl-C, and Ctrl-V are forwarded to CEF. `Esc` returns to
Browser Normal Mode.

The `o` prompt is intentionally prefilled with the current page URL, making it
possible to edit only its path or query. Use `<CR>` instead when the cursor is
already on a link or button in the page.

Browser Visual Mode starts at the nearest rendered text position to the Normal
Mode spatial cursor and uses a DOM `Range` for
the actual page selection while also entering Neovim's native characterwise
Visual mode. This keeps the editor mode indicator and Visual keymap context in
sync. The same text motions remain active:

| Key | Action |
| --- | --- |
| `h` / `l` | Previous / next Unicode grapheme |
| `b` / `w` | Previous / next word |
| `j` / `k` | Previous / next visual line, retaining preferred X |
| `0` / `$` | Visual line start / end |
| `o` | Swap selection endpoints |
| `y` | Yank selection to registers `"` and `0` |
| `Esc` | Cancel |

Set `visual.clipboard=true` to also populate `+` and `*`.

## Manual test

From this repository:

```bash
nvim --clean -u tests/integration/kitty_init.lua
```

Then run:

```vim
:Browser file:///absolute/path/to/browser.nvim/tests/pages/basic.html
:vsplit
:enew
```

Use `:bprevious` to return to the browser, resize and move the split with
`<C-w>H/J/K/L`, switch tabs, and finally run `:bdelete`. The Chromium-rendered
page must remain aligned to the browser window and must disappear while the
buffer is hidden or deleted. Exercise link hints on `tests/pages/links.html`,
input on `tests/pages/forms.html`, and Unicode visual selection on
`tests/pages/selection.html` and `tests/pages/graphemes.html`. See
[tests/integration/README.md](tests/integration/README.md) for the complete
checklist.

## Architecture

```text
Neovim Lua control plane
  buffer lifecycle / mappings / anchor / JSON Lines IPC
                         |
                         v
browserd C++20
  CEF OSR BGRA -> retained RGBA framebuffer -> POSIX SHM -> short Kitty APC
                         |
                         v
Kitty virtual U+10EEEE anchor <- relative browser placement
```

When `$TMUX` is present, each complete Kitty APC is escaped inside one tmux DCS
passthrough envelope and written to the Neovim UI TTY with one `write()`.
`browserd` prefers `/dev/tty`; on Linux it safely falls back to a verified TTY
descriptor from its parent Neovim process when Neovim has no controlling TTY.

The CEF UI thread owns all browser objects. A dedicated IPC reader parses JSON
Lines and posts work to that UI thread; CEF objects are never manipulated from
the reader thread. View and popup paints are kept separate, with `PET_POPUP`
composited over the root framebuffer before transmission.

The first paint and every reattach upload a full root frame. Later CEF dirty
rectangles are clipped and merged; excessive rectangles or a configured area
threshold fall back to a full upload. Other updates edit the existing Kitty
root frame with shared-memory region payloads, so image bytes never share the
PTY with Neovim's TUI output.

Link hints and visual selection are injected into each main-frame JavaScript
context. Renderer-to-browser messages are accepted only for the expected
browser, current main-frame URL, and active operation mode.

The browser placement uses Kitty z-index `-1`, keeping Neovim text, floating
window contents, and borders above the browser pixels. Floating-window
background cells do not fully mask the image, so the page may remain visible
behind otherwise empty areas of a float. Browser windows replace the
end-of-buffer `~` marker with a blank cell and restore the prior fill-character
settings when the buffer leaves the window.

CEF's Linux sandbox is enabled by default. `BROWSER_NO_SANDBOX=1` exists only
as an explicit diagnostic escape hatch and should not be used for normal web
browsing.

The IPC and placement lifecycle are documented in [docs/ipc.md](docs/ipc.md).

## MVP limitations

The same browser buffer cannot be displayed in two Neovim windows at once.
Mouse input, downloads UI, file pickers, DevTools UI, complete IME composition
UI, cross-origin iframe selection, closed Shadow DOM selection, canvas text,
and native PDF selection are not implemented. UTF-8 text already committed by
Neovim or a terminal IME is forwarded to CEF.

## License

MIT
