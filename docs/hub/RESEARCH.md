# Source inspection and platform decision

Inspected public source on 2026-10-29, with local clones retained during development.

| Project | Inspected revision | Licence and assessment |
|---|---|---|
| [EVO Player](https://github.com/sainsaji/EVO-PLAYER-PS5) | `2db44b5294657b12bf91666154a05f029ddb5aa2` | Root COPYRIGHT.md explicitly requires GPL-3.0-or-later, tracing ProsperoPlayer ancestry. Native AGC rendering, sceVideodec2 decode, controller UI and Stremio provider make this the preferred base. |
| [BFplayer](https://github.com/ItsBlurf/BFplayer) | `ad3bdc9d7742606913701dac035f29e2ab49b080` | README states GPL-3.0-or-later; root LICENSE contains GPLv3. Native SDL/FFmpeg player, library, subtitles and standalone media tile. Useful alternative/reference; no BFplayer code has been copied into this fork. |

EVO's `projects/evoplayer/addons/src/provider_stremio.c` implements manifest, catalog, metadata and direct HTTP(S) stream resolution. It skips torrent-only infoHash streams; this is not a TorBox integration. Its existing subtitle playback support is distinct from add-on subtitle resource lookup.

## Toolchain and execution

Use the source's pinned Docker toolchain: Ubuntu 24.04, LLVM 18, PS5 Payload SDK v0.42, pacbrew v0.39, FFmpeg 7.1.1. The SDK is community-maintained; no proprietary Sony SDK is required. Existing clean-room runtime/app conversion tools and bundled shader headers are inherited, with provenance kept.

Primary sources: [SDK](https://github.com/ps5-payload-dev/sdk), [kernel offset source](https://github.com/ps5-payload-dev/sdk/blob/master/crt/kernel.c), [etaHEN](https://github.com/etaHEN/etaHEN), [EVO build guide](https://github.com/sainsaji/EVO-PLAYER-PS5/blob/main/docs/build/building.md).

The SDK source at the pinned v0.42 tag contains firmware 10.00 offsets (`case 0x10000000:`), verified from its raw source after the build. The release archive omits this source file, so this is release-tag source evidence alongside the compiled SDK smoke test. etaHEN documents an ELF loader on TCP 9021 and app jailbreak facilities. Neither presence of SDK offsets nor a loader demonstrates that this app module boots on 10.00.

The selected full-player distribution is a native application module packaged as a folder-title ZIP and/or `.ffpfsc` for a compatible app loader (EVO documents ShadowMountPlus). A plain payload ELF has different process/session constraints and is not equivalent to the full native application. The user already has a jailbroken Digital Edition; do not propose disc-only entry points or install a new exploit as part of building this app. Ask which HEN/loader is already in use before deployment.

Upstream's README claims broad firmware support, while its build guide identifies PS5 12.70 for development/testing. Treat 10.00 as unverified until this fork is run on compatible hardware.

## Service contracts to use

- [Stremio protocol](https://github.com/Stremio/stremio-addon-sdk/blob/master/docs/protocol.md): HTTPS JSON resources; never execute add-on code locally. Preserve resource/type/idPrefix filtering, URI encoding, paging and bounded responses.
- [Trakt API](https://trakt.docs.apiary.io/): device authorization, then authenticated watchlist/collection/history/progress and scrobbling. This session's documentation fetch timed out; verify exact error/retry semantics before implementation.
- [TorBox API](https://api-docs.torbox.app/): documentation is client-rendered. Verify actual auth/API schema rather than inventing an OAuth or device flow. A supported TorBox-configured Stremio add-on may return direct links, but this does not provide cloud management or native account linking.
- [SponsorBlock API](https://github.com/ajayyy/SponsorBlock/wiki/API-Docs): read segment ranges, apply selected categories and bounds, treat 404 as no segments; keep attribution and database licence requirements.

4K, HDR, Dolby Vision and audio passthrough implementations are inherited from EVO. Their presence is source evidence only. HDR and surround capabilities also depend on HDMI chain, decoder profiles, firmware and homebrew process permissions.

## Build repair: unavailable UI revision

The original `blackbearreloaded/ps5-homebrew-ui` repository returns 404 to Git, and its pinned `47f4b9868c33264716b2e92922b0d2c9d0f2ab0f` revision was unavailable in the public mirrors checked. Use the EVO maintainer's public mirror `sainsaji/ps5-homebrew-ui` pinned to `4b3fbb73fa309579570d42d0d7fcd60399d8b03b` (release 01.000.010). Its GPL licence and third-party notices remain in the submodule.

This public release supports baked font fallback but lacks the later ComplexText interface. Media Hub adapts `set_fallback` to `add_fallback` and omits the dynamic Thai/Indic/CJK shaping module from the first build. Missing UI glyphs may display question marks. The limitation must remain visible in status; restoring shaped international UI text is follow-up work. No fake no-op ComplexText API has been added. Subtitle playback uses the separate inherited subtitle path and still requires hardware tests.
