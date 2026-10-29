# Media Hub status

Updated 2026-10-29. Hardware tests of this fork: **none**.

| Area | Current state | Next evidence needed |
|---|---|---|
| Native application | EVO-derived app module; isolated title/data identity | Link/package and inspect artifact |
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
