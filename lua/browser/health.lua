local config = require("browser.config")
local ipc = require("browser.ipc")
local util = require("browser.util")

local M = {}

local function nearest_existing(path)
  local candidate = path
  while candidate and candidate ~= "" and vim.fn.isdirectory(candidate) ~= 1 do
    local parent = vim.fs.dirname(candidate)
    if parent == candidate then
      return nil
    end
    candidate = parent
  end
  return candidate
end

function M.check()
  vim.health.start("browser.nvim")

  if vim.fn.has("nvim-0.10") == 1 then
    local version = vim.version()
    vim.health.ok(("Neovim %d.%d.%d"):format(version.major, version.minor, version.patch))
  else
    vim.health.error("Neovim 0.10 or newer is required")
  end

  local in_kitty = (vim.env.KITTY_WINDOW_ID and vim.env.KITTY_WINDOW_ID ~= "")
    or (vim.env.TERM or ""):find("kitty", 1, true) ~= nil
  if not in_kitty then
    vim.health.error("Kitty was not detected")
  elseif vim.fn.executable("kitty") == 1 then
    local result = vim.system({ "kitty", "--version" }, { text = true }):wait(1500)
    local version = (result.stdout or ""):match("kitty%s+([%d.]+)")
    if result.code == 0 and version and util.version_at_least(version, "0.31.0") then
      vim.health.ok("Kitty " .. version .. " (relative placements supported)")
    else
      vim.health.error("Kitty 0.31.0 or newer is required")
    end
  else
    vim.health.warn("Kitty environment detected, but the kitty executable is not in PATH")
  end

  if vim.o.termguicolors then
    vim.health.ok("termguicolors is enabled")
  else
    vim.health.error("termguicolors must be enabled")
  end

  local executable = util.resolve_browserd(config.get().browserd)
  if vim.fn.executable(executable) == 1 then
    vim.health.ok("browserd: " .. executable)
    local version_result = vim.system({ executable, "--version" }, { text = true }):wait(3000)
    local version = vim.trim(version_result.stdout or "")
    if version_result.code == 0
      and version:find("protocol=2", 1, true)
      and version:find("cef=", 1, true)
    then
      vim.health.ok(version)
    elseif version_result.code == 0
      and version:find("protocol=2", 1, true)
      and version:find("phase1-gradient", 1, true)
    then
      vim.health.error("browserd was built without CEF; rebuild after running scripts/fetch-cef.sh")
    elseif version_result.code == 0 then
      vim.health.error("browserd is out of date; rebuild it with cmake: " .. executable)
    else
      vim.health.error("browserd could not report its CEF version")
    end

    local runtime_dir = vim.fs.dirname(executable)
    local missing = {}
    for _, name in ipairs({ "libcef.so", "icudtl.dat", "resources.pak", "v8_context_snapshot.bin" }) do
      if vim.fn.filereadable(vim.fs.joinpath(runtime_dir, name)) ~= 1 then
        missing[#missing + 1] = name
      end
    end
    if vim.fn.isdirectory(vim.fs.joinpath(runtime_dir, "locales")) ~= 1 then
      missing[#missing + 1] = "locales/"
    end
    if #missing == 0 then
      vim.health.ok("CEF runtime files are staged beside browserd")
    else
      vim.health.error("CEF runtime files are missing beside browserd: " .. table.concat(missing, ", "))
    end
  else
    vim.health.error("browserd is missing or not executable: " .. executable)
  end

  if vim.env.TMUX and vim.env.TMUX ~= "" then
    local result = vim.system({ "tmux", "show", "-gv", "allow-passthrough" }, {
      text = true,
    }):wait(1500)
    local value = vim.trim(result.stdout or "")
    if result.code == 0 and (value == "on" or value == "all") then
      vim.health.ok("tmux allow-passthrough=" .. value)
    else
      vim.health.error("tmux allow-passthrough must be on or all")
    end
  else
    vim.health.ok("tmux not detected (direct Kitty session)")
  end

  local metrics = ipc.metrics()
  if metrics then
    local source = metrics.source and (" via " .. metrics.source) or ""
    vim.health.ok(("cell size = %dx%d%s"):format(metrics.cell_width, metrics.cell_height, source))
  elseif config.get().cell_width and config.get().cell_height then
    vim.health.ok(("configured cell size = %dx%d"):format(
      config.get().cell_width,
      config.get().cell_height
    ))
  else
    vim.health.warn("terminal pixel size is not known until browserd starts; fallback is 10x20")
  end

  local parent = nearest_existing(config.get().profile_dir)
  if parent and vim.fn.filewritable(parent) == 2 then
    vim.health.ok("profile directory can be created: " .. config.get().profile_dir)
  else
    vim.health.error("profile directory is not writable: " .. config.get().profile_dir)
  end
end

return M
