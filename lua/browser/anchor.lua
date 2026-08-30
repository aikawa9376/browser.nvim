local M = {}

M.namespace = vim.api.nvim_create_namespace("browser.nvim.anchor")
M.placeholder = vim.fn.nr2char(0x10eeee)
local zero_diacritic = vim.fn.nr2char(0x0305)
M.anchor_text = M.placeholder .. zero_diacritic .. zero_diacritic

local function rgb(value)
  return ("#%06x"):format(value % 0x1000000)
end

function M.highlight_name(state)
  return ("BrowserNvimAnchor_%06x_%06x"):format(
    state.anchor_image_id % 0x1000000,
    state.anchor_placement_id % 0x1000000
  )
end

function M.install(bufnr, state)
  local group = M.highlight_name(state)
  vim.api.nvim_set_hl(0, group, {
    fg = rgb(state.anchor_image_id),
    sp = rgb(state.anchor_placement_id),
    underline = true,
    nocombine = true,
  })

  vim.api.nvim_set_option_value("modifiable", true, { buf = bufnr })
  vim.api.nvim_buf_set_lines(bufnr, 0, -1, false, { M.anchor_text })
  vim.api.nvim_buf_clear_namespace(bufnr, M.namespace, 0, -1)
  vim.api.nvim_buf_set_extmark(bufnr, M.namespace, 0, 0, {
    end_col = #M.anchor_text,
    hl_group = group,
    priority = 10000,
  })
  vim.api.nvim_set_option_value("modifiable", false, { buf = bufnr })
end

function M.clear(bufnr)
  if vim.api.nvim_buf_is_valid(bufnr) then
    vim.api.nvim_buf_clear_namespace(bufnr, M.namespace, 0, -1)
  end
end

return M
