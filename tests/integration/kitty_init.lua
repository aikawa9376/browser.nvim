local source = debug.getinfo(1, "S").source:sub(2)
local root = vim.fs.dirname(vim.fs.dirname(vim.fs.dirname(source)))

vim.opt.runtimepath:prepend(root)
vim.opt.termguicolors = true

require("browser").setup({
  browserd = vim.fs.joinpath(root, "build", "browserd", "browserd"),
})
