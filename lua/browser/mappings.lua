local config = require("browser.config")
local hints = require("browser.hints")
local visual = require("browser.visual")
local anchor = require("browser.anchor")

local M = {}

local function map(bufnr, lhs, callback, description, nowait)
  vim.keymap.set({ "n", "x" }, lhs, callback, {
    buffer = bufnr,
    silent = true,
    nowait = nowait ~= false,
    desc = "browser.nvim: " .. description,
  })
end

local function nmap(bufnr, lhs, callback, description, nowait)
  vim.keymap.set("n", lhs, callback, {
    buffer = bufnr,
    silent = true,
    nowait = nowait ~= false,
    desc = "browser.nvim: " .. description,
  })
end

local function imap(bufnr, lhs, callback, description)
  vim.keymap.set("i", lhs, callback, {
    buffer = bufnr,
    silent = true,
    nowait = true,
    desc = "browser.nvim: " .. description,
  })
end

function M.enter_insert(state)
  if not vim.api.nvim_buf_is_valid(state.bufnr) then
    return
  end
  vim.api.nvim_set_option_value("modifiable", true, { buf = state.bufnr })
  vim.schedule(function()
    if state.mode == "insert"
      and vim.api.nvim_get_current_buf() == state.bufnr
    then
      vim.api.nvim_win_set_cursor(0, { 1, 0 })
      pcall(vim.cmd, "startinsert")
    end
  end)
end

function M.leave_insert(state)
  if not vim.api.nvim_buf_is_valid(state.bufnr) then
    return
  end
  if vim.api.nvim_get_current_buf() == state.bufnr then
    pcall(vim.cmd, "stopinsert")
  end
  anchor.install(state.bufnr, state)
end

local function editor_is_visual()
  local mode = vim.api.nvim_get_mode().mode
  return mode == "v" or mode == "V" or mode == "\22"
end

function M.enter_visual(state)
  vim.schedule(function()
    if state.mode == "visual"
      and vim.api.nvim_get_current_buf() == state.bufnr
      and not editor_is_visual()
    then
      pcall(vim.cmd, "normal! v")
    end
  end)
end

function M.leave_visual(state)
  if vim.api.nvim_get_current_buf() == state.bufnr and editor_is_visual() then
    vim.api.nvim_feedkeys("\27", "nx", false)
  end
end

local function hint_input(state, key)
  if state.mode == "hint" then
    hints.input(state, key)
    return true
  end
  if state.mode == "visual_hint" then
    visual.hint_input(state, key)
    return true
  end
  return false
end

