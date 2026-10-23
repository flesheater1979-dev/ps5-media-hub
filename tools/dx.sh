#!/usr/bin/env bash
# tools/dx.sh - run a command in a LONG-LIVED dev container (no per-call start-up lag).
#
#   ./tools/dx.sh ./tools/evo-remote.sh status
#   ./tools/dx.sh bash -c './tools/evo-remote.sh key down; ./tools/evo-remote.sh ui --line'
#   ./tools/dx.sh --stop        # remove the container (do this before `docker compose build`)
#
# Same image, mounts and PS5_HOST as `docker compose run --rm ps5-dev`, but the container
# is created once (named evo-dev) and reused with `docker exec`, so a call costs ~0.2 s
# instead of several seconds. Run from Git Bash on the host. The container only sleeps; it
# never touches the console on its own. Builds and deploys can use it too, but ONE command at
# a time (two runners on one console fought before - see the hardware-cycle rules).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NAME=evo-dev
cd "${ROOT}"

if [[ "${1:-}" == "--stop" ]]; then
    docker rm -f "${NAME}" >/dev/null 2>&1 && echo "removed ${NAME}" || echo "${NAME} was not running"
    exit 0
fi
[[ $# -ge 1 ]] || { echo "usage: $0 <command...> | --stop" >&2; exit 1; }

if [[ "$(docker inspect -f '{{.State.Running}}' "${NAME}" 2>/dev/null || true)" != "true" ]]; then
    docker rm -f "${NAME}" >/dev/null 2>&1 || true
    # Pass the console address the way compose does (.env), unless it is already exported.
    if [[ -z "${PS5_HOST:-}" && -f .env ]]; then
        PS5_HOST="$(grep -E '^PS5_HOST=' .env | head -1 | cut -d= -f2- | tr -d '\r')"
    fi
    MSYS_NO_PATHCONV=1 docker compose run -d --name "${NAME}" -e "PS5_HOST=${PS5_HOST:-}" \
        --entrypoint sleep ps5-dev infinity >/dev/null
fi

# MSYS_NO_PATHCONV keeps container paths such as /workspace from being rewritten.
MSYS_NO_PATHCONV=1 exec docker exec -i -e "PS5_HOST=${PS5_HOST:-$(docker exec "${NAME}" printenv PS5_HOST)}" "${NAME}" "$@"
