local ipc = require("browser.ipc")
local util = require("browser.util")

local M = {}

function M.start(state)
  if not state.ready or not state.page_ready then
    util.notify("browser.nvim: page is still loading", vim.log.levels.WARN)
    return false
  end
  local sent = ipc.send({
    type = "hints_start",
    browser_id = state.browser_id,
  })
  if sent then
    state.mode = "hint"
  end
  return sent
end

function M.input(state, key)
  ipc.send({
    type = "hints_input",
    browser_id = state.browser_id,
    key = key,
  })
end

function M.cancel(state)
  ipc.send({
    type = "hints_cancel",
    browser_id = state.browser_id,
  })
  state.mode = "normal"
end

return M
