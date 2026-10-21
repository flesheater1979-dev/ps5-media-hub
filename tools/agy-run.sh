#!/bin/bash
# tools/agy-run.sh <task-name> [timeout-seconds]
#
# Runs scratch/agy/task-<name>.txt through the agy CLI (Antigravity, headless) as a
# worker agent. Claude writes the task and reviews the result; agy never touches the
# console, git, or the hardware scripts (the task prompt says so; see tools/agy-task.md).
#
# Output: scratch/agy/<name>-result.json, <name>-stderr.txt, one line per run appended
# to scratch/agy/usage.log. Run it in the background. scratch/ is git-ignored.
set -u
cd "$(dirname "$0")/.." || exit 1
mkdir -p scratch/agy
name=${1:?usage: tools/agy-run.sh <task-name> [timeout-seconds]}
secs=${2:-1500}
task="scratch/agy/task-${name}.txt"
[[ -f "$task" ]] || { echo "missing $task" >&2; exit 2; }

# The shared rules go in front of every task so a short task file stays short.
prompt="$(cat tools/agy-task.md; printf '\n\n---- TASK ----\n'; cat "$task")"

agy -p "$prompt" --dangerously-skip-permissions --output-format json \
    --add-dir "$(pwd -W 2>/dev/null || pwd)" --print-timeout "${secs}s" \
    > "scratch/agy/${name}-result.json" 2> "scratch/agy/${name}-stderr.txt"
rc=$?

python3 -X utf8 -c "
import json, datetime
try:
    d = json.load(open('scratch/agy/${name}-result.json', encoding='utf-8'))
    u = d.get('usage', {})
    print(datetime.datetime.now().strftime('%F %T'), '${name}', d.get('status'),
          'tokens', u.get('total_tokens'), 'sec', round(d.get('duration_seconds', 0)), d.get('error', ''))
except Exception as e:
    print(datetime.datetime.now().strftime('%F %T'), '${name}', 'rc=${rc} no-json', e)
" >> scratch/agy/usage.log
tail -1 scratch/agy/usage.log

# agy can leave a child process behind when it times out.
if [[ $rc -ne 0 ]]; then
    taskkill //F //IM agy.exe >/dev/null 2>&1 || pkill -x agy >/dev/null 2>&1 || true
fi
exit $rc
