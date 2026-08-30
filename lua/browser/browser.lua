local buffer = require("browser.buffer")
local config = require("browser.config")
local ipc = require("browser.ipc")
local mappings = require("browser.mappings")
local util = require("browser.util")
local visual = require("browser.visual")

local M = {}

local states_by_buf = {}
local states_by_id = {}
local augroup
local id_cursor = 0
local attach_generation = 0

local seed = tostring(vim.fn.getpid()) .. ":" .. tostring(vim.uv.hrtime())
local session_base = (tonumber(vim.fn.sha256(seed):sub(1, 6), 16) % 0xff0000) + 1

local function allocate_ids()
  id_cursor = id_cursor + 4
  if session_base + id_cursor + 4 > 0xffffff then
    error("browser.nvim: image ID namespace exhausted")
  end
  return {
    anchor_image_id = session_base + id_cursor,
    anchor_placement_id = session_base + id_cursor + 1,
    browser_image_id = session_base + id_cursor + 2,
    browser_placement_id = session_base + id_cursor + 3,
  }
end

local function normalize_url(url)
  url = vim.trim(url or "")
  if url == "" then
    return "about:blank"
  end
  if url:match("^[%w.-]+:%d+") then
    return "http://" .. url
  end
  if not url:match("^%a[%w+.-]*:") then
    return "https://" .. url
  end
  return url
end

local function cell_size()
  local opts = config.get()
  local metrics = ipc.metrics() or {}
  return opts.cell_width or metrics.cell_width or 10,
    opts.cell_height or metrics.cell_height or 20
end

local function set_dimensions(state, winid)
  local cols = vim.api.nvim_win_get_width(winid)
  local rows = vim.api.nvim_win_get_height(winid)
  local cell_width, cell_height = cell_size()
  state.cols = cols
  state.rows = rows
  state.pixel_width = cols * cell_width
  state.pixel_height = rows * cell_height
end

local function create_payload(state)
  return {
    type = "create",
    browser_id = state.browser_id,
    url = state.url,
    cols = state.cols,
    rows = state.rows,
    width = state.pixel_width,
    height = state.pixel_height,
    fps = config.get().max_fps,
    dirty_rects = config.get().rendering.dirty_rects,
    max_dirty_rects = config.get().rendering.max_dirty_rects,
    full_frame_threshold = config.get().rendering.full_frame_threshold,
    profile_dir = config.get().profile_dir,
    anchor_image_id = state.anchor_image_id,
    anchor_placement_id = state.anchor_placement_id,
    browser_image_id = state.browser_image_id,
    browser_placement_id = state.browser_placement_id,
  }
end

local function send_resize(state, force)
  if not state.winid or not vim.api.nvim_win_is_valid(state.winid) then
    return
  end
  if vim.api.nvim_win_get_buf(state.winid) ~= state.bufnr then
    return
  end
  local old_cols, old_rows = state.cols, state.rows
  local old_width, old_height = state.pixel_width, state.pixel_height
  set_dimensions(state, state.winid)
  if force
    or old_cols ~= state.cols
    or old_rows ~= state.rows
    or old_width ~= state.pixel_width
    or old_height ~= state.pixel_height
  then
    ipc.send({
      type = "resize",
      browser_id = state.browser_id,
      cols = state.cols,
      rows = state.rows,
      width = state.pixel_width,
      height = state.pixel_height,
    })
  end
end

local function state_for_current()
  return states_by_buf[vim.api.nvim_get_current_buf()]
end

local function set_mode(state, mode)
  if not state or state.mode == mode then
    return
  end
  local previous = state.mode
  state.mode = mode
  if previous == "insert" and mode ~= "insert" then
    mappings.leave_insert(state)
  elseif previous ~= "insert" and mode == "insert" then
    mappings.enter_insert(state)
  end
end

local function cancel_ephemeral_mode(state)
  if state.mode == "hint" then
    ipc.send({ type = "hints_cancel", browser_id = state.browser_id })
  elseif state.mode == "visual" or state.mode == "visual_hint" then
    ipc.send({ type = "visual_cancel", browser_id = state.browser_id })
  elseif state.mode == "insert" then
    ipc.send({ type = "input_cancel", browser_id = state.browser_id })
  else
    return
  end
  set_mode(state, "normal")
  state.hint_count = nil
  state.visual_hint_count = nil
end

