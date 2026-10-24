# Codec support

What EVO decodes. **Built** means the decoder is compiled into the FFmpeg `minimal`
profile (`scripts/build-ffmpeg.sh`); **Verified** means it was played on a real PS5
and seen decoding, not inferred from the build configuration. Hardware rows come
from `g_codec[]` in `media/src/evo_vdec_native.c`. How video decode is arranged is in
[video-decode.md](video-decode.md); audio in [audio.md](audio.md).

---

## Video

| Codec | Decoder | Max | Verified |
|---|---|---|---|
| H.264 / AVC, up to High | hardware (`sceVideodec2`) | 4K60, real time | yes |
| HEVC Main (8-bit) | hardware | 4K, real time | yes |
| HEVC Main10 (10-bit, HDR10 / HLG) | hardware; FFmpeg if the hardware declines the stream | 4K | yes |
| VP9 Profile 0 | hardware | 4K | yes |
| VP9 Profile 2 (10-bit) | hardware | 4K, real time | yes |
| AV1, including 10-bit | software (libdav1d) | 4K 10-bit, real time | yes |
| MPEG-2 | software | 1080p | yes |
| VP8 | software | 1080p | yes |
| MPEG-4 Part 2 / DivX | software | 1080p | no |
| MPEG-1, H.263 / Sorenson, FLV, VP6 | software, built | | no |
| VC-1, WMV 1/2/3, MS-MPEG4 | software, built | | no |
| Theora, MJPEG, Cinepak, Indeo, RealVideo, DV, CAVS | software, built | | no |
| ProRes, DNxHD, FFV1, HuffYUV, UtVideo | software, built | CPU-bound | no |

Five hardware decoders (AVC, HEVC, VP9, HEVC Main10, VP9 Profile 2) exist at boot at
1080p and are resized to the stream on demand. Settings → Video Decoder `AUTO`
prefers them; `FFMPEG` forces software.

- **4K software decode is allowed by default** (#95). It used to be refused for
  memory reasons that #94 answered: the malloc shim serves large blocks from direct
  memory, and 4K 10-bit AV1 plays and seeks in real time with flexible memory at 200+
  MB free. What is left is speed. `/mnt/usb0/evo_no_sw_4k` switches it back off
  ([../console/diagnostics.md](../console/diagnostics.md#switches)).
- **The old "4K60 H.264 reaches ~14 fps" was a misread number** (#162): the sweep's
  `dec_fps` divided frames by the whole wall-clock window, idle time after the clip
  ended included. Real playback of `EVO_TEST_h264_4k60.mp4` is native at 0.80 ms per
  frame, 474 frames, no late drops. VP9 Profile 2 used to fall back because boot
  skipped its slot; boot now creates it at 1080p and grows it to 4K on demand.
- **Interlaced content is not deinterlaced yet** (#188).
- Dolby Vision Profile 5 is reshaped on the GPU from the RPU ([video-decode.md](video-decode.md#colour-hdr-and-dolby-vision)).
- **Raw AV1 streams** (#94): `.obu` (low-overhead OBU) plays and is listed in the
  browser; Annex B and `.ivf` are built too. A raw stream carries no timestamps, so
  the frame rate comes from a `<n>fps` hint in the file name (`...-2397fps-...`
  becomes 24000/1001), else FFmpeg's default of 25. The first seek into an unread
  part has to scan (no index); remux to `.mkv` for instant seeks.

Not built: nothing proprietary and nothing needing an external library beyond
libdav1d. FFmpeg's own `av1` decoder is excluded on purpose (it is a hardware-only
wrapper that faults without hardware).

---

## Audio

| Codec | Decoder | Channels tested | Verified |
|---|---|---|---|
| AAC, AAC-LATM | native `libSceAudiodec`, FFmpeg otherwise | 2, 5.1, 7.1 | yes |
| MP3 / MP2 | native `libSceAudiodec` (MP3), FFmpeg | 2 | yes |
| AC-3 | FFmpeg; passthrough optional | 5.1 | yes |
| E-AC-3 (incl. the Atmos bed) | FFmpeg; passthrough optional | 5.1, 7.1 | yes |
| DTS core, DTS-HD HRA / MA, DTS:X (bed) | FFmpeg; passthrough optional | 5.1, 7.1 | yes |
| Dolby TrueHD / MLP (incl. the Atmos bed) | FFmpeg; MAT passthrough, experimental | | built, not verified |
| FLAC | FFmpeg | 5.0, 7.1 | yes |
| Opus, Vorbis | FFmpeg | 2 | yes |
| ALAC, raw PCM (every width) | FFmpeg | | no |
| WMA (v1, v2, Pro, Lossless, Voice), WavPack, Monkey's Audio, TTA, TAK, Musepack | FFmpeg, built | | no |
| ATRAC, Cook / RealAudio, AMR, GSM, DSD, ADPCM variants, Shorten and the rest of FFmpeg's native set | FFmpeg, built | | no |

Output is an 8-channel (7.1) 48 kHz S16 port whenever the source has more than two
channels, otherwise stereo, unless Output Channels is `STEREO`. Object audio is not
rendered: Atmos and DTS:X play their bed. With `AUDIO PASSTHROUGH` on, AC-3,
E-AC-3, DTS and TrueHD go to a receiver as the original bitstream; AAC never does.
Everything FFmpeg decodes natively was enabled on 2026-09-24 so that "plays with no
sound" stops being a per-format discovery.

---

## Subtitles

| Format | Decoder | Verified |
|---|---|---|
| SRT / SubRip | software | yes |
| ASS / SSA | software; text and timing only, not styling (#43) | yes |
| WebVTT, MicroDVD, SAMI, SubViewer, MPL2, VPlayer, PJS, JACOsub, RealText, STL, XSUB | software, built | no |
| MOV text (mp4) | software | no |
| PGS (Blu-ray bitmap), DVB, DVD (VobSub) | software | no |

Subtitle files in legacy code pages (Windows-1250..1257, 874, GBK, Big5, Shift-JIS,
EUC-KR) are converted to UTF-8 on load (`media/src/evo_charset.c`). Latin
extended, Greek, Cyrillic, Hebrew, Arabic, Thai and the Indic scripts render from
embedded fonts through the kit's text path; Chinese, Japanese and Korean need
`NotoSans{SC,JP,KR}-Regular.otf` in `/data/evoplayer/fonts/`
([../ui/hui-kit.md](../ui/hui-kit.md#text-in-every-script-128)). Two subtitle tracks
can show at once, and Subtitle Picker → Auto-Sync aligns an external SRT to the audio
(#102).

---

## Containers and protocols

MP4/MOV, Matroska/WebM, AVI, MPEG-TS, MPEG-PS, Ogg, WAV, FLAC, ASF/WMV, FLV,
RealMedia, DV, MXF, and raw AV1 (`.obu`, Annex B, IVF) and raw MP3/AAC/AC-3/E-AC-3/
DTS/TrueHD streams. MP4, MKV, WAV, FLAC and `.obu` are verified; the rest are enabled
and untested. Network: `http`, `https` (static OpenSSL), HLS and DASH, with
reconnect on a dropped connection.

Sources of truth: hardware rows from `g_codec[]` in `media/src/evo_vdec_native.c`;
software rows from the `*_DECODERS` arrays in `scripts/build-ffmpeg.sh`; verification
from the codec sweeps ([../build/validation.md](../build/validation.md#the-codec-sweep)).
