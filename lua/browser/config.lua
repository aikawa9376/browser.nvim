local M = {}

local defaults = {
  browserd = nil,
  max_fps = 60,
  profile_dir = vim.fn.stdpath("data") .. "/browser/profile",

  keys = {
    scroll_down = "j",
    scroll_up = "k",
    half_down = "<C-d>",
    half_up = "<C-u>",
    page_down = "<C-f>",
    page_up = "<C-b>",
    back = "H",
    forward = "L",
    reload = "r",
    hints = "f",
    visual = "v",
    open = "o",
    yank_url = "yy",
  },

  scroll_step = 120,

  visual = {
    enabled = true,
    clipboard = false,
    max_hints = 300,
  },

  rendering = {
    dirty_rects = true,
    max_dirty_rects = 32,
    full_frame_threshold = 0.5,
  },

  -- Overrides are useful on terminals whose TIOCGWINSZ pixel fields are zero.
  -- The daemon reports detected values and otherwise uses its own fallback.
  cell_width = nil,
  cell_height = nil,
}

local options = vim.deepcopy(defaults)

local function positive_integer(name, value)
  if type(value) ~= "number" or value < 1 or value % 1 ~= 0 then
    error(("browser.nvim: %s must be a positive integer"):format(name))
  end
end

local function validate(opts)
  if opts.browserd ~= nil and (type(opts.browserd) ~= "string" or opts.browserd == "") then
    error("browser.nvim: browserd must be nil or a non-empty string")
  end

  positive_integer("max_fps", opts.max_fps)
  if opts.max_fps > 240 then
    error("browser.nvim: max_fps must be <= 240")
  end

  if type(opts.profile_dir) ~= "string" or opts.profile_dir == "" then
    error("browser.nvim: profile_dir must be a non-empty string")
  end

  if opts.cell_width ~= nil then
    positive_integer("cell_width", opts.cell_width)
  end
  if opts.cell_height ~= nil then
    positive_integer("cell_height", opts.cell_height)
  end

  positive_integer("scroll_step", opts.scroll_step)

  local visual = opts.visual
  if type(visual.enabled) ~= "boolean" then
    error("browser.nvim: visual.enabled must be a boolean")
  end
  if type(visual.clipboard) ~= "boolean" then
    error("browser.nvim: visual.clipboard must be a boolean")
  end
  positive_integer("visual.max_hints", visual.max_hints)
  if visual.max_hints > 1000 then
    error("browser.nvim: visual.max_hints must be <= 1000")
  end

  local rendering = opts.rendering
  if type(rendering.dirty_rects) ~= "boolean" then
    error("browser.nvim: rendering.dirty_rects must be a boolean")
  end
  positive_integer("rendering.max_dirty_rects", rendering.max_dirty_rects)
  if rendering.max_dirty_rects > 1024 then
    error("browser.nvim: rendering.max_dirty_rects must be <= 1024")
  end
  if type(rendering.full_frame_threshold) ~= "number"
    or rendering.full_frame_threshold <= 0
    or rendering.full_frame_threshold > 1
  then
    error("browser.nvim: rendering.full_frame_threshold must be in (0, 1]")
  end
end

function M.setup(opts)
  local candidate = vim.tbl_deep_extend("force", vim.deepcopy(defaults), opts or {})
  validate(candidate)
  options = candidate
  return options
end

function M.get()
  return options
end

function M.defaults()
  return vim.deepcopy(defaults)
end

return M
