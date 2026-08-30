local anchor = require("browser.anchor")
local util = require("browser.util")

local M = {}

M.transparent_highlight = "BrowserNvimTransparent"

local buffer_options = {
  buftype = "nofile",
  bufhidden = "hide",
  swapfile = false,
  modifiable = false,
  filetype = "browser",
  modeline = false,
  undofile = false,
}

local window_options = {
  number = false,
  relativenumber = false,
  signcolumn = "no",
  foldcolumn = "0",
  wrap = false,
  cursorline = false,
  cursorcolumn = false,
  list = false,
  spell = false,
  colorcolumn = "",
  statuscolumn = "",
  scrolloff = 0,
  sidescrolloff = 0,
}

local function browser_name(url)
  local authority = url:match("^%a[%w+.-]*://([^/%?#]+)")
  if authority then
    return "browser://" .. authority
  end
  local scheme_value = url:match("^(%a[%w+.-]*:[^%s]+)")
  if scheme_value then
    return "browser://" .. scheme_value:gsub("[^%w._~-]", "-")
  end
  return "browser://new-tab"
end

local function name_in_use(name, except)
  for _, bufnr in ipairs(vim.api.nvim_list_bufs()) do
    if bufnr ~= except and vim.api.nvim_buf_is_valid(bufnr) then
      if vim.api.nvim_buf_get_name(bufnr) == name then
        return true
      end
    end
  end
  return false
end

local function unique_name(url, except)
  local base = browser_name(url)
  if not name_in_use(base, except) then
    return base
  end
  local suffix = 2
  while name_in_use(base .. "#" .. suffix, except) do
    suffix = suffix + 1
  end
  return base .. "#" .. suffix
end

function M.create(url, state)
  local bufnr = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_name(bufnr, unique_name(url))
  for option, value in pairs(buffer_options) do
    vim.api.nvim_set_option_value(option, value, { buf = bufnr })
  end
  anchor.install(bufnr, state)
  return bufnr
end

function M.rename(bufnr, url)
  if vim.api.nvim_buf_is_valid(bufnr) then
    vim.api.nvim_buf_set_name(bufnr, unique_name(url, bufnr))
  end
end

local function transparent_winhighlight(value)
  local items = {}
  local replaced = false
  for item in value:gmatch("[^,]+") do
    if item:match("^Normal:") then
      if not replaced then
        items[#items + 1] = "Normal:" .. M.transparent_highlight
        replaced = true
      end
    else
      items[#items + 1] = item
    end
  end
  if not replaced then
    table.insert(items, 1, "Normal:" .. M.transparent_highlight)
  end
  return table.concat(items, ",")
end

local function hidden_eob_fillchars(value)
  local items = {}
  local replaced = false
  for item in value:gmatch("[^,]+") do
    if item:match("^eob:") then
      if not replaced then
        items[#items + 1] = "eob: "
        replaced = true
      end
    else
      items[#items + 1] = item
    end
  end
  if not replaced then
    items[#items + 1] = "eob: "
  end
  return table.concat(items, ",")
end

function M.ensure_transparent_highlight()
  vim.api.nvim_set_hl(0, M.transparent_highlight, { bg = "NONE" })
end

function M.configure_window(winid, state)
  if not vim.api.nvim_win_is_valid(winid) then
    return
  end
  for option, value in pairs(window_options) do
    vim.api.nvim_set_option_value(option, value, { win = winid, scope = "local" })
  end
  M.ensure_transparent_highlight()
  if state.window_style and state.window_style.winid ~= winid then
    M.restore_window(state)
  end
  if not state.window_style then
    local winhighlight = vim.api.nvim_get_option_value("winhighlight", { win = winid })
    local fillchars = vim.api.nvim_get_option_value("fillchars", { win = winid })
    state.window_style = {
      winid = winid,
      winhighlight = winhighlight,
      applied_winhighlight = transparent_winhighlight(winhighlight),
      fillchars = fillchars,
      applied_fillchars = hidden_eob_fillchars(fillchars),
    }
  end
  if state.window_style.winid == winid then
    vim.api.nvim_set_option_value(
      "winhighlight",
      state.window_style.applied_winhighlight,
      { win = winid }
    )
    vim.api.nvim_set_option_value(
      "fillchars",
      state.window_style.applied_fillchars,
      { win = winid }
    )
  end
  M.pin_view(winid)
end

function M.restore_window(state)
  local style = state.window_style
  state.window_style = nil
  if style and vim.api.nvim_win_is_valid(style.winid) then
    vim.api.nvim_set_option_value("winhighlight", style.winhighlight, { win = style.winid })
    vim.api.nvim_set_option_value("fillchars", style.fillchars, { win = style.winid })
  end
end

function M.pin_view(winid)
  if not vim.api.nvim_win_is_valid(winid) then
    return
  end
  vim.api.nvim_win_call(winid, function()
    vim.fn.winrestview({ topline = 1, leftcol = 0, skipcol = 0, lnum = 1, col = 0 })
  end)
end

function M.windows(bufnr)
  local windows = {}
  for _, tabpage in ipairs(vim.api.nvim_list_tabpages()) do
    for _, winid in ipairs(vim.api.nvim_tabpage_list_wins(tabpage)) do
      if vim.api.nvim_win_is_valid(winid) and vim.api.nvim_win_get_buf(winid) == bufnr then
        windows[#windows + 1] = winid
      end
    end
  end
  return windows
end

function M.show_error(bufnr, lines)
  if not vim.api.nvim_buf_is_valid(bufnr) then
    return
  end
  anchor.clear(bufnr)
  vim.api.nvim_set_option_value("modifiable", true, { buf = bufnr })
  vim.api.nvim_buf_set_lines(bufnr, 0, -1, false, lines)
  vim.api.nvim_set_option_value("modifiable", false, { buf = bufnr })
end

function M.restore_anchor(bufnr, state)
  if vim.api.nvim_buf_is_valid(bufnr) then
    anchor.install(bufnr, state)
  end
end

function M.reject_duplicate(winid)
  if not vim.api.nvim_win_is_valid(winid) then
    return
  end
  local error_buf = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_set_option_value("buftype", "nofile", { buf = error_buf })
  vim.api.nvim_set_option_value("bufhidden", "wipe", { buf = error_buf })
  vim.api.nvim_set_option_value("swapfile", false, { buf = error_buf })
  vim.api.nvim_set_option_value("filetype", "browser-error", { buf = error_buf })
  vim.api.nvim_buf_set_lines(error_buf, 0, -1, false, {
    "browser.nvim:",
    "",
    "A browser buffer can only be displayed in one window at a time.",
  })
  vim.api.nvim_set_option_value("modifiable", false, { buf = error_buf })
  vim.api.nvim_win_set_buf(winid, error_buf)
  util.notify(
    "A browser buffer can only be displayed in one window at a time.",
    vim.log.levels.ERROR
  )
end

return M
