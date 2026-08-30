#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 7 ]]; then
  printf 'usage: %s /path/to/browserd basic.html scroll.html links.html forms.html selection.html graphemes.html\n' "$0" >&2
  exit 2
fi

browserd=$1
page=$2
scroll_page=$3
links_page=$4
forms_page=$5
selection_page=$6
graphemes_page=$7
cursor_page=$(dirname "$selection_page")/cursor.html
temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/browser-cef-smoke.XXXXXX")
events_file="$temporary_dir/events.jsonl"
all_events_file="$temporary_dir/all-events.jsonl"
stderr_file="$temporary_dir/stderr.log"

cleanup() {
  if [[ -n ${BROWSERD_PID:-} ]]; then
    kill "$BROWSERD_PID" 2>/dev/null || true
    wait "$BROWSERD_PID" 2>/dev/null || true
  fi
  rm -rf -- "$temporary_dir"
}
trap cleanup EXIT HUP INT TERM

coproc BROWSERD {
  trap - EXIT HUP INT TERM
  exec env BROWSER_KITTY_DRY_RUN=1 \
    BROWSER_CELL_WIDTH=10 \
    BROWSER_CELL_HEIGHT=20 \
    BROWSER_PROFILE_DIR="$temporary_dir/profile" \
    "$browserd" 2>"$stderr_file"
}

daemon_input=${BROWSERD[1]}
daemon_output=${BROWSERD[0]}

line_matches() {
  local line=$1
  shift
  local needle
  for needle in "$@"; do
    [[ $line == *"$needle"* ]] || return 1
  done
}

event_seen() {
  local existing
  while IFS= read -r existing; do
    if line_matches "$existing" "$@"; then
      return 0
    fi
  done <"$events_file"
  return 1
}

wait_for_event() {
  local description=$1
  shift
  if event_seen "$@"; then
    return 0
  fi
  local deadline=$((SECONDS + 30))
  local line
  while (( SECONDS < deadline )); do
    if IFS= read -r -t 1 line <&"$daemon_output"; then
      printf '%s\n' "$line" >>"$events_file"
      printf '%s\n' "$line" >>"$all_events_file"
      if line_matches "$line" "$@"; then
        return 0
      fi
    elif ! kill -0 "$BROWSERD_PID" 2>/dev/null; then
      break
    fi
  done
  printf 'timed out waiting for %s\n' "$description" >&2
  sed -n '1,200p' "$events_file" >&2
  sed -n '1,120p' "$stderr_file" >&2
  return 1
}

wait_for_loading_cycle() {
  local saw_loading=false
  local deadline=$((SECONDS + 30))
  local line
  while (( SECONDS < deadline )); do
    if IFS= read -r -t 1 line <&"$daemon_output"; then
      printf '%s\n' "$line" >>"$events_file"
      printf '%s\n' "$line" >>"$all_events_file"
      if line_matches "$line" '"type":"loading"' '"loading":true'; then
        saw_loading=true
      elif [[ $saw_loading == true ]] &&
        line_matches "$line" '"type":"loading"' '"loading":false'; then
        return 0
      fi
    elif ! kill -0 "$BROWSERD_PID" 2>/dev/null; then
      break
    fi
  done
  printf 'timed out waiting for a complete reload cycle\n' >&2
  sed -n '1,200p' "$events_file" >&2
  return 1
}

reset_events() {
  : >"$events_file"
}

printf '%s\n' \
  "{\"type\":\"create\",\"browser_id\":1,\"url\":\"file://$scroll_page\",\"cols\":40,\"rows\":15,\"width\":400,\"height\":300,\"fps\":60,\"anchor_image_id\":101,\"anchor_placement_id\":102,\"browser_image_id\":103,\"browser_placement_id\":104}" \
  "{\"type\":\"attach\",\"browser_id\":1,\"cols\":40,\"rows\":15,\"full_frame\":true}" \
  '{"type":"scroll","browser_id":1,"dx":0,"dy":600}' \
  >&"$daemon_input"

wait_for_event 'initial 400x300 frame' '"type":"frame_ready"' '"width":400' '"height":300'
wait_for_event 'queued 600px scroll' '"type":"title_changed"' '"title":"browser.nvim scroll:middle:600"'
reset_events

