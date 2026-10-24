# Audio

How sound gets from a file to the speakers or a receiver: decode, the output port,
the gain and timing controls, and bitstream passthrough. The codec list is in
[codec-support.md](codec-support.md).

## Decode and the output port

- **AAC and MP3** decode on the console's own `libSceAudiodec` (AJM-backed) where it
  can (`media/src/evo_adec_native.c`). Everything else (AC-3, E-AC-3, DTS family,
  FLAC, Opus, Vorbis, ALAC, TrueHD...) decodes in FFmpeg. The Audiodec probe runs
  before the self-unjail, because the credential swap breaks it.
- The decoded audio is resampled to **48 kHz S16** and written to one `sceAudioOut`
  MAIN port opened as the system user (`0xFF`): an **8-channel** port when the
  source has more than two channels, otherwise stereo. Settings → Audio → Output
  Channels = `STEREO` downmixes 5.1/7.1 to two speakers. The interleave is **FL FR
  FC LFE BL BR SL SR**; a 5.1 source uses channels 4 and 5 as the surrounds (drawn
  side-rear in Surround Studio) and leaves 6 and 7 empty.
- Object audio is not rendered: Atmos and DTS:X play their bed as 5.1/7.1 PCM. The
  PS5 cannot be handed an Atmos or DTS:X object stream any other way except
  passthrough to a receiver that decodes it.
- The audio clock (`pp/src/pp_audio_clock.c`) is the master clock for A/V sync; video
  waits on it. For the first fraction of a second after an open or a seek neither
  side throttles the other, because both clocks restart at zero.

### Controls on the PCM path

| Setting | What it does |
|---|---|
| Volume Boost (`OFF`, +50, +100, +200, +300 %) | `evo_audio_apply_gain_s16`: scales decoded PCM in int32 and clamps to ±32767, so loud passages clip cleanly instead of wrapping into noise. No effect on passthrough |
| Audio Delay (-500..+500 ms) | shifts the audio against the video clock to fix lip-sync on a TV, soundbar or Bluetooth speaker; positive means sound later |
| Surround Sound Test and calibration | per-channel tones, and a DualSense-microphone room measurement whose gain and delay profile (`speaker_calibration.cfg`) is applied to playback in `audio_queue_push` ([../ui/surround-studio.md](../ui/surround-studio.md)) |

## Bitstream passthrough

Settings → Audio → `AUDIO PASSTHROUGH` (default off) sends **Dolby Digital, Dolby
Digital Plus (including the Atmos bed) and DTS** over HDMI as the original
bitstream, so a soundbar or receiver decodes it. **Dolby TrueHD** is sent as MAT and
is experimental. Hardware-proven on a PS5 Pro, FW 12.70, with a soundbar that
showed the format and played it (2026-10-08); the receiver's Atmos indicator was
not checked, and TrueHD has played in the standalone research app but the receiver
showed "Dolby Audio", not "Dolby TrueHD".

**AAC is never passed through.** It is lossy stereo, decoding it to PCM loses
nothing, and a sink that advertises AAC over HDMI (an LG UltraGear) played an AAC
bitstream as silence (#163).

### How it is done

The recipe is the one the PS5's own media apps use, found in citroncore:

```
h = sceAudioOutExOpen(0xFF, mode)
sceAudioOutExConfigureOutput(0, 0, mode, 1 /*HDMI*/, 0)
… sceAudioOutOutput(h, grain) …     // IEC 61937 bursts as stereo S16 samples
sceAudioOutOutput(h, NULL)          // drain
sceAudioOutExClose(h)
sceAudioOutExConfigureOutput(0, 0, 0xFF, 0xFF, 0)    // restore PCM
```

`ExOpen` + `ExConfigureOutput` is what switches the HDMI transmitter into non-audio
mode; without it the daemon sees non-PCM data on a PCM-labelled link and mutes the
port. The carrier per codec comes from `sceAudioOutExOpen`'s own tables
(`pt::carrier_for`):

| Codec | Mode | Port | Grain |
|---|---|---|---|
| AC-3 | 0 | 48 kHz | 256 frames |
| DTS | 2 | 48 kHz | 256 frames |
| E-AC-3 | 3 | 192 kHz | 1024 frames |
| TrueHD (MAT) | 5, `sceAudioOutSysOpen` | 768 kHz, 8 channels | 1024 frames |

- Code: `core/src/audio/evo_pt.cpp` (the port lifecycle, mutex-guarded grain queue
  and the settle timings), `iec61937.cpp` (frame parsing and burst packing: `Pa
  0xF872`, `Pb 0x4E1F`, data type, length code, big-endian payload, zero padding to
  the burst length), `bitstream.cpp` (the HDMI sink's capability list).
- Before opening, `evo_pt_probe_sink_support()` reads the sink's audio descriptors
  (CEA-861 coding types from the EDID, via `sceAudioOutSysGetHdmiMonitorInfo`). A
  sink that reports none at all (a capture card, a splitter) is allowed to try.
- **Receivers follow format changes slowly.** Switching formats back to back left one
  stuck on the previous format. So a switch waits out a settle time after the last
  reset, the stream is bracketed by IEC 61937 null bursts (400 ms lead-in, 500 ms
  lead-out), and the reset is spaced 250 ms from the port close.
- The TrueHD route (the 768 kHz system port, MAT) is the one the Blu-ray player uses;
  its "768 kHz" port label is really consumed at 192 kHz.
- A seek calls `evo_pt_reset()` to drop queued grains.
- Log lines are prefixed `[PT]`; `audio: source 2ch -> port 2ch (pt=0)` shows the
  decision per file.

### What did not work

Two routes that look obvious fail, and both were measured on hardware (2026-09-26):

- **IEC 61937 inside an ordinary PCM port** (`sceAudioOutOpen`, S16 stereo): the HDMI
  Channel Status stays "Linear PCM", so the receiver plays the compressed payload as
  full-scale noise.
- **`sceAudioOutPtOpen` / `sceAudioOutExPtOpen`** (the undocumented passthrough
  ports, frequency 48000, param 14 or 12): the port opens and accepts buffers, but
  with the transmitter not reconfigured the audio daemon mutes it. Silence.

The Settings → Sound → Audio Format (Priority) entries on the console are not
passthrough either: they make the system mixer re-encode the whole mix to lossy
Dolby Digital or DTS.

## Open work

Native audio decode and passthrough breadth are tracked in the roadmap
([../planning/roadmap.md](../planning/roadmap.md)): #47 (native decode and
passthrough), #163 (AAC passthrough and a split audio reader, rolled back because
it broke A/V sync; to be redone in measured steps), #178 (a network stall must not
flush the audio queue), #210 (loudness, night mode, room EQ).
