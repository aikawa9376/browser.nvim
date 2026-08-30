local browser = require("browser.browser")
local config = require("browser.config")

local M = {}
local commands_registered = false

local function open_with_prompt(url)
  if url and vim.trim(url) ~= "" then
    browser.open(url)
    return
  end
  vim.ui.input({ prompt = "URL: ", default = "https://" }, function(value)
    if value and vim.trim(value) ~= "" then
      browser.open(value)
    end
  end)
end

local function register_commands()
  if commands_registered then
    return
  end
  commands_registered = true

  vim.api.nvim_create_user_command("Browser", function(command)
    open_with_prompt(command.args)
  end, { nargs = "?", desc = "Open a browser buffer" })
  vim.api.nvim_create_user_command("BrowserBack", function()
    browser.command(nil, "back")
  end, { desc = "Navigate back" })
  vim.api.nvim_create_user_command("BrowserForward", function()
    browser.command(nil, "forward")
  end, { desc = "Navigate forward" })
  vim.api.nvim_create_user_command("BrowserReload", function()
    browser.reload()
  end, { desc = "Reload the browser" })
  vim.api.nvim_create_user_command("BrowserStop", function()
    browser.stop()
  end, { desc = "Stop loading" })
  vim.api.nvim_create_user_command("BrowserOpen", function()
    browser.prompt_navigate()
  end, { desc = "Open a URL in the current browser" })
  vim.api.nvim_create_user_command("BrowserClose", function()
    browser.close()
  end, { desc = "Delete the current browser buffer" })
end

function M.setup(opts)
  config.setup(opts)
  browser.setup()
  register_commands()
  return M
end

function M.open(url)
  return browser.open(url)
end

function M._load()
  M.setup({})
end

return M