printf '%s\n' \
  '{"type":"detach","browser_id":1}' \
  '{"type":"visibility","browser_id":1,"visible":false}' \
  '{"type":"visibility","browser_id":1,"visible":true}' \
  '{"type":"attach","browser_id":1,"cols":40,"rows":15,"full_frame":true}' \
  '{"type":"scroll","browser_id":1,"dx":0,"dy":1}' \
  >&"$daemon_input"

wait_for_event 'scroll state after hide/show' '"type":"title_changed"' '"title":"browser.nvim scroll:middle:601"'
reset_events

printf '%s\n' \
  '{"type":"resize","browser_id":1,"cols":32,"rows":12,"width":320,"height":240}' \
  >&"$daemon_input"

wait_for_event 'resized 320x240 frame' '"type":"frame_ready"' '"width":320' '"height":240'
reset_events

printf '%s\n' '{"type":"scroll_to","browser_id":1,"edge":"bottom"}' >&"$daemon_input"
wait_for_event 'bottom scroll' '"type":"title_changed"' '"title":"browser.nvim scroll:bottom:'
reset_events

printf '%s\n' '{"type":"scroll_to","browser_id":1,"edge":"top"}' >&"$daemon_input"
wait_for_event 'top scroll' '"type":"title_changed"' '"title":"browser.nvim scroll:top:0"'
reset_events

printf '%s\n' \
  "{\"type\":\"navigate\",\"browser_id\":1,\"url\":\"file://$page\"}" \
  >&"$daemon_input"
wait_for_event 'navigate title' '"type":"title_changed"' '"title":"browser.nvim basic"'
wait_for_event 'navigate completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$page\""
reset_events

printf '%s\n' '{"type":"back","browser_id":1}' >&"$daemon_input"
wait_for_event 'back navigation' '"type":"title_changed"' '"title":"browser.nvim scroll:top:0"'
wait_for_event 'back completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$scroll_page\""
reset_events

printf '%s\n' '{"type":"forward","browser_id":1}' >&"$daemon_input"
wait_for_event 'forward navigation' '"type":"title_changed"' '"title":"browser.nvim basic"'
wait_for_event 'forward completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$page\""
reset_events

printf '%s\n' '{"type":"reload","browser_id":1}' >&"$daemon_input"
wait_for_loading_cycle
reset_events

printf '%s\n' \
  '{"type":"back","browser_id":1}' \
  '{"type":"forward","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'queued rapid back/forward' '"type":"title_changed"' '"title":"browser.nvim basic"'
wait_for_event 'queued forward completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$page\""
reset_events

printf '%s\n' \
  "{\"type\":\"navigate\",\"browser_id\":1,\"url\":\"file://$links_page\"}" \
  >&"$daemon_input"
wait_for_event 'links page title' '"type":"title_changed"' '"title":"browser.nvim links"'
wait_for_event 'links page completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$links_page\""
reset_events

printf '%s\n' '{"type":"cursor_activate","browser_id":1}' >&"$daemon_input"
wait_for_event 'normal cursor link activation' '"type":"title_changed"' '"title":"browser.nvim basic"'
wait_for_event 'normal cursor link completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$page\""
reset_events

printf '%s\n' \
  "{\"type\":\"navigate\",\"browser_id\":1,\"url\":\"file://$links_page\"}" \
  >&"$daemon_input"
wait_for_event 'return to links after cursor activation' '"type":"title_changed"' '"title":"browser.nvim links"'
wait_for_event 'cursor links return completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$links_page\""
reset_events

printf '%s\n' '{"type":"hints_start","browser_id":1}' >&"$daemon_input"
wait_for_event 'hint mode start' '"type":"mode_changed"' '"mode":"hint"'
wait_for_event 'four visible hint targets' '"type":"hints_ready"' '"count":4'
printf '%s\n' '{"type":"hints_input","browser_id":1,"key":"a"}' >&"$daemon_input"
wait_for_event 'anchor hint activation' '"type":"hint_activated"' '"action":"click"' '"tag":"a"'
wait_for_event 'hint link navigation' '"type":"title_changed"' '"title":"browser.nvim basic"'
wait_for_event 'hint link completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$page\""
reset_events

printf '%s\n' \
  "{\"type\":\"navigate\",\"browser_id\":1,\"url\":\"file://$links_page\"}" \
  >&"$daemon_input"