local function on_buf_win_enter(bufnr)
  local state = states_by_buf[bufnr]
  if not state then
    return
  end

  local winid = vim.api.nvim_get_current_win()
  local windows = buffer.windows(bufnr)
  if #windows > 1 then
    local primary = state.winid
    if not primary
      or not vim.api.nvim_win_is_valid(primary)
      or vim.api.nvim_win_get_buf(primary) ~= bufnr
    then
      for _, candidate in ipairs(windows) do
        if candidate ~= winid then
          primary = candidate
          break
        end
      end
    end
    if primary and primary ~= winid then
      vim.schedule(function()
        buffer.reject_duplicate(winid)
      end)
      return
    end
  end

  state.winid = winid
  state.visible = true
  buffer.configure_window(winid)
  send_resize(state, true)
  ipc.send({ type = "visibility", browser_id = state.browser_id, visible = true })
  ipc.send({ type = "focus", browser_id = state.browser_id, focused = winid == vim.api.nvim_get_current_win() })

  attach_generation = attach_generation + 1
  local generation = attach_generation
  state.attach_generation = generation
  vim.schedule(function()
    if states_by_buf[bufnr] ~= state
      or state.attach_generation ~= generation
      or not state.winid
      or not vim.api.nvim_win_is_valid(state.winid)
      or vim.api.nvim_win_get_buf(state.winid) ~= bufnr
    then
      return
    end
    pcall(vim.cmd, "redraw")
    ipc.send({
      type = "attach",
      browser_id = state.browser_id,
      cols = state.cols,
      rows = state.rows,
      full_frame = true,
    })
  end)
end

local function on_buf_win_leave(bufnr)
  local state = states_by_buf[bufnr]
  if not state then
    return
  end
  local leaving_win = vim.api.nvim_get_current_win()
  if state.winid ~= leaving_win then
    return
  end
  cancel_ephemeral_mode(state)
  state.attach_generation = -1
  ipc.send({ type = "focus", browser_id = state.browser_id, focused = false })
  ipc.send({ type = "detach", browser_id = state.browser_id })
  state.visible = false
  state.winid = nil
end

local function destroy(bufnr)
  local state = states_by_buf[bufnr]
  if not state or state.destroyed then
    return
  end
  cancel_ephemeral_mode(state)
  state.destroyed = true
  ipc.send({ type = "destroy", browser_id = state.browser_id })
  states_by_buf[bufnr] = nil
  states_by_id[state.browser_id] = nil
end

local function on_event(event)
  if event.type == "terminal_metrics" then
    for _, state in pairs(states_by_buf) do
      send_resize(state, true)
    end
    return
  end

  local state = states_by_id[event.browser_id]
  if not state then
    if event.type == "error" then
      util.notify("browserd: " .. tostring(event.message), vim.log.levels.ERROR)
    end
    return
  end

  if event.type == "url_changed" and type(event.url) == "string" then
    state.url = event.url
    set_mode(state, "normal")
    state.hint_count = nil
    state.visual_hint_count = nil
    buffer.rename(state.bufnr, state.url)
  elseif event.type == "title_changed" and type(event.title) == "string" then
    state.title = event.title
  elseif event.type == "loading" then
    state.loading = not not event.loading
    state.can_go_back = not not event.can_go_back
    state.can_go_forward = not not event.can_go_forward
  elseif event.type == "created" then
    state.ready = true
  elseif event.type == "mode_changed" and type(event.mode) == "string" then
    set_mode(state, event.mode)
    if event.mode == "normal" then
      state.hint_count = nil
      state.visual_hint_count = nil
    end
  elseif event.type == "hints_ready" then
    state.hint_count = tonumber(event.count) or 0
  elseif event.type == "hint_activated" then
    state.hint_count = nil
  elseif event.type == "hints_cancelled" then
    set_mode(state, "normal")
    state.hint_count = nil
  elseif event.type == "hints_empty" then
    set_mode(state, "normal")
    state.hint_count = nil
    util.notify("browser.nvim: no visible hint targets", vim.log.levels.INFO)
  elseif event.type == "visual_hints_ready" then
    state.visual_hint_count = tonumber(event.count) or 0
  elseif event.type == "visual_cancelled" then
    set_mode(state, "normal")
    state.visual_hint_count = nil
  elseif event.type == "visual_empty" then
    set_mode(state, "normal")
    state.visual_hint_count = nil
    util.notify("browser.nvim: no visible text targets", vim.log.levels.INFO)
  elseif event.type == "visual_yank" and type(event.text) == "string" then
    if state.mode == "visual" then
      visual.handle_yank(event.text)
      set_mode(state, "normal")
    end
  elseif event.type == "error" then
    if state.mode ~= "normal" then
      set_mode(state, "normal")
      state.hint_count = nil
      state.visual_hint_count = nil
    end
    util.notify("browserd: " .. tostring(event.message), vim.log.levels.ERROR)
  end
