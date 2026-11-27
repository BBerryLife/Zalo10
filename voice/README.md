# Voice engine (1:1 Zalo calls)

Runs next to the app, started by `VoiceCallBridge` (src/core/voice). Speaks JSON lines over
`<appdata>/data/zaloone-engine.sock`.

- `callcore.py` – call signalling state machine (Zalo "voip" control frames)
- `phone_engine.py` – process entry, socket server, ties signalling, media and alerts together
- `relaymedia.py` / `q10media.py` / `videowire.py` – UDP media, Opus, QNX audio, H.264 packetizer
- `root_alerts.py` – system call popup + ringtone (needs libbps system dialogs)
- `session_probe` – native helper that signs/sends call API requests using the app's saved session
  (`imei`, `secretKey`, `cookies` from `BerryLife/Zalo10.conf`)

Incoming calls: with the app in front the call screen (CallScreen.qml) shows. Minimized, the engine sends `open_ui`
and the app opens itself on the call screen (no Hub notification); if it is still not in front after 3 s, one system
popup follows. Locked, the app cannot be shown above the lock screen, so the same 3 s rule ends in the system popup. With the app closed, Zalo10Headless opens the app and hands it the call frame.

Requirements (inherited from the reference build): Python 3.2 at /usr/bin/python3.2 and a device
that lets the app run it with audio access (rooted Q10 in the reference). Voice only; video
(`libq10video`/`libq10camera`) is shipped but not wired to the UI.

## Audio tuning (optional)

Create `<appdata>/data/voice-tuning.json` on the device; it is read at the start of each call.
Every key is optional:

```json
{"dsp": true, "mic_gain": "auto", "mic_target": 1800, "mic_max_gain": 8,
 "far_max_gain": 6, "ns_db": -10, "cushion": 3, "gate": 120, "play_gain": 1.0, "audio_type": 4}
```

- `mic_gain`: `"auto"` (default; AGC toward `mic_target` RMS, capped at `mic_max_gain`, and at `far_max_gain`
  while the far end is audible, when it also never rises so residual echo is not amplified; clip-safe)
  or a fixed multiplier (0.1-16).
- `dsp`: `true` (default) runs the echo canceller/noise suppressor but uses its output only while the far
  end is audible (echo is possible) and uses the raw microphone otherwise; `false` never uses it;
  `"always"` is the previous behaviour (always on).
- `cushion`: how many silent frames (0-4, default 3) the speaker queue may add when the audio stack reports
  it ran dry ("Auto-filling the buffer"); 0 disables the adaptation. The engine reads those warnings from
  its own stderr (still forwarded unchanged to the app log).
- `ns_db`: noise suppression depth in dB (-30..-1) applied to the stock voice processor, which ships at -18;
  `"stock"` leaves it alone. Reported as `cfg_ns` / `ns_applied` in the call counters.
- `play_gain`: software gain for received audio (0.1-8).
- `audio_type`: audio-manager type passed to the PCM open (default 4, the stock value); changes the
  output route. Experimental.

When a call ends the app log prints `[Voice] media: ...` with counters:

- Receive: `received_audio_packets`, `concealed_frames` (frames invented for lost packets; should be a
  small fraction of received), `late_packets`, `resyncs`, `skips` (latency cap), `play_wait` (20 ms
  steps spent waiting because the sender paused), `play_voice_frames` / `play_voice_rms` (received frames
  that actually carry speech; 0 means nothing audible was decoded), `play_silence`, `play_full`.
- Send: `sent_audio_packets`, `mic_speech_frames`, `mic_raw_speech_rms` (mic before DSP, speech only),
  `mic_sent_speech_rms` (what was sent), `mic_gain_x100`, `mic_raw_peak`, `mic_dsp_muted` (speech frames
  the echo canceller removed).
- Device: `play_xruns`, `cap_xruns`. Config actually used: `cfg_*`.
- Loop timing (to find what stalls the engine and starves the speaker): `gap_gt40/gap_gt60/gap_gt100`
  count loop iterations that ran later than 40/60/100 ms, `gap_max` is the worst in ms, and
  `slow_<part>` / `max_<part>` count and bound sections slower than 30 ms (`cap` mic read, `enc`
  DSP+encode+send, `play` decode+speaker, `udp`, `tick`, `alert`).
- `mic_dsp_frames`: frames where the echo canceller output was (partly) used.
- Speaker-queue adaptation: `dev_autofill` / `dev_discard` / `dev_first` count the audio stack's warnings
  ("Auto-filling the buffer", "Had to discard a playback frame", "discarded first playback frame"), and
  `cush_total` is how many silent frames were added in response. If `dev_autofill` stays high with
  `cush_total` at its cap, the underruns are not caused by queue depth.