wait_for_event 'return to links page' '"type":"title_changed"' '"title":"browser.nvim links"'
wait_for_event 'links return completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$links_page\""
reset_events

printf '%s\n' \
  '{"type":"hints_start","browser_id":1}' \
  '{"type":"hints_input","browser_id":1,"key":"s"}' \
  >&"$daemon_input"
wait_for_event 'button hint activation' '"type":"hint_activated"' '"action":"click"' '"tag":"button"'
wait_for_event 'normal mode after button hint' '"type":"mode_changed"' '"mode":"normal"'
reset_events

printf '%s\n' \
  '{"type":"hints_start","browser_id":1}' \
  '{"type":"hints_cancel","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'explicit hint cancellation' '"type":"mode_changed"' '"mode":"normal"'
reset_events

printf '%s\n' \
  '{"type":"hints_start","browser_id":1}' \
  "{\"type\":\"navigate\",\"browser_id\":1,\"url\":\"file://$forms_page\"}" \
  >&"$daemon_input"
wait_for_event 'hint mode reset by navigation' '"type":"mode_changed"' '"mode":"normal"'
wait_for_event 'forms page title' '"type":"title_changed"' '"title":"browser.nvim forms"'
wait_for_event 'forms page completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$forms_page\""
reset_events

printf '%s\n' \
  '{"type":"cursor_move","browser_id":1,"operation":"next_word"}' \
  '{"type":"input_cursor_start","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'input focus from normal cursor' '"type":"cursor_input_focused"' '"tag":"input"'
wait_for_event 'insert mode from normal cursor' '"type":"mode_changed"' '"mode":"insert"'
printf '%s\n' '{"type":"input_cancel","browser_id":1}' >&"$daemon_input"
wait_for_event 'normal mode after cursor input' '"type":"mode_changed"' '"mode":"normal"'
reset_events

printf '%s\n' \
  '{"type":"hints_start","browser_id":1}' \
  '{"type":"hints_input","browser_id":1,"key":"a"}' \
  >&"$daemon_input"
wait_for_event 'input hint focus' '"type":"hint_activated"' '"action":"focus"' '"tag":"input"'
wait_for_event 'insert mode after input focus' '"type":"mode_changed"' '"mode":"insert"'
reset_events

printf '%s\n' \
  '{"type":"input_key","browser_id":1,"key":"a","control":true}' \
  '{"type":"input_text","browser_id":1,"text":"browser日本語👨‍👩‍👧‍👦é"}' \
  >&"$daemon_input"
wait_for_event 'committed UTF-8 text input' '"type":"title_changed"' '"title":"browser.nvim input:browser日本語👨‍👩‍👧‍👦é"'
reset_events

printf '%s\n' \
  '{"type":"input_key","browser_id":1,"key":"Home"}' \
  '{"type":"input_key","browser_id":1,"key":"Delete"}' \
  >&"$daemon_input"
wait_for_event 'home and delete input editing' '"type":"title_changed"' '"title":"browser.nvim input:rowser日本語👨‍👩‍👧‍👦é"'
reset_events

printf '%s\n' \
  '{"type":"input_key","browser_id":1,"key":"End"}' \
  '{"type":"input_key","browser_id":1,"key":"Backspace"}' \
  >&"$daemon_input"
wait_for_event 'combining-mark backspace editing' '"type":"title_changed"' '"title":"browser.nvim input:rowser日本語👨‍👩‍👧‍👦e"'
reset_events

printf '%s\n' \
  '{"type":"input_key","browser_id":1,"key":"Backspace"}' \
  >&"$daemon_input"
wait_for_event 'base-character backspace editing' '"type":"title_changed"' '"title":"browser.nvim input:rowser日本語👨‍👩‍👧‍👦"'
reset_events

printf '%s\n' \
  '{"type":"input_key","browser_id":1,"key":"Left"}' \
  '{"type":"input_key","browser_id":1,"key":"Right"}' \
  '{"type":"input_key","browser_id":1,"key":"Tab"}' \
  '{"type":"input_key","browser_id":1,"key":"Tab","shift":true}' \
  '{"type":"input_cancel","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'leave insert mode' '"type":"mode_changed"' '"mode":"normal"'