end

local function on_exit(completed, expected, stderr)
  if expected then
    return
  end
  local detail = (stderr and vim.trim(stderr) ~= "") and ("\n\n" .. vim.trim(stderr)) or ""
  for _, state in pairs(states_by_buf) do
    state.crashed = true
    set_mode(state, "normal")
    buffer.show_error(state.bufnr, {
      "browser.nvim daemon exited.",
      "",
      "Press r to restart browser.",
      detail,
    })
  end
  util.notify(
    ("browserd exited (code=%s, signal=%s)"):format(completed.code, completed.signal),
    vim.log.levels.ERROR
  )
end

function M.setup()
  ipc.setup({ on_event = on_event, on_exit = on_exit })
  augroup = vim.api.nvim_create_augroup("BrowserNvim", { clear = true })

  vim.api.nvim_create_autocmd("BufWinEnter", {
    group = augroup,
    callback = function(event)
      on_buf_win_enter(event.buf)
    end,
  })
  vim.api.nvim_create_autocmd("BufWinLeave", {
    group = augroup,
    callback = function(event)
      on_buf_win_leave(event.buf)
    end,
  })
  vim.api.nvim_create_autocmd("BufHidden", {
    group = augroup,
    callback = function(event)
      local state = states_by_buf[event.buf]
      if state then
        state.visible = false
        ipc.send({ type = "visibility", browser_id = state.browser_id, visible = false })
      end
    end,
  })
  vim.api.nvim_create_autocmd("InsertCharPre", {
    group = augroup,
    callback = function(event)
      local state = states_by_buf[event.buf]
      if not state or state.mode ~= "insert" then
        return
      end
      local text = vim.v.char
      vim.v.char = ""
      if text ~= "" then
        M.input_text(state, text)
      end
    end,
  })
  vim.api.nvim_create_autocmd("InsertLeave", {
    group = augroup,
    callback = function(event)
      local state = states_by_buf[event.buf]
      if state and state.mode == "insert" then
        ipc.send({ type = "input_cancel", browser_id = state.browser_id })
        set_mode(state, "normal")
      end
    end,
  })
  vim.api.nvim_create_autocmd({ "BufDelete", "BufWipeout" }, {
    group = augroup,
    callback = function(event)
      destroy(event.buf)
    end,
  })
  vim.api.nvim_create_autocmd({ "WinResized", "VimResized" }, {
    group = augroup,
    callback = function()
      for _, state in pairs(states_by_buf) do
        send_resize(state, false)
      end
    end,
  })
  vim.api.nvim_create_autocmd("WinScrolled", {
    group = augroup,
    callback = function(event)
      local winid = tonumber(event.match)
      if winid and vim.api.nvim_win_is_valid(winid) then
        local state = states_by_buf[vim.api.nvim_win_get_buf(winid)]
        if state then
          buffer.pin_view(winid)
        end
      end
    end,
  })
  vim.api.nvim_create_autocmd("WinEnter", {
    group = augroup,
    callback = function()
      local state = state_for_current()
      if state then
        local winid = vim.api.nvim_get_current_win()
        local windows = buffer.windows(state.bufnr)
        if #windows > 1 and state.winid and state.winid ~= winid then
          vim.schedule(function()
            buffer.reject_duplicate(winid)
          end)
          return
        end
        if not state.winid then
          on_buf_win_enter(state.bufnr)
          return
        end
        ipc.send({ type = "focus", browser_id = state.browser_id, focused = true })
      end
    end,
  })
  vim.api.nvim_create_autocmd("WinLeave", {
    group = augroup,
    callback = function(event)
      local state = states_by_buf[event.buf]
      if state then
        ipc.send({ type = "focus", browser_id = state.browser_id, focused = false })
      end
    end,
  })
  vim.api.nvim_create_autocmd("WinClosed", {
    group = augroup,
    callback = function(event)
      local winid = tonumber(event.match)
      for _, state in pairs(states_by_buf) do
        if state.winid == winid then
          cancel_ephemeral_mode(state)
          state.attach_generation = -1
          state.winid = nil
          state.visible = false
          ipc.send({ type = "focus", browser_id = state.browser_id, focused = false })
          ipc.send({ type = "detach", browser_id = state.browser_id })
          ipc.send({ type = "visibility", browser_id = state.browser_id, visible = false })
        end
      end
    end,
  })
  vim.api.nvim_create_autocmd("VimLeavePre", {
    group = augroup,
    callback = function()
      ipc.shutdown()
    end,
  })
end

