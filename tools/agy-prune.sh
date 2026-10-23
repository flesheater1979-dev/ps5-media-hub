#!/bin/bash
# tools/agy-prune.sh [days]
#
# Deletes files in scratch/agy/ older than [days] (default 14) that match:
#   task-*
#   *-result.json
#   *-stderr.txt
#   *-report.md
#   *-run.log
#
# Keeps usage.log and any other file.
# Prints each deleted file and a count.
set -u
cd "$(dirname "$0")/.." || exit 1

days=${1:-14}
if ! [[ "$days" =~ ^[0-9]+$ ]]; then
    echo "usage: tools/agy-prune.sh [days]" >&2
    exit 2
fi

dir="${2:-${AGY_DIR:-scratch/agy}}"

if [[ ! -d "$dir" ]]; then
    echo "Directory '$dir' does not exist; deleted 0 files."
    exit 0
fi

count=0
while IFS= read -r file; do
    [[ -z "$file" ]] && continue
    echo "$file"
    count=$((count + 1))
done < <(find "$dir" -type f -mtime +"$days" \
    \( -name "task-*" -o -name "*-result.json" -o -name "*-stderr.txt" -o -name "*-report.md" -o -name "*-run.log" \) \
    -print -delete)

echo "Deleted $count files."
