local function fail(message)
  error("browser.nvim test failure: " .. message, 2)
end

local function assert_equal(actual, expected, message)
  if not vim.deep_equal(actual, expected) then
    fail((message or "values differ")
      .. "\nexpected: " .. vim.inspect(expected)
      .. "\nactual: " .. vim.inspect(actual))
  end
end

local ipc = require("browser.ipc")
local browser = require("browser.browser")
local anchor = require("browser.anchor")
local buffer = require("browser.buffer")
local config = require("browser.config")
local util = require("browser.util")
local visual = require("browser.visual")

assert(util.version_at_least("0.48.2", "0.31.0"), "newer Kitty version must pass")
assert(not util.version_at_least("0.30.9", "0.31.0"), "older Kitty version must fail")

local configured_fps = config.get().max_fps
assert(not pcall(config.setup, { max_fps = 0 }), "invalid configuration must fail")
assert_equal(config.get().max_fps, configured_fps, "invalid configuration must not replace active options")

local sent = {}
ipc.start = function()
  return true
end
ipc.send = function(message)
  sent[#sent + 1] = vim.deepcopy(message)
  return true
end
ipc.metrics = function()
  return { cell_width = 9, cell_height = 18, source = "test" }
end

local function count_messages(kind)
  local count = 0
  for _, message in ipairs(sent) do
    if message.type == kind then
      count = count + 1
    end
  end
  return count
end

vim.api.nvim_set_option_value("winhighlight", "CursorLine:Visual", { win = 0 })
vim.api.nvim_set_option_value("fillchars", "vert:!,eob:~", { scope = "global" })
local state = assert(browser.open("example.com"))
vim.wait(100, function()
  return count_messages("attach") > 0
end)

assert_equal(state.url, "https://example.com", "URL normalization")
assert_equal(vim.api.nvim_buf_get_name(state.bufnr), "browser://example.com", "buffer name")
assert_equal(vim.bo[state.bufnr].buftype, "nofile", "buftype")
assert_equal(vim.bo[state.bufnr].bufhidden, "hide", "bufhidden")
assert_equal(vim.bo[state.bufnr].swapfile, false, "swapfile")
assert_equal(vim.bo[state.bufnr].modifiable, false, "modifiable")
assert_equal(vim.bo[state.bufnr].filetype, "browser", "filetype")
assert_equal(vim.api.nvim_buf_get_lines(state.bufnr, 0, 1, false)[1], anchor.anchor_text, "anchor text")
assert_equal(state.pixel_width, vim.api.nvim_win_get_width(0) * 9, "pixel width")
assert_equal(state.pixel_height, vim.api.nvim_win_get_height(0) * 18, "pixel height")
assert_equal(
  vim.api.nvim_get_option_value("winhighlight", { win = 0 }),
  "Normal:" .. buffer.transparent_highlight .. ",CursorLine:Visual",
  "browser window must use a transparent Normal background and preserve mappings"
)
assert(vim.tbl_isempty(vim.api.nvim_get_hl(0, { name = buffer.transparent_highlight })),
  "browser transparent highlight must not define a background")
assert_equal(
  vim.api.nvim_get_option_value("fillchars", { win = 0 }),
  "vert:!,eob: ",
  "browser window must hide end-of-buffer markers and preserve fill characters"
)
assert(count_messages("create") == 1, "create must be sent once")
assert(count_messages("visibility") >= 1, "visibility must be sent")
assert(count_messages("attach") == 1, "first attach must be sent")

local function feed(keys)
  local encoded = vim.api.nvim_replace_termcodes(keys, true, false, true)
  vim.api.nvim_feedkeys(encoded, "mx", false)
  vim.wait(100)
end

local message_count = #sent
feed("gg")
assert(#sent == message_count + 1, "gg must emit exactly one command")
assert_equal(sent[#sent].type, "scroll_to", "gg command")
assert_equal(sent[#sent].edge, "top", "gg edge")

feed("G")
assert_equal(sent[#sent].type, "scroll_to", "G command")
assert_equal(sent[#sent].edge, "bottom", "G edge")

feed("j")
assert_equal(sent[#sent].type, "scroll", "j command")
assert_equal(sent[#sent].dy, 120, "j scroll step")

feed("H")
assert_equal(sent[#sent].type, "back", "H navigation")
feed("L")
assert_equal(sent[#sent].type, "forward", "L navigation")

state.ready = true
feed("i")
assert_equal(sent[#sent].type, "input_start", "i must request browser insert mode")

state.mode = "insert"
local mappings = require("browser.mappings")
mappings.enter_insert(state)
vim.wait(50)
assert_equal(vim.bo[state.bufnr].modifiable, true, "insert mode makes the anchor buffer writable")
local before_text = #sent
-- startinsert takes effect after the current headless Lua chunk returns. The
-- leading sentinel advances that pending transition; a real user key arrives
-- on a later main-loop iteration and does not need it.
feed("x日本語")
local committed = {}
for index = before_text + 1, #sent do
  if sent[index].type == "input_text" then
    committed[#committed + 1] = sent[index].text
  end
end
assert_equal(table.concat(committed), "日本語", "InsertCharPre must forward committed UTF-8")
assert_equal(state.mode, "normal", "an external InsertLeave must cancel browser insert mode")
assert_equal(sent[#sent].type, "input_cancel", "InsertLeave cancellation message")
state.mode = "insert"
mappings.enter_insert(state)
assert_equal(browser.input_key(state, { key = "Backspace" }), true, "special input key must be forwarded")
assert_equal(sent[#sent].type, "input_key", "special input message type")
assert_equal(sent[#sent].key, "Backspace", "special input key payload")
assert_equal(browser.cancel_input(state), true, "insert mode cancellation")
assert_equal(sent[#sent].type, "input_cancel", "input cancel message")
assert_equal(state.mode, "normal", "input cancellation restores browser normal mode")
assert_equal(vim.bo[state.bufnr].modifiable, false, "leaving insert mode restores an immutable anchor")

vim.fn.setreg('"', "")
feed("yy")
assert_equal(vim.fn.getreg('"'), state.url, "yy must yank the current URL")

local selected_text = "これは日本語です。 👨‍👩‍👧‍👦 é"
visual.handle_yank(selected_text)
assert_equal(vim.fn.getreg('"'), selected_text, "visual yank unnamed register")
assert_equal(vim.fn.getreg("0"), selected_text, "visual yank register zero")

local other = vim.api.nvim_create_buf(true, false)
vim.api.nvim_win_set_buf(0, other)
vim.wait(50)
assert_equal(
  vim.api.nvim_get_option_value("winhighlight", { win = 0 }),
  "CursorLine:Visual",
  "leaving the browser must restore the window highlight"
)
assert_equal(
  vim.api.nvim_get_option_value("fillchars", { win = 0 }),
  "vert:!,eob:~",
  "leaving the browser must restore fill characters"
)
assert(count_messages("detach") >= 1, "buffer leave must detach placement")
assert(browser._states()[state.bufnr] == state, "hidden state must be retained")

vim.api.nvim_win_set_buf(0, state.bufnr)
vim.wait(100, function()
  return count_messages("attach") >= 2
end)
assert(count_messages("attach") >= 2, "buffer re-entry must reattach")

local windows_before = #buffer.windows(state.bufnr)
assert_equal(windows_before, 1, "browser must start in one window")
vim.cmd("vsplit")
vim.wait(150, function()
  return #buffer.windows(state.bufnr) == 1
end)
assert_equal(#buffer.windows(state.bufnr), 1, "duplicate browser window must be rejected")
if #vim.api.nvim_tabpage_list_wins(0) > 1 then
  vim.cmd("close")
end

local delete_buf = state.bufnr
vim.api.nvim_win_set_buf(0, other)
vim.api.nvim_buf_delete(delete_buf, { force = true })
vim.wait(50)
assert(browser._states()[delete_buf] == nil, "BufDelete must drop Lua state")
assert(count_messages("destroy") == 1, "BufDelete must destroy browserd state")

print("browser.nvim Lua tests: OK")
vim.cmd("qa!")