function M.open(url)
  local ok, message = ipc.start()
  if not ok then
    util.notify(message, vim.log.levels.ERROR)
    return nil, message
  end

  local normalized = normalize_url(url)
  local ids = allocate_ids()
  local winid = vim.api.nvim_get_current_win()
  local state = vim.tbl_extend("force", ids, {
    browser_id = id_cursor / 4,
    url = normalized,
    title = normalized,
    visible = false,
    winid = nil,
    mode = "normal",
    loading = false,
    ready = false,
    can_go_back = false,
    can_go_forward = false,
    crashed = false,
  })
  set_dimensions(state, winid)
  state.bufnr = buffer.create(normalized, state)
  states_by_buf[state.bufnr] = state
  states_by_id[state.browser_id] = state
  mappings.attach(state.bufnr, state)

  local sent, send_error = ipc.send(create_payload(state))
  if not sent then
    states_by_buf[state.bufnr] = nil
    states_by_id[state.browser_id] = nil
    vim.api.nvim_buf_delete(state.bufnr, { force = true })
    util.notify(send_error, vim.log.levels.ERROR)
    return nil, send_error
  end

  vim.api.nvim_win_set_buf(winid, state.bufnr)
  return state
end

function M.current()
  return state_for_current()
end

function M.navigate(state, url)
  state = state or state_for_current()
  if not state or state.destroyed or states_by_buf[state.bufnr] ~= state then
    return
  end
  url = normalize_url(url)
  state.url = url
  ipc.send({ type = "navigate", browser_id = state.browser_id, url = url })
end

function M.prompt_navigate(state)
  state = state or state_for_current()
  if not state then
    return
  end
  vim.ui.input({ prompt = "URL: ", default = state.url }, function(value)
    if value and vim.trim(value) ~= "" then
      M.navigate(state, value)
    end
  end)
end

function M.command(state, command)
  state = state or state_for_current()
  if state and not state.destroyed and states_by_buf[state.bufnr] == state then
    ipc.send({ type = command, browser_id = state.browser_id })
  end
end

function M.reload(state)
  state = state or state_for_current()
  if not state then
    return
  end
  if state.crashed then
    M.restart_daemon()
  else
    M.command(state, "reload")
  end
end

function M.scroll(state, dy)
  ipc.send({ type = "scroll", browser_id = state.browser_id, dx = 0, dy = dy })
end

function M.scroll_fraction(state, fraction)
  M.scroll(state, math.floor((state.pixel_height or 600) * fraction))
end

function M.scroll_to(state, edge)
  ipc.send({ type = "scroll_to", browser_id = state.browser_id, edge = edge })
end

function M.yank_url(state)
  vim.fn.setreg('"', state.url)
  vim.fn.setreg("0", state.url)
end

function M.stop(state)
  M.command(state, "stop")
end

function M.start_input(state)
  state = state or state_for_current()
  if not state or state.mode ~= "normal" or not state.ready or state.loading then
    return false
  end
  return ipc.send({ type = "input_start", browser_id = state.browser_id })
end

function M.input_text(state, text)
  if not state or state.mode ~= "insert" or type(text) ~= "string" or text == "" then
    return false
  end
  return ipc.send({
    type = "input_text",
    browser_id = state.browser_id,
    text = text,
  })
end

function M.input_key(state, event)
  if not state or state.mode ~= "insert" or type(event) ~= "table" then
    return false
  end
  return ipc.send({
    type = "input_key",
    browser_id = state.browser_id,
    key = event.key,
    shift = not not event.shift,
    control = not not event.control,
    alt = not not event.alt,
  })
end

function M.cancel_input(state)
  state = state or state_for_current()
  if not state or state.mode ~= "insert" then
    return false
  end
  local sent = ipc.send({ type = "input_cancel", browser_id = state.browser_id })
  if sent then
    set_mode(state, "normal")
  end
  return sent
end

function M.close(state)
  state = state or state_for_current()
  if state and vim.api.nvim_buf_is_valid(state.bufnr) then
    vim.api.nvim_buf_delete(state.bufnr, { force = false })
  end
end

function M.restart_daemon()
  local ok, message = ipc.start()
  if not ok then
    util.notify(message, vim.log.levels.ERROR)
    return false
  end
  for _, state in pairs(states_by_buf) do
    state.crashed = false
    set_mode(state, "normal")
    state.winid = nil
    buffer.restore_anchor(state.bufnr, state)
    ipc.send(create_payload(state))
    local windows = buffer.windows(state.bufnr)
    if #windows == 1 then
      vim.api.nvim_win_call(windows[1], function()
        on_buf_win_enter(state.bufnr)
      end)
    end
  end
  return true
end

function M._states()
  return states_by_buf
end

return M
