# Voice engine (1:1 Zalo calls)

Runs next to the app, started by `VoiceCallBridge` (src/core/voice). Speaks JSON lines over
`<appdata>/data/zaloone-engine.sock`.

- `callcore.py` – call signalling state machine (Zalo "voip" control frames)
- `phone_engine.py` – process entry, socket server, ties signalling, media and alerts together
- `relaymedia.py` / `q10media.py` / `videowire.py` – UDP media, Opus, QNX audio, H.264 packetizer
- `root_alerts.py` – system call popup + ringtone (needs libbps system dialogs)
- `session_probe` – native helper that signs/sends call API requests using the app's saved session
  (`imei`, `secretKey`, `cookies` from `BerryLife/Zalo10.conf`)

Requirements (inherited from the reference build): Python 3.2 at /usr/bin/python3.2 and a device
that lets the app run it with audio access (rooted Q10 in the reference). Voice only; video
(`libq10video`/`libq10camera`) is shipped but not wired to the UI.
