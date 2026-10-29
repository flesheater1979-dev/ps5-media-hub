# PS5 Media Hub

Experimental native PS5 media application targeting a jailbroken **PS5 Digital Edition, firmware 10.00**. No feature of this fork has been verified on PS5 hardware yet.

Based on [EVO Player](https://github.com/sainsaji/EVO-PLAYER-PS5) at `2db44b5294657b12bf91666154a05f029ddb5aa2`. Original notices and GPL-3.0-or-later terms are retained. See [COPYRIGHT.md](COPYRIGHT.md), [projects/evoplayer/NOTICE](projects/evoplayer/NOTICE) and [README.upstream.md](README.upstream.md). Upstream documentation describes EVO, including upstream hardware results; it is not evidence of tests of this fork.

## First build

The first milestone keeps the existing native GPU interface, controller navigation, local/network playback, subtitles and Stremio provider engine. Media Hub has a distinct title ID (`PPSA99179`, locally chosen, not an officially allocated ID), title and data directory (`/data/ps5-media-hub`). The Movies & TV navigation entry opens the existing native provider screen. EVO's upstream self-updater is disabled.

Trakt, direct TorBox cloud integration, add-on subtitle retrieval and YouTube account/playback integration are **not complete**. No accounts or passwords are needed for the first boot test. See [docs/hub/STATUS.md](docs/hub/STATUS.md).

## Build without contacting a console

```sh
git submodule update --init --recursive
docker compose build
docker compose run --rm ps5-dev ./tests/run_tests.sh
docker compose run --rm ps5-dev ./scripts/build-ffmpeg.sh --profile minimal --install
docker compose run --rm ps5-dev ./scripts/package-app.sh --ffpfsc --zip
```

The Linux container uses Clang 18, PS5 Payload SDK v0.42, pacbrew v0.39 and FFmpeg 7.1.1. The package commands compile only. A console is not connected to this build environment. Runtime compatibility depends on the console's existing HEN and application loading setup; firmware number alone is insufficient.

Do not run inherited deployment, remote-control, cleanup or lifecycle tools without reviewing their target and obtaining explicit approval. No deployment or console execution is authorized automatically by a successful build. Companion tooling must present each transfer and execution for approval.

## Accounts and security

Never submit account passwords or API tokens through chat, GitHub issues or source files. Trakt should use its device authorization flow after application registration. TorBox's supported authorization mechanism needs verification before implementing account linking. Store credentials in a Windows-protected companion vault where possible; do not claim PS5 filesystem permissions provide a secure keystore. Disable credential-bearing request logging before connecting live accounts.

## Development order

1. Compile, package, then validate launch, D-pad/Cross/Circle navigation and clean exit on firmware 10.00.
2. Validate a local 1080p stereo sample, audio and subtitles before 4K/HDR/surround tests.
3. Extend Movies & TV: Trakt linking and sync, TorBox cloud browsing, add-on subtitles and discovery.
4. Add YouTube browsing/playback where supported, then SponsorBlock and subscriptions.

Source and matching binary artifacts must be supplied together when distributing this GPL derivative.