reset_events

printf '%s\n' \
  '{"type":"input_start","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'explicit insert mode start' '"type":"mode_changed"' '"mode":"insert"'
printf '%s\n' '{"type":"input_cancel","browser_id":1}' >&"$daemon_input"
wait_for_event 'explicit insert mode cancellation' '"type":"mode_changed"' '"mode":"normal"'
reset_events

printf '%s\n' \
  "{\"type\":\"navigate\",\"browser_id\":1,\"url\":\"file://$cursor_page\"}" \
  >&"$daemon_input"
wait_for_event 'cursor page title' '"type":"title_changed"' '"title":"browser.nvim cursor"'
wait_for_event 'cursor page completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$cursor_page\""
reset_events

printf '%s\n' \
  '{"type":"cursor_move","browser_id":1,"operation":"down"}' \
  '{"type":"visual_cursor_start","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'visual mode after crossing blank space' '"type":"mode_changed"' '"mode":"visual"'
printf '%s\n' '{"type":"visual_yank","browser_id":1}' >&"$daemon_input"
wait_for_event 'stable cursor movement across blank space' '"type":"visual_yank"' '"text":"B"'
reset_events

printf '%s\n' \
  "{\"type\":\"navigate\",\"browser_id\":1,\"url\":\"file://$selection_page\"}" \
  >&"$daemon_input"
wait_for_event 'selection page title' '"type":"title_changed"' '"title":"browser.nvim selection"'
wait_for_event 'selection page completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$selection_page\""
reset_events

printf '%s\n' \
  '{"type":"cursor_move","browser_id":1,"operation":"next_word"}' \
  '{"type":"visual_cursor_start","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'visual mode from normal cursor' '"type":"mode_changed"' '"mode":"visual"'
printf '%s\n' '{"type":"visual_yank","browser_id":1}' >&"$daemon_input"
wait_for_event 'normal cursor word yank' '"type":"visual_yank"' '"text":"w"'
reset_events

printf '%s\n' '{"type":"visual_start","browser_id":1,"max_hints":300}' >&"$daemon_input"
wait_for_event 'visual text hints' '"type":"visual_hints_ready"'
printf '%s\n' \
  '{"type":"visual_hint_input","browser_id":1,"key":"a"}' \
  '{"type":"visual_hint_input","browser_id":1,"key":"a"}' \
  >&"$daemon_input"
wait_for_event 'visual mode start' '"type":"mode_changed"' '"mode":"visual"'
printf '%s\n' '{"type":"visual_yank","browser_id":1}' >&"$daemon_input"
wait_for_event 'initial grapheme yank' '"type":"visual_yank"' '"text":"H"'
reset_events

printf '%s\n' '{"type":"visual_start","browser_id":1,"max_hints":300}' >&"$daemon_input"
wait_for_event 'second visual text hints' '"type":"visual_hints_ready"'
printf '%s\n' \
  '{"type":"visual_hint_input","browser_id":1,"key":"a"}' \
  '{"type":"visual_hint_input","browser_id":1,"key":"a"}' \
  >&"$daemon_input"
wait_for_event 'second visual mode start' '"type":"mode_changed"' '"mode":"visual"'
printf '%s\n' \
  '{"type":"visual_move","browser_id":1,"operation":"next_grapheme"}' \
  '{"type":"visual_move","browser_id":1,"operation":"swap"}' \
  '{"type":"visual_yank","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'backward selection yank' '"type":"visual_yank"' '"text":"He"'
reset_events

printf '%s\n' '{"type":"visual_start","browser_id":1,"max_hints":300}' >&"$daemon_input"
wait_for_event 'third visual text hints' '"type":"visual_hints_ready"'
printf '%s\n' \
  '{"type":"visual_hint_input","browser_id":1,"key":"a"}' \
  '{"type":"visual_hint_input","browser_id":1,"key":"a"}' \
  >&"$daemon_input"
wait_for_event 'third visual mode start' '"type":"mode_changed"' '"mode":"visual"'
printf '%s\n' \
  '{"type":"visual_move","browser_id":1,"operation":"next_word"}' \
  '{"type":"visual_move","browser_id":1,"operation":"next_word"}' \
  '{"type":"visual_yank","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'cross-span paragraph yank' '"type":"visual_yank"' '"text":"Hello world.'
