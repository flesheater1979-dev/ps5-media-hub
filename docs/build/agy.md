# Delegating to agy (saves Claude tokens)

Bulk reading and code drafts go to the `agy` CLI as a worker; Claude writes the
task, reviews the report and `git diff`, spot-checks claims, runs the checks, does
every console action, and commits. agy never touches the console or git.

- Write the task to `scratch/agy/task-<name>.txt` (absolute paths, the files it may
  edit, the exact check to run, the report path under `scratch/agy/`). Run
  `tools/agy-run.sh <name> [secs]` in the background. `tools/agy-task.md` is
  prepended to every task (hard rules, report format). Result JSON and one line of
  tokens per run land in `scratch/agy/` (`usage.log`).
- The launcher returns at once: a "completed" notification for the launch command
  means agy started, not finished. Wait for the result JSON / `usage.log` line.
- Small mechanical jobs first: screens ported from a worked example, boilerplate,
  reading large trees or logs. Not for hardware decisions or anything on the
  console.
- **Review before trusting.** Read the report, then `git diff`, then re-run the
  check yourself. agy has invented file contents through its own subagents and
  over-claimed passes; the task rules forbid subagents, but verify anyway.
- The task needs `--dangerously-skip-permissions` to use tools headless; that is
  why the rules in `agy-task.md` (no console, no git) are part of every prompt.
