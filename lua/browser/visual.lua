local config = require("browser.config")
local ipc = require("browser.ipc")
local util = require("browser.util")

local M = {}

function M.start(state)
  if not config.get().visual.enabled then
    return false
  end
  if not state.ready or state.loading then
    util.notify("browser.nvim: page is still loading", vim.log.levels.WARN)
    return false
  end
  local sent = ipc.send({
    type = "visual_start",
    browser_id = state.browser_id,
    max_hints = config.get().visual.max_hints,
  })
  if sent then
    state.mode = "visual_hint"
  end
  return sent
end

function M.hint_input(state, key)
  ipc.send({
    type = "visual_hint_input",
    browser_id = state.browser_id,
    key = key,
  })
end

function M.move(state, operation)
  ipc.send({
    type = "visual_move",
    browser_id = state.browser_id,
    operation = operation,
  })
end

function M.swap(state)
  M.move(state, "swap")
end

function M.yank(state)
  ipc.send({
    type = "visual_yank",
    browser_id = state.browser_id,
  })
end

function M.cancel(state)
  ipc.send({
    type = "visual_cancel",
    browser_id = state.browser_id,
  })
  state.mode = "normal"
end

function M.handle_yank(text)
  vim.fn.setreg('"', text)
  vim.fn.setreg("0", text)
  if config.get().visual.clipboard then
    pcall(vim.fn.setreg, "+", text)
    pcall(vim.fn.setreg, "*", text)
  end
end

return M
