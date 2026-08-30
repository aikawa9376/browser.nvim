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
9. Run `:bdelete` on the browser buffer. Both anchor and browser resources must
   disappear.
10. Open `links.html`, press `f`, and activate both a link and a form target
    without using a mouse. Also move the Normal cursor onto a link and press
    `<CR>`; it must navigate without opening the `o` URL prompt. `Esc` must
    remove an unfinished hint overlay.
11. On `forms.html`, move the Normal cursor onto the outlined input, press `i`,
    and verify that Neovim enters Insert mode. Enter Japanese and emoji text,
    use editing keys, and return to both Browser and Neovim Normal Mode with
    `Esc`. Repeat once by selecting the input through `f` hints.
12. On `selection.html`, verify that a block cursor is always visible. Move it
    with `h/l/w/b/j/k/0/$`, press `v`, and verify Neovim's mode indicator also
    changes to Visual. Extend or
    reverse the selection with `h/l/w/b/j/k/0/$/o`, and press `y`. Verify the
    result with `:registers`.
13. Repeat visual selection on `graphemes.html`; family emoji, combining text,
    and Japanese graphemes must not be split.
14. Open `popup.html` and exercise a select popup, then open `animation.html`.
    Popup pixels and animated dirty updates must remain inside the surface.
15. Repeat once directly in Kitty and once through Kitty → tmux → Neovim.

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
