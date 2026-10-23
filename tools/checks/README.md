# Hardware Feature Checks

Per-feature hardware verification scripts executed against an already running EVO build compiled with `--usb-remote`. Each check configures its target setting via `./tools/evo-remote.sh set <name> <value>`, starts clip playback, reads back real runtime telemetry from `evo.log`, `evo_ui.json`, or `evo_status` to verify the feature in action, prints a single PASS or FAIL evidence line, and restores the original setting on exit. Run a check from the host using `./tools/dx.sh tools/checks/<name>.sh [clip]` or directly inside the dev container.

audio-delay - lip-sync compensation audio delay applied to output (#170)
volume-boost - PCM volume gain boost and clipping applied to audio output (#166)
network-buffer - network stream read-ahead ring size scaling (#199)
diagnostics-panel - debug overlay setting toggle and active playback state (#167)
