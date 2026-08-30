# Kitty + CEF integration checklist

Build first, then start a clean editor in Kitty:

```bash
./scripts/fetch-cef.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
nvim --clean -u tests/integration/kitty_init.lua
```

When testing through tmux, verify `tmux show -gv allow-passthrough` prints `on`
or `all` before starting Neovim.

1. Run `:checkhealth browser` and resolve every error.
2. Run `:Browser file:///absolute/path/to/browser.nvim/tests/pages/basic.html`.
   The page title and styled HTML must be rendered by Chromium; a gradient is
   a Phase 1 build and is a failure here.
3. Create a split, put another buffer in it, and resize in both directions.
4. Move the browser window with `<C-w>H`, `<C-w>J`, `<C-w>K`, and `<C-w>L`.
5. Switch the browser window to another buffer. The image must disappear.
6. Return with `:bprevious`. The same Chromium page must reappear at the
   correct size and location without a new browser instance.
7. Move the browser buffer to another tab and return.
8. Attempt to show the same browser buffer in two windows. The second window
   must show the single-window restriction message, never a second surface.
9. Open a bordered Neovim floating window over the browser. Its full rectangle
   must remain visible without hiding the rest of the page; moving or closing
   it must restore the current browser pixels without a stale patch.
10. Run `:bdelete` on the browser buffer. Both anchor and browser resources must
   disappear.
11. Open `links.html`, press `f`, and activate both a link and a form target
    without using a mouse. `Esc` must remove an unfinished hint overlay.
12. On `forms.html`, select the input hint, enter Japanese and emoji text, use
    editing keys, and return to Browser Normal Mode with `Esc`.
13. On `selection.html`, press `v`, choose a text hint, move with
    `h/l/w/b/j/k/0/$/o`, and press `y`. Verify the result with `:registers`.
14. Repeat visual selection on `graphemes.html`; family emoji, combining text,
    and Japanese graphemes must not be split.
15. Open `popup.html` and exercise a select popup, then open `animation.html`.
    Popup pixels and animated dirty updates must remain inside the surface.
16. Repeat once directly in Kitty and once through Kitty → tmux → Neovim.

When `DISPLAY` is available, CTest also runs
`tests/integration/cef-smoke.sh`. It disables Kitty writes only for the test,
loads only local pages, and verifies initial/resized frames, lifecycle and
history state, link hints, UTF-8 form input, DOM visual selection (including
emoji, combining text, and Japanese), and clean destruction. Protocol/unit
tests separately verify exact Kitty root-frame edit commands and dirty-region
framebuffer extraction.

The local test pages can be served independently with:

```bash
python3 -m http.server 8765 --directory tests/pages
```
