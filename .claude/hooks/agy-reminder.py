#!/usr/bin/env python3
"""PreToolUse hook for Edit/Write.

Reminds Claude to consider the agy worker before hand-writing source code.
The first Edit/Write of a source file in a session is blocked once (exit 2);
the block is skipped when a scratch/agy/task-*.txt was written in the last 6
hours (agy is already in use) or when the block already fired this session.
Retrying the same edit then passes, so it costs one extra call.
"""
import json
import os
import sys
import time

SRC_EXT = (".c", ".cpp", ".h", ".hpp", ".rml", ".rcss")
WINDOW_S = 6 * 3600

MSG = (
    "agy check: you are about to hand-write source code. CLAUDE.md says to "
    "decide first whether this is mechanical (settings row, screen from a "
    "worked example, boilerplate, host tests, large reads). If yes, write "
    "scratch/agy/task-<name>.txt and run tools/agy-run.sh instead. If no "
    "(hardware decision, tiny fix, review follow-up), say 'agy: no, because "
    "...' in your reply and repeat the edit; this reminder fires once per session."
)


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except Exception:
        return 0
    path = (data.get("tool_input") or {}).get("file_path", "")
    if not path.lower().endswith(SRC_EXT):
        return 0
    norm = path.replace("\\", "/").lower()
    if "/projects/evoplayer/" not in norm and "/tests/" not in norm:
        return 0

    root = os.environ.get("CLAUDE_PROJECT_DIR", os.getcwd())
    agy_dir = os.path.join(root, "scratch", "agy")
    now = time.time()
    try:
        for name in os.listdir(agy_dir):
            if name.startswith("task-") and now - os.path.getmtime(os.path.join(agy_dir, name)) < WINDOW_S:
                return 0
    except OSError:
        pass

    sid = str(data.get("session_id", "nosession"))
    marker = os.path.join(os.environ.get("TEMP", "/tmp"), "evo-agy-reminder-" + sid)
    if os.path.exists(marker):
        return 0
    try:
        open(marker, "w").close()
    except OSError:
        return 0
    sys.stderr.write(MSG)
    return 2


if __name__ == "__main__":
    sys.exit(main())
