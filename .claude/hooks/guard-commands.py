#!/usr/bin/env python3
"""Hooks that enforce CLAUDE.md rules. Exit 2 = block (stderr goes to Claude).

  guard-commands.py pre    PreToolUse  Bash       block make / deploy over a running EVO / unverified commit
  guard-commands.py post   PostToolUse Bash       record launch/close, record a passing build or test
  guard-commands.py edit   PostToolUse Edit|Write source edit -> clears the "verified" mark

State lives in one JSON file in TEMP, so it is shared across sessions on purpose
(the console state outlives a session).
"""
import json
import os
import re
import sys
import time

SRC_EXT = (".c", ".cpp", ".h", ".hpp", ".rml", ".rcss")
STATE = os.path.join(os.environ.get("TEMP", "/tmp"), "evo-hooks-state.json")
CHECKS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "tools", "checks"))
SEGMENT_SPLIT = re.compile(r"&&|\|\||;|\||\n")
MAKE = re.compile(r"^(?:docker\s+compose\s+run\s+(?:--?\S+\s+)*ps5-dev\s+)?make(?:\s|$)")
REMOTE = re.compile(r"evo-remote\.sh\s+(launch|quit|close|cycle)\b")
VERIFY = re.compile(r"package-app\.sh|build-evoplayer\.sh|run_tests\.sh")


def load():
    try:
        with open(STATE, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def save(st):
    try:
        with open(STATE, "w", encoding="utf-8") as f:
            json.dump(st, f)
    except OSError:
        pass


def block(msg):
    sys.stderr.write(msg)
    return 2


def pre(cmd):
    st = load()
    for seg in SEGMENT_SPLIT.split(cmd):
        seg = seg.strip()
        if MAKE.match(seg):
            return block("Blocked: never call `make` directly (the Makefile lacks FFmpeg's transitive "
                         "deps). Use scripts/package-app.sh, or scripts/build-evoplayer.sh for a compile check.")
    if "deploy-app.sh" in cmd and "--force" not in cmd:
        if st.get("evo") == "launch":
            return block("Blocked: EVO was launched and not closed since (last evo-remote action: launch). "
                         "Never deploy over a running EVO: `evo-remote.sh quit`, wait for parked=1, then "
                         "`evo-remote.sh close`. If EVO crashed (already closed), repeat with --force.")
    if re.search(r"\bgit\s+commit\b", cmd) and "# verified" not in cmd:
        if st.get("dirty"):
            checks = []
            if os.path.isdir(CHECKS_DIR):
                try:
                    checks = [f for f in sorted(os.listdir(CHECKS_DIR)) if f.endswith(".sh")]
                except OSError:
                    pass
            extra = ""
            if checks:
                checks_list = ", ".join(f"tools/checks/{f}" for f in checks)
                extra = f" Matching hardware check scripts (suggested, not run): {checks_list}."
            return block("Blocked: source files changed since the last passing build/test. CLAUDE.md: no "
                         "commits until verified. Run the build/tests (and the hardware check if it "
                         f"applies), then commit; or add `# verified` to the command if you checked another way.{extra}")
    return 0


def post(cmd, resp):
    st = load()
    m = None
    for m in REMOTE.finditer(cmd):
        pass
    if m:
        st["evo"] = "launch" if m.group(1) in ("launch", "cycle") else m.group(1)
        if m.group(1) == "cycle":
            st["evo"] = "close"  # cycle closes EVO itself when it finishes
    if VERIFY.search(cmd):
        text = json.dumps(resp)[:200000].lower() if resp is not None else ""
        failed = bool(re.search(r"error:|\bfail(ed)?\b|\\u2717|exit code [1-9]|interrupted\": true", text))
        if not failed:
            st["dirty"] = False
    st["t"] = time.time()
    save(st)
    return 0


def edit(path):
    if path.lower().endswith(SRC_EXT):
        st = load()
        st["dirty"] = True
        save(st)
    return 0


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    try:
        data = json.load(sys.stdin)
    except Exception:
        return 0
    ti = data.get("tool_input") or {}
    if mode == "pre":
        return pre(ti.get("command", ""))
    if mode == "post":
        return post(ti.get("command", ""), data.get("tool_response"))
    if mode == "edit":
        return edit(ti.get("file_path", ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
