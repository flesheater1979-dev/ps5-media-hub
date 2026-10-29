# Media Hub status

Updated 2026-10-29. Hardware tests of this fork: **none**.

| Area | Current state | Next evidence needed |
|---|---|---|
| Native application | Compiled and packaged v0.1.0-hub-test1; executable integrity and ZIP structure checked | First hardware launch |
| Firmware 10.00 | Target, not validated | Actual HEN/loader details and launch test |
| DualSense / TV UI | Inherited native implementation | D-pad, Cross, Circle and exit on target |
| International UI text | Baked fallback fonts; dynamic Thai/Indic/CJK shaping omitted for public UI release compatibility | Restore newer kit API and render multilingual fixtures |
| Local/network media | Inherited playback engine | Local 1080p stereo sample first |
| 4K / HDR / surround | Upstream implementation/research exists | Decoder/output and display/receiver tests on 10.00 |
| Stremio | Inherited catalogs/meta/direct HTTP streams | Fixture tests then live compatible add-on |
| Add-on subtitles | Not yet wired to subtitle resource | Implement resource matching and bounded parsing |
| Trakt | Planned device auth + watchlist/collection/history/progress | Registered app, protected token storage, live test |
| TorBox | Direct API integration pending | Verify auth, cloud list and streaming API contracts |
| YouTube | Not implemented | Choose supported playback/auth path; SmartTube is Android code, not a native PS5 backend |
| SponsorBlock | Planned | Segment API adapter and player seek hook |
| Windows companion | Optional Python/Tk GUI: offline hash validation, confirmed FTP image transfer, confirmed bounded log collection | GUI and console network tests; never deployed automatically |

## Validation ladder

Keep a record per build: SHA-256, source commit, firmware, HEN version, loader/version, TV/receiver, test media properties, result and logs. Mark each stage PASS/FAIL/NOT RUN. A compiled artifact is not a boot test.

1. Package structure and executable format inspection.
2. Approved installation and launch; no network provider required.
3. Navigate every screen and exit cleanly.
4. Play/pause/seek a known local 1080p H.264/AAC stereo sample.
5. External and embedded subtitles; resume persistence.
6. Network stream; cancellation and disconnected network behavior.
7. Codec-specific 4K, HEVC Main10, HDR and surround tests separately.

Deployment remains pending explicit user approval of a named artifact and target console.

## Local evidence, 2026-10-29

- Pinned Clang 18 / SDK v0.42 toolchain built; SDK hello_world ELF smoke test passed.
- Confirmed firmware 10.00 offset case in upstream SDK tag v0.42 `crt/kernel.c` (release archive does not include that source path).
- FFmpeg 7.1.1 minimal profile compiled and installed.
- Host provider/settings/navigation/audio tests: **15/15 groups passed**. The suite builds with `NO_OPENSSL=1`; it does not validate live TLS or external accounts.
- Native application compiled, linked and signed as a homebrew plaintext container; container inspection reported `integrity: valid`.
- Native `.ffpfsc` (27,852,800 bytes) and folder ZIP (26,891,571 bytes) produced. ZIP CRC, title identity and required executable/runtime/metadata entries checked.
- 96 local fixture screenshots rendered with GL error 0x0. Branding, home navigation and About-screen status visually inspected. Fixtures use sample metadata and placeholder artwork; they are not live provider or console evidence.
- Companion offline tests: **4/4 passed**, covering target restrictions, changed-image rejection, common secret redaction and verified-readback activation (using a fake FTP server). Console networking and GUI interactions have not been validated.
- Native binary source commit: `157698e10d71268e4666c982ca1dd3df240f17cb`. Later companion/build-export/documentation changes do not change the native executable.
- User reports ShadowMountPlus + kstuff. No console address supplied and no transfer/launch attempted.
