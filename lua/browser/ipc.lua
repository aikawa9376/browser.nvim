local config = require("browser.config")
local util = require("browser.util")

local M = {}

local process
local stdout_buffer = ""
local stderr_tail = ""
local event_handler
local exit_handler
local intentional_shutdown = false
local terminal_metrics

local function tmux_passthrough_ok()
  if not vim.env.TMUX or vim.env.TMUX == "" then
    return true
  end

  local result = vim.system({ "tmux", "show", "-gv", "allow-passthrough" }, {
    text = true,
  }):wait(1500)
  local value = vim.trim(result.stdout or "")
  if result.code == 0 and (value == "on" or value == "all") then
    return true
  end
  return false,
    "browser.nvim: tmux allow-passthrough is disabled.\n\n"
      .. "Add this to ~/.tmux.conf:\n\n"
      .. "    set -g allow-passthrough on"
end

function M.preflight()
  if vim.fn.has("nvim-0.10") ~= 1 then
    return false, "browser.nvim requires Neovim 0.10 or newer"
  end
  if not vim.o.termguicolors then
    return false, "browser.nvim requires 'termguicolors' for exact Kitty image IDs"
  end

  local in_kitty = (vim.env.KITTY_WINDOW_ID and vim.env.KITTY_WINDOW_ID ~= "")
    or (vim.env.TERM or ""):find("kitty", 1, true) ~= nil
  if not in_kitty then
    return false, "browser.nvim requires Kitty"
  end

  if vim.fn.executable("kitty") ~= 1 then
    return false, "browser.nvim requires the kitty executable in PATH"
  end
  local kitty_result = vim.system({ "kitty", "--version" }, { text = true }):wait(1500)
  local kitty_version = (kitty_result.stdout or ""):match("kitty%s+([%d.]+)")
  if kitty_result.code ~= 0
    or not kitty_version
    or not util.version_at_least(kitty_version, "0.31.0")
  then
    return false, "browser.nvim requires Kitty 0.31.0 or newer"
  end

  local passthrough, message = tmux_passthrough_ok()
  if not passthrough then
    return false, message
  end

  local executable = util.resolve_browserd(config.get().browserd)
  if vim.fn.executable(executable) ~= 1 then
    return false,
      ("browser.nvim: browserd is not executable: %s\nBuild it with cmake first."):format(executable)
  end
  local daemon_version = vim.system({ executable, "--version" }, { text = true }):wait(3000)
  if daemon_version.code ~= 0
    or not (daemon_version.stdout or ""):find("protocol=2", 1, true)
  then
    return false,
      ("browser.nvim: browserd is out of date: %s\nRebuild it with cmake."):format(executable)
  end
  return true, executable
end

local function dispatch_line(line)
  if line == "" then
    return
  end
  local ok, event = pcall(vim.json.decode, line)
  if not ok or type(event) ~= "table" then
    util.notify("browserd emitted invalid JSON: " .. line, vim.log.levels.ERROR)
    return
  end
  if event.type == "terminal_metrics" then
    terminal_metrics = event
  end
  if event_handler then
    vim.schedule(function()
      event_handler(event)
    end)
  end
end

local function on_stdout(err, data)
  if err then
    util.notify("browserd stdout error: " .. err, vim.log.levels.ERROR)
    return
  end
  if not data then
    return
  end
  stdout_buffer = stdout_buffer .. data
  while true do
    local newline = stdout_buffer:find("\n", 1, true)
    if not newline then
      break
    end
    local line = stdout_buffer:sub(1, newline - 1)
    stdout_buffer = stdout_buffer:sub(newline + 1)
    dispatch_line(line)
  end
end

local function on_stderr(_, data)
  if not data then
    return
  end
  stderr_tail = (stderr_tail .. data):sub(-8192)
end

function M.setup(handlers)
  handlers = handlers or {}
  event_handler = handlers.on_event
  exit_handler = handlers.on_exit
end

function M.start()
  if process and not process:is_closing() then
    return true
  end

  local ok, executable_or_error = M.preflight()
  if not ok then
    return false, executable_or_error
  end

  stdout_buffer = ""
  stderr_tail = ""
  intentional_shutdown = false
  local executable = executable_or_error
  local spawn_ok, result = pcall(vim.system, { executable }, {
    stdin = true,
    text = true,
    stdout = on_stdout,
    stderr = on_stderr,
    env = {
      BROWSER_PROFILE_DIR = config.get().profile_dir,
      BROWSER_CELL_WIDTH = config.get().cell_width,
      BROWSER_CELL_HEIGHT = config.get().cell_height,
    },
  }, function(completed)
    local expected = intentional_shutdown
    process = nil
    if exit_handler then
      vim.schedule(function()
        exit_handler(completed, expected, stderr_tail)
      end)
    end
  end)
  if not spawn_ok then
    process = nil
    return false, "browser.nvim: failed to start browserd: " .. tostring(result)
  end
  process = result
  return true
end

function M.send(message)
  if not process or process:is_closing() then
    return false, "browserd is not running"
  end
  local ok, encoded = pcall(vim.json.encode, message)
  if not ok then
    return false, encoded
  end
  process:write(encoded .. "\n")
  return true
end

function M.shutdown()
  if not process or process:is_closing() then
    process = nil
    return
  end
  intentional_shutdown = true
  M.send({ type = "shutdown" })
  process:write(nil)
end

function M.running()
  return process ~= nil and not process:is_closing()
end

function M.metrics()
  return terminal_metrics
end

return M