reset_events

printf '%s\n' '{"type":"visual_start","browser_id":1,"max_hints":300}' >&"$daemon_input"
wait_for_event 'visual line text hints' '"type":"visual_hints_ready"'
printf '%s\n' \
  '{"type":"visual_hint_input","browser_id":1,"key":"a"}' \
  '{"type":"visual_hint_input","browser_id":1,"key":"a"}' \
  >&"$daemon_input"
wait_for_event 'visual line mode start' '"type":"mode_changed"' '"mode":"visual"'
printf '%s\n' \
  '{"type":"visual_move","browser_id":1,"operation":"line_end"}' \
  '{"type":"visual_yank","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'visual line-end yank' '"type":"visual_yank"' '"text":"Hello world."'
reset_events

printf '%s\n' \
  '{"type":"visual_start","browser_id":1,"max_hints":300}' \
  '{"type":"visual_cancel","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'visual hint cancellation' '"type":"mode_changed"' '"mode":"normal"'
reset_events

printf '%s\n' \
  "{\"type\":\"navigate\",\"browser_id\":1,\"url\":\"file://$graphemes_page\"}" \
  >&"$daemon_input"
wait_for_event 'grapheme page title' '"type":"title_changed"' '"title":"browser.nvim graphemes"'
wait_for_event 'grapheme page completion' '"type":"loading"' '"loading":false' "\"url\":\"file://$graphemes_page\""
reset_events

printf '%s\n' '{"type":"visual_start","browser_id":1,"max_hints":10}' >&"$daemon_input"
wait_for_event 'grapheme visual hints' '"type":"visual_hints_ready"' '"count":3'
printf '%s\n' \
  '{"type":"visual_hint_input","browser_id":1,"key":"a"}' \
  >&"$daemon_input"
wait_for_event 'emoji visual mode start' '"type":"mode_changed"' '"mode":"visual"'
printf '%s\n' '{"type":"visual_yank","browser_id":1}' >&"$daemon_input"
wait_for_event 'family emoji yank' '"type":"visual_yank"' '"text":"👨‍👩‍👧‍👦"'
reset_events

printf '%s\n' '{"type":"visual_start","browser_id":1,"max_hints":10}' >&"$daemon_input"
wait_for_event 'combining visual hints' '"type":"visual_hints_ready"' '"count":3'
printf '%s\n' \
  '{"type":"visual_hint_input","browser_id":1,"key":"s"}' \
  >&"$daemon_input"
wait_for_event 'combining visual mode start' '"type":"mode_changed"' '"mode":"visual"'
printf '%s\n' '{"type":"visual_yank","browser_id":1}' >&"$daemon_input"
wait_for_event 'combining grapheme yank' '"type":"visual_yank"' '"text":"é"'
reset_events

printf '%s\n' '{"type":"visual_start","browser_id":1,"max_hints":10}' >&"$daemon_input"
wait_for_event 'Japanese visual hints' '"type":"visual_hints_ready"' '"count":3'
printf '%s\n' \
  '{"type":"visual_hint_input","browser_id":1,"key":"d"}' \
  >&"$daemon_input"
wait_for_event 'Japanese visual mode start' '"type":"mode_changed"' '"mode":"visual"'
printf '%s\n' \
  '{"type":"visual_move","browser_id":1,"operation":"next_grapheme"}' \
  '{"type":"visual_move","browser_id":1,"operation":"next_grapheme"}' \
  '{"type":"visual_yank","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'Japanese text yank' '"type":"visual_yank"' '"text":"日本語"'
reset_events

printf '%s\n' \
  '{"type":"stop","browser_id":1}' \
  '{"type":"destroy","browser_id":1}' \
  >&"$daemon_input"
wait_for_event 'browser destruction' '"type":"destroyed"' '"browser_id":1'

printf '%s\n' '{"type":"shutdown"}' >&"$daemon_input"
wait "$BROWSERD_PID"
BROWSERD_PID=

if rg -q '"type":"error"' "$all_events_file"; then
  printf 'browserd emitted an IPC error\n' >&2
  sed -n '1,240p' "$all_events_file" >&2
  exit 1
fi

printf 'CEF smoke test passed: lifecycle, navigation, hints, input, visual selection, and rendering verified\n'