function M.attach(bufnr, state)
  local browser = require("browser.browser")
  local keys = config.get().keys

  map(bufnr, keys.scroll_down, function()
    if hint_input(state, "j") then
      return
    end
    if state.mode == "visual" then
      visual.move(state, "down")
    elseif state.mode == "normal" then
      visual.move_cursor(state, "down")
    end
  end, "move down")

  map(bufnr, keys.scroll_up, function()
    if hint_input(state, "k") then
      return
    end
    if state.mode == "visual" then
      visual.move(state, "up")
    elseif state.mode == "normal" then
      visual.move_cursor(state, "up")
    end
  end, "move up")

  map(bufnr, keys.half_down, function()
    if state.mode == "normal" then
      browser.scroll_fraction(state, 0.5)
    end
  end, "half page down")
  map(bufnr, keys.half_up, function()
    if state.mode == "normal" then
      browser.scroll_fraction(state, -0.5)
    end
  end, "half page up")
  map(bufnr, keys.page_down, function()
    if state.mode == "normal" then
      browser.scroll_fraction(state, 1.0)
    end
  end, "page down")
  map(bufnr, keys.page_up, function()
    if state.mode == "normal" then
      browser.scroll_fraction(state, -1.0)
    end
  end, "page up")

  map(bufnr, "gg", function()
    if state.mode == "normal" then
      browser.scroll_to(state, "top")
    end
  end, "scroll to top")
  map(bufnr, "G", function()
    if state.mode == "normal" then
      browser.scroll_to(state, "bottom")
    end
  end, "scroll to bottom")

  map(bufnr, keys.back, function()
    if state.mode == "normal" then
      browser.command(state, "back")
    end
  end, "back")
  map(bufnr, keys.forward, function()
    if state.mode == "normal" then
      browser.command(state, "forward")
    end
  end, "forward")
  map(bufnr, keys.reload, function()
    if state.mode == "normal" then
      browser.reload(state)
    end
  end, "reload")

  map(bufnr, "i", function()
    if state.mode == "normal" then
      browser.start_input(state)
    end
  end, "browser insert mode")

  map(bufnr, "<CR>", function()
    if state.mode == "normal" then
      browser.activate_cursor(state)
    end
  end, "activate cursor target")

  map(bufnr, keys.open, function()
    if state.mode == "visual" then
      visual.swap(state)
    elseif state.mode == "normal" then
      browser.prompt_navigate(state)
    end
  end, "open URL or swap visual endpoints")

  map(bufnr, keys.hints, function()
    if not hint_input(state, "f") and state.mode == "normal" then
      hints.start(state)
    end
  end, "link hints")

  map(bufnr, keys.visual, function()
    if state.mode == "normal" then
      visual.start(state)
    end
  end, "browser visual mode")

  nmap(bufnr, keys.yank_url, function()
    if state.mode == "normal" then
      browser.yank_url(state)
    end
  end, "yank URL", false)

  local visual_moves = {
    h = "previous_grapheme",
    l = "next_grapheme",
    w = "next_word",
    b = "previous_word",
    ["0"] = "line_start",
    ["$"] = "line_end",
  }
  for key, operation in pairs(visual_moves) do
    map(bufnr, key, function()
      if not hint_input(state, key) then
        if state.mode == "visual" then
          visual.move(state, operation)
        elseif state.mode == "normal" then
          visual.move_cursor(state, operation)
        end
      end
    end, "cursor " .. operation)
  end

  map(bufnr, "y", function()
    if state.mode == "visual" then
      visual.yank(state)
    end
  end, "yank visual selection", false)

  for _, key in ipairs({ "a", "s", "d", "g" }) do
    map(bufnr, key, function()
      hint_input(state, key)
    end, "hint input " .. key, key ~= "g")
  end

  map(bufnr, "<Esc>", function()
    if state.mode == "hint" then
      hints.cancel(state)
    elseif state.mode == "visual" or state.mode == "visual_hint" then
      visual.cancel(state)
    elseif state.mode == "insert" then
      browser.cancel_input(state)
    end
  end, "cancel browser mode")

  local input_keys = {
    ["<CR>"] = { key = "Enter" },
    ["<BS>"] = { key = "Backspace" },
    ["<Del>"] = { key = "Delete" },
    ["<Tab>"] = { key = "Tab" },
    ["<S-Tab>"] = { key = "Tab", shift = true },
    ["<Left>"] = { key = "Left" },
    ["<Right>"] = { key = "Right" },
    ["<Up>"] = { key = "Up" },
    ["<Down>"] = { key = "Down" },
    ["<Home>"] = { key = "Home" },
    ["<End>"] = { key = "End" },
    ["<C-a>"] = { key = "a", control = true },
    ["<C-c>"] = { key = "c", control = true },
    ["<C-v>"] = { key = "v", control = true },
  }
  for lhs, event in pairs(input_keys) do
    imap(bufnr, lhs, function()
      if state.mode == "insert" then
        browser.input_key(state, event)
      end
    end, "input " .. event.key)
  end
  imap(bufnr, "<Esc>", function()
    if state.mode == "insert" then
      browser.cancel_input(state)
    end
  end, "leave browser insert mode")
end

return M
