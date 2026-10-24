#!/bin/bash
# tools/oc-run.sh <task-name> [timeout-seconds]
#
# Same job as agy-run.sh, but the worker is opencode on BharatCode (qwen-3.8-27b).
# Reads scratch/agy/task-<name>.txt, prepends tools/agy-task.md, writes
# scratch/agy/<name>-oc-result.jsonl (raw events), <name>-oc-stderr.txt, and one line
# per run to scratch/agy/usage.log. Run it in the background. Override the model with
# OC_MODEL=bharatcode/deepseek-v4.1-flash (Pro only).
set -u
cd "$(dirname "$0")/.." || exit 1
mkdir -p scratch/agy
name=${1:?usage: tools/oc-run.sh <task-name> [timeout-seconds]}
secs=${2:-1500}
task="scratch/agy/task-${name}.txt"
[[ -f "$task" ]] || { echo "missing $task" >&2; exit 2; }

prompt="$(cat tools/agy-task.md; printf '\n\n---- TASK ----\n'; cat "$task")"

timeout "$secs" opencode run "$prompt" --model "${OC_MODEL:-bharatcode/qwen-3.8-27b}" \
    --auto --format json --dir "$(pwd -W 2>/dev/null || pwd)" \
    > "scratch/agy/${name}-oc-result.jsonl" 2> "scratch/agy/${name}-oc-stderr.txt"
rc=$?
echo "$(date '+%F %T') ${name} oc rc=${rc} bytes=$(wc -c < "scratch/agy/${name}-oc-result.jsonl")" >> scratch/agy/usage.log
tail -1 scratch/agy/usage.log

if [[ $rc -ne 0 ]]; then
    taskkill //F //IM opencode.exe >/dev/null 2>&1 || pkill -x opencode >/dev/null 2>&1 || true
fi
exit $rc
