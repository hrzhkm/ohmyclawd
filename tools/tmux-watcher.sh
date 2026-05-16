#!/usr/bin/env bash
# ohmyclawd-tmux-watcher
# Monitors tmux panes running Claude Code for idle state (waiting for input).
# Posts notifications to the ohmyclawd daemon.
#
# Usage: ./tmux-watcher.sh [daemon_url]
# Run in background: nohup ./tmux-watcher.sh &

set -euo pipefail

DAEMON_URL="${1:-http://localhost:8787}"
IDLE_THRESHOLD=30  # seconds of no output before considered "waiting"
CHECK_INTERVAL=5   # seconds between checks

declare -A last_activity

while true; do
  # Find tmux panes running claude
  panes=$(tmux list-panes -a -F '#{session_name}:#{window_index}.#{pane_index} #{pane_current_command}' 2>/dev/null | grep -i claude || true)

  waiting=0
  total=0

  while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    pane_id=$(echo "$line" | awk '{print $1}')
    total=$((total + 1))

    # Get pane content (last line)
    content=$(tmux capture-pane -t "$pane_id" -p 2>/dev/null | tail -5 | md5sum | awk '{print $1}')

    now=$(date +%s)
    prev_hash="${last_activity[$pane_id]:-}"
    prev_time="${last_activity[${pane_id}_t]:-$now}"

    if [[ "$content" != "$prev_hash" ]]; then
      # Activity detected
      last_activity[$pane_id]="$content"
      last_activity[${pane_id}_t]="$now"
    else
      # No change — check if idle long enough
      idle=$((now - prev_time))
      if [[ $idle -ge $IDLE_THRESHOLD ]]; then
        waiting=$((waiting + 1))
      fi
    fi
  done <<< "$panes"

  # Post status to daemon
  if [[ $total -gt 0 ]]; then
    curl -sf -X POST "${DAEMON_URL}/notify" \
      -H "Content-Type: application/json" \
      -d "{\"sessions\":$total,\"waiting\":$waiting}" \
      >/dev/null 2>&1 || true
  fi

  sleep "$CHECK_INTERVAL"
done
