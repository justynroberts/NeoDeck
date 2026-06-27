# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

DeckSSH — firmware for the **LilyGo T-Deck Plus** (ESP32-S3) that turns it into a
handheld SSH client with stored connection profiles and an on-device Claude-powered
assistant. C++ / Arduino framework, built with PlatformIO.

## Build / flash / monitor

```bash
pio run                 # compile
pio run -t upload       # flash firmware over USB-C
pio run -t uploadfs     # flash the LittleFS image (data/ -> /config.json etc.)
pio device monitor      # serial @115200 (exception decoder enabled)
```

There is no host-side test suite; this is embedded firmware. "Verifying" a change
means it compiles (`pio run`) and behaves correctly on the device. The maintainer
flashes hardware — you generally cannot run it from CI.

## Architecture (the big picture)

Single-binary firmware with a cooperative main loop in `src/main.cpp`:
`poll input → UI::handle → UI::tick → UI::render` every few ms. Three things make
the structure non-obvious:

1. **SSH runs in its own FreeRTOS task** (`src/ssh_session.cpp`). libssh is
   blocking and needs a ~50 KB stack, so it cannot live in the UI loop. The task
   and the UI thread communicate **only** through two mutex-guarded `String`
   buffers (`inBuf_` server→screen, `outBuf_` keyboard→server). Never call libssh
   from the UI thread; push/drain the buffers instead. The task is pinned to
   core 1 (WiFi owns core 0).

2. **The UI is an immediate-mode state machine** (`src/ui.cpp`), not a widget
   tree. A single `Screen` enum + a `dirty` flag drive `render()`. The terminal
   screen redraws every tick when `termDirty`; other screens redraw only when
   `dirty`. Adding a screen = add the enum, a `drawX`, and a `handleX`.

3. **The terminal is a hand-rolled emulator** inside `ui.cpp` (`termFeed`/
   `termDraw`): a `vector<String>` scrollback + a current line. It strips ANSI/CSI
   escapes rather than rendering color, and handles `\n`, `\b`, `\t`.

Other modules are leaf utilities: `input` (I2C keyboard + trackball → `InputEvent`),
`storage` (LittleFS ⇄ `gConfig` JSON via ArduinoJson), `wifi_mgr`, `keys`
(reads PEM private keys from `/keys`), `tls` (pins `/ca/anthropic.pem` for the AI
client, else falls back to insecure), and `ai` (Claude Messages API over HTTPS —
raw HTTP, no SDK exists for C++).

SSH auth is selected per-profile by `SshEnv.auth` (0 = password via
`ssh_userauth_password`, 1 = public key: the PEM is read from `/keys/<keyName>`
and imported in-memory with `ssh_pki_import_privkey_base64`). The terminal
interprets SGR color escapes — lines are stored as `TLine { String t;
vector<uint8_t> c; }` (one palette index per char) and drawn as color runs.

`gConfig` (defined in `storage.cpp`, declared in `config.h`) is the single source
of truth for WiFi creds, API key, AI model, and the list of `SshEnv` profiles.

## Conventions / gotchas specific to this repo

- **TFT_eSPI is configured entirely via `build_flags`** in `platformio.ini`
  (`USER_SETUP_LOADED=1` + the pin/driver defines). Do **not** add a
  `User_Setup.h`; change display config in `platformio.ini`.
- **Board pins live in `include/config.h`** and are the documented T-Deck values
  but unverified on hardware — flag pin changes clearly. `BOARD_POWERON` (GPIO10)
  must be driven HIGH at boot or every peripheral is dead.
- Arduino `String` has **no repeat-char constructor**; use the `dots(n)` helper in
  `ui.cpp` for masked fields, not `String('*', n)`.
- The AI client uses `claude-opus-4-8` by default (configurable to
  `claude-haiku-4-5` for latency). It calls the Anthropic API with raw
  `HTTPClient` + `WiFiClientSecure`; keep the request shape (`model`,
  `max_tokens`, `system`, `messages[]`) and `anthropic-version: 2023-06-01`.
- Keep libssh stack at ≥ 50 KB if you touch `xTaskCreatePinnedToCore` in
  `ssh_session.cpp`; smaller stacks crash during key exchange.

## Roadmap markers (intentionally unfinished)

Host-key verification is trust-on-first-use (the SSH task sets `State::HOSTKEY`
and blocks on `hostKeyDecision_` until the UI calls `resolveHostKey()`; accepted
keys persist to `/littlefs/known_hosts`). Remaining gaps: no `known_hosts`
editor, terminal color is SGR-foreground + bold only (no backgrounds /
256-color), plaintext secrets at rest, and no on-device key-passphrase entry
(set `keyPass` via a pre-seeded `config.json`). These are in the README's *Known
rough edges* — preserve those notes when editing.
