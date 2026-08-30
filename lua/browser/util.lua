local M = {}

function M.root()
  local source = debug.getinfo(1, "S").source
  if source:sub(1, 1) == "@" then
    source = source:sub(2)
  end
  return vim.fs.dirname(vim.fs.dirname(vim.fs.dirname(source)))
end

function M.resolve_browserd(configured)
  if configured and configured ~= "" then
    return vim.fn.fnamemodify(configured, ":p")
  end

  local candidates = {
    vim.fs.joinpath(M.root(), "browserd", "build", "browserd"),
    vim.fs.joinpath(M.root(), "build", "browserd", "browserd"),
  }
  for _, candidate in ipairs(candidates) do
    if vim.fn.executable(candidate) == 1 then
      return candidate
    end
  end

  local from_path = vim.fn.exepath("browserd")
  if from_path ~= "" then
    return from_path
  end
  return candidates[1]
end

function M.notify(message, level)
  local emit = function()
    vim.notify(message, level or vim.log.levels.INFO, { title = "browser.nvim" })
  end
  if vim.in_fast_event() then
    vim.schedule(emit)
  else
    emit()
  end
end

function M.version_at_least(version, minimum)
  local function parts(value)
    local result = {}
    for number in value:gmatch("%d+") do
      result[#result + 1] = tonumber(number)
      if #result == 3 then
        break
      end
    end
    return result
  end

  local lhs = parts(version)
  local rhs = parts(minimum)
  for index = 1, 3 do
    local a = lhs[index] or 0
    local b = rhs[index] or 0
    if a ~= b then
      return a > b
    end
  end
  return true
end

return M
