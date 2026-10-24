# PS Remote Play from the PC (chiaki-ng)

**Works** (paired and streamed 2026-10-07 on FW 12.70, offline-activated account,
no PSN). It reaches what `evo-remote.sh` cannot: the dev remote injects into EVO's
own pad state and `key l3` captures only EVO's framebuffer, so neither shows nor
drives a **system dialog** (the native IME, the web browser dialog). A Remote Play
session shows the real screen and sends real pad input.

On an offline-activated account the normal Settings → Remote Play → *Link Device*
screen is blocked, but the function behind its PIN, `sceRemoteplayGeneratePinCode`,
works without patching. LinkDev (bundled with `websrv`) calls it and shows the PIN
and the account id on the TV. A real PSN id is only needed for Sony's official
Remote Play app, not for chiaki-ng.

This changes console state and reaches the screen. Do not start a stream, or any
console action, the owner has not asked for, and close EVO first (`evo-remote.sh
quit`, then `close`): never send a payload while EVO is submitting GPU work.

## PC side

| | |
|---|---|
| Client | chiaki-ng v1.10.0 (AGPL-3.0), the `win_x64-MSYS2-Release-portable` build, unpacked outside the repo at `%USERPROFILE%\tools\chiaki-ng\app\chiaki-ng-Win\chiaki.exe` (override with `CHIAKI_NG_EXE`) |
| Launcher | `./tools/chiaki.ps1 where \| open \| stream [-Fullscreen]` (PowerShell; reads `PS5_HOST` from `.env.local`, else `.env`). `open` runs chiaki's own LAN discovery; `stream` connects to an already-registered console |
| Payload | LinkDev from the `websrv` bundle (v0.34). The separate `rp-get-pin.elf` (idlesauce/ps5-remoteplay-get-pin, ptrace-based) has no licence and is kept out of this repo |

## Pairing, in order

1. **Offline activation** of the account with `offact` (`websrv` v0.14 or newer).
   It is persistent: check first whether it is already done (OffAct shows
   `type np flags 0x1002`).
2. Send `websrv` to elfldr on `:9021` and launch **LinkDev** from its page on
   `:8080`. It shows a PIN and an Account ID on the TV, valid for 300 s.
3. In chiaki-ng: add the console, **Register**, paste the base64 Account ID, type
   the PIN, mode PS5. Do not use chiaki's PSN login button.
4. `./tools/chiaki.ps1 stream -Fullscreen`.

## Pitfalls

- **Do not retype the Account ID by eye.** The TV font makes `l`, `I` and `1` look
  alike; the first attempts failed with HTTP 403 "Invalid PSN ID" for exactly that.
  The default ID of an account named `<name>` is FNV-1 (offset 0, prime
  `0x100000001B3`) over the name's bytes, base64 of the 8 bytes little-endian, so
  you can compute what it should be.
- **A failed pairing uses up the LinkDev session.** Exit with Circle and relaunch
  for a new PIN before each retry.
- LinkDev prints the ID with two `=`; one is the standard form and decodes to the
  same bytes.
- The console's address moves after a power cut; `ps5-remoteplay status` finds it.
- Windows Firewall asks to allow chiaki-ng (UDP) the first time.
- Open question: whether a ptrace-based payload on ShellUI can upset a running EVO.
  Not tested; hence "close EVO first".

## To drive a system dialog

Set Settings → Interface & Storage → `KEYBOARD INPUT` = `NATIVE PS5 IME`, open the
dialog from EVO, and use the stream's keyboard. For tests that type text *without*
Remote Play, choose `VIRTUAL KEYBOARD` instead and use `evo-remote.sh type`
([remote-control.md](remote-control.md)).
