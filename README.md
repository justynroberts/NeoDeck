# NeoDeck

A full-featured **SSH client firmware** for the LilyGo **T-Deck Plus** (ESP32-S3 +
BlackBerry-style keyboard + trackball + 320×240 ST7789). Stored connection
profiles, a real cursor-addressable terminal over [libssh](https://www.libssh.org/),
an on-device **AI co-pilot** powered by the Claude API, and a Matrix-styled UI with
tumbling katakana rain.

> **Flash it from your browser:** **https://justynroberts.github.io/NeoDeck/**
> (Chrome/Edge desktop, Web Serial). Or load it from an SD card via the
> [bmorcelli Launcher](https://github.com/bmorcelli/Launcher).

## Features

- 🔐 **Interactive SSH** — full PTY shell via libssh in its own FreeRTOS task so the
  UI never blocks. **Password or public-key auth**, host-key verification (TOFU).
- 🖥️ **Real terminal** — a cursor-addressable VT100/xterm screen grid: CSI cursor
  positioning + erase, SGR **foreground and background** colors, UTF-8, OSC title
  swallowing, 8-column tabs. `vim`, `nano`, `htop`, `top`, `less` render in place.
- ⌨️ **Session actions** — trackball click opens a menu: **Ctrl-C, Tab, Esc, Ctrl-D**,
  **scrollback**, clear screen, *Ask AI*, *Paste last AI command*, disconnect.
- 💾 **Stored connections** — named host/user/port + password *or* key profiles,
  persisted as JSON. Add / edit / delete on-device. **Peek** masked secrets.
- 🤖 **AI assistant** — ask Claude a question, optionally with the last terminal
  output as context; the first line of the reply is a runnable command. **TLS is
  validated** against a pinned root CA when present.
- 🌧️ **Matrix UI** — phosphor-green-on-black theme, tumbling **katakana** digital
  rain behind a translucent main menu, custom vector icons, double-buffered
  (flicker-free) rendering.
- 🔣 **Powerline glyphs** — the common separators/symbols (U+E0A0–E0B3) are drawn as
  vectors, since no embedded font carries them.
- 📶 **WiFi manager** with network picker and stored credentials.

## Hardware

LilyGo T-Deck **Plus** (ESP32-S3, 16 MB flash, 8 MB PSRAM). PSRAM is **required** —
libssh + mbedTLS + scrollback + the framebuffer will not fit in internal RAM.

## Install

### Option A — Web flasher (easiest)

Open **https://justynroberts.github.io/NeoDeck/** in Chrome or Edge on desktop,
plug the T-Deck in over USB-C, and click **Install**. This writes a full image at
`0x0` and **replaces whatever firmware is on the board**.

### Option B — From an SD card via the Launcher

Keeps the [bmorcelli Launcher](https://github.com/bmorcelli/Launcher) as the boot
menu and runs NeoDeck as an app:

1. Flash the Launcher for the T-Deck Plus once (its merged release bin at `0x0`).
2. Copy `NeoDeck.bin` (an app-only `firmware.bin`) onto a **FAT32 / MBR** SD card.
3. In the Launcher open **SD**, select `NeoDeck.bin`, choose **Install**.
4. Hold **select / trackball at power-on** to return to the Launcher (or enable
   *Boot to Launcher* in its settings).

NeoDeck stores its config on the **SD card** (`/deckssh/config.json`) when one is
present, so settings survive launching in and out.

### Option C — Build from source (PlatformIO)

```bash
pio run                 # build
pio run -t upload       # flash over USB-C
pio device monitor      # serial console @115200
```

To regenerate the browser-flasher image after a build:

```bash
esptool.py --chip esp32s3 merge_bin -o docs/firmware/NeoDeck-merged.bin \
  --flash_mode keep --flash_freq keep --flash_size 16MB \
  0x0     .pio/build/tdeck-plus/bootloader.bin \
  0x8000  .pio/build/tdeck-plus/partitions.bin \
  0xe000  <framework>/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/tdeck-plus/firmware.bin
```

## First run

1. **WiFi** → pick a **2.4 GHz** network (ESP32 has no 5 GHz radio), enter the password.
2. **Connections → New connection** → host / user / port + password or key → *Save*.
3. Select it to open the terminal. **Trackball click** = Session menu.
4. Optional: set an Anthropic API key in **Settings** for the AI assistant. In
   Settings, trackball **right** peeks the stored secrets.

## Controls

| Context        | Trackball                                   | Keyboard                         |
|----------------|---------------------------------------------|----------------------------------|
| Menus / lists  | up/down move, click = select                | Enter = select, `` ` `` = back   |
| Connections    | right = edit                                | `e` edit, `d` delete             |
| Terminal       | arrows = cursor keys, **click = Session menu** | everything typed goes to the shell |
| Scrollback     | up/down pan history, left = back to live    | —                                |
| Editors        | click = show/hide a masked field            | type, Enter = save, Backspace = delete |

## Architecture

Single-binary firmware with a cooperative main loop in `src/main.cpp`
(`poll input → UI::handle → UI::tick → UI::render`). Highlights:

- **SSH runs in its own FreeRTOS task** (`src/ssh_session.cpp`, ~50 KB stack, core 1);
  it talks to the UI only through two mutex-guarded buffers.
- **Double-buffered immediate-mode UI** (`src/ui.cpp`) — everything draws into a
  full-screen PSRAM sprite and is pushed in one shot (no flicker).
- **Cursor-addressable terminal grid** inside `ui.cpp` (`termFeed`/`applyCsi`/
  `drawTLine`) with fg+bg color cells, UTF-8, and vector Powerline glyphs.
- Leaf modules: `input` (I2C keyboard + trackball), `touch` (GT911), `storage`
  (SD/LittleFS JSON via `fsx`), `wifi_mgr`, `keys`, `tls` (pins
  `/ca/anthropic.pem`), `ai` (Claude Messages API), `icons`, `matrix_glyphs.h`
  (baked katakana bitmaps from `assets/gen_matrix_glyphs.py`).

```
platformio.ini          build + TFT_eSPI config (no User_Setup.h edits)
include/config.h         board pins, app constants, data structs
include/theme.h          Matrix palette / layout
include/matrix_glyphs.h  baked katakana rain glyphs (generated)
src/main.cpp             boot + main loop
src/ui.{h,cpp}           screens, grid terminal, rain, WiFi picker, AI overlay
src/ssh_session.{h,cpp}  libssh shell (password + key auth, host-key verify)
src/touch.{h,cpp}        GT911 capacitive touch (+ I2C bus scan diagnostic)
src/{input,storage,fsx,wifi_mgr,keys,tls,ai,icons}.{h,cpp}
docs/                    GitHub Pages web flasher (index.html + manifest + merged bin)
```

## Known rough edges

- **Touch** (GT911) is wired but unverified on hardware — the **About** screen shows
  an I2C bus scan; if it isn't detected the pin/address may need adjusting in
  `include/config.h`.
- Terminal: no alternate-screen buffer (exiting `vim` won't restore what was behind
  it — use Session → Clear), no 256/truecolor, no insert/delete-char/line.
- Powerline glyphs render only when the **remote prompt** emits them.
- Board pins in `include/config.h` are the documented T-Deck values; verify before
  relying on them. `BOARD_POWERON` (GPIO10) must be HIGH at boot.
- Secrets (passwords, key passphrases, API key) are stored **in plaintext**. This is
  a personal handheld tool — treat the device accordingly.

## License

MIT — see `LICENSE`.
