# Optional Windows companion

Open `Start Companion.cmd` on a Windows PC with Python 3 and Tk installed. This development PC already has Python. The helper is source, not a packaged standalone executable.

Opening the helper does not make network connections. Choose the `.ffpfsc` beside `build-manifest.json` to check its SHA-256 and size offline. Enter the console's private LAN IPv4 address and its existing FTP port. Do not change console settings to fit a default port.

Every transfer requires a confirmation displaying the target, filename, byte count and hash. A ShadowMountPlus installation may automatically mount or launch the uploaded image: the confirmation explicitly covers this possibility. Do not approve a transfer if another native app is running or if the loader configuration is unknown. No transfer has been attempted during development.

FTP is unencrypted. Optional FTP credentials are entered locally and kept only in memory; never put them in chat or Git. Upload writes only `/data/homebrew/PPSA99179.ffpfsc`. It does not contain an exploit installer, console settings changes, process-killing commands or a remote launch payload. A failed transfer may leave a partial image; no automatic retry is attempted. Remote hash verification is not implemented, so a completed FTP upload is not proof of successful installation.

Log collection separately asks approval to connect to an existing log TCP service for at most 15 seconds and 2 MiB. It saves a local file only. URLs and common credential fields are redacted on a best-effort basis; review before sharing. TCP 9081 is an etaHEN klog convention, not a promise that kstuff or ShadowMountPlus supplies it. Media Hub's own inherited diagnostic file is separate; collecting that via FTP is follow-up work.

Offline tests: `python companion/test_companion.py` from the project root. Tests cover LAN target restrictions, modified-artifact rejection and common log secrets. Network deployment and log collection remain untested on PS5 hardware.
