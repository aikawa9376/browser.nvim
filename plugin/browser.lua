if vim.g.loaded_browser_nvim == 1 then
  return
end

vim.g.loaded_browser_nvim = 1
require("browser")._load()
