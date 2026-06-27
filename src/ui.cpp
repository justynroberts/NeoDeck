#include "ui.h"
#include <TFT_eSPI.h>   // also makes the bundled GFXFF free fonts available
#include <functional>
#include <vector>
#include "config.h"
#include "theme.h"
#include "display.h"
#include "icons.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "ai.h"
#include "ssh_session.h"
#include "keys.h"
#include "fsx.h"
#include "touch.h"
#include "matrix_glyphs.h"

// The real panel, plus the off-screen framebuffer everything draws into. `tft`
// is a full-screen sprite (PSRAM) named to match the rest of the code, so all
// existing `tft.xxx` draw calls land in the buffer; render() pushes it to gDev.
static TFT_eSPI tft_device;
TFT_eSprite     tft(&tft_device);
static bool     fbReady = false;

// Type tiers. The Matrix aesthetic is monospaced terminal type, so the chrome
// uses the bundled FreeMono (bold) and the dense terminal/editors stay on the
// compact built-in fixed font.
static void fontTitle() { tft.setFreeFont(&FreeMonoBold12pt7b); }  // screen titles
static void fontUi()    { tft.setFreeFont(&FreeMonoBold9pt7b);  }  // labels / cards
static void fontSmall() { tft.setTextFont(1); }   // setTextFont() clears the GFX font

namespace UI {

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
enum class Screen {
  SPLASH, MENU, ENV_LIST, ENV_EDIT, TERMINAL, SESSION_MENU,
  AI_INPUT, AI_RESULT, SETTINGS, TEXT_INPUT, HOSTKEY, WIFI, ABOUT
};

static Screen   screen   = Screen::SPLASH;
static bool     dirty    = true;          // needs a full redraw
static uint32_t splashAt = 0;

// Flicker control: clearing the whole body every frame is what makes the menu
// flash while scrolling. We only wipe the body when the screen *changes* (or a
// row count shrinks); within a screen, cards repaint over themselves with no
// black flash. `fresh` is recomputed each render() from `lastDrawn`.
static Screen lastDrawn = Screen::SPLASH;
static bool   fresh     = true;
static bool   needClear = false;          // force one clear (e.g. after delete)

static void present();                    // push the off-screen frame to the panel
static void rainStep();                   // advance the Matrix rain one frame
static void rainDraw(bool dim);           // render the rain into the framebuffer

static int menuSel = 0, listSel = 0, editSel = 0, setSel = 0, sessSel = 0, hkSel = 0;
static uint32_t savedFlash = 0;   // timestamp of last settings save (for toast)

// Reveal toggles for masked secrets (peek at WiFi / API keys).
static bool setReveal = false;    // Settings screen: show real values
static bool tiReveal  = false;    // text editor: show what you're typing

// Terminal scrollback view.
static bool termScrollMode = false;
static int  termScrollOff  = 0;   // lines scrolled up from the live bottom

// WiFi screen state
static std::vector<WifiMgr::Net> wifiNets;
static int    wifiSel = 0;
static String wifiMsg;

// Connection being edited (index -1 == new)
static SshEnv editEnv;
static int    editIndex = -1;

// Terminal emulator. This is a real cursor-addressable screen: a fixed grid of
// cells (TERM_ROWS x TERM_COLS) the cursor writes into, so full-screen apps that
// use CSI cursor-positioning + erase (vim, htop, nano, top, less) render
// correctly instead of as an append-only mess. Each cell holds a char (or a
// Powerline glyph sentinel) plus a packed color byte: (bg << 4) | fg, giving
// full xterm fg+bg coloring. Lines that scroll off the top are kept in `term`
// (the scrollback) for the history viewer.
struct TLine { String t; std::vector<uint8_t> c; };
static std::vector<TLine> term;            // committed scrollback (off-screen top)

static char    gCh[TERM_ROWS][TERM_COLS];  // on-screen characters / glyph ids
static uint8_t gCol[TERM_ROWS][TERM_COLS]; // on-screen packed colors
static int     curR = 0, curC = 0;         // cursor row / column
static int     savedR = 0, savedC = 0;     // ESC 7 / CSI s save slot

static bool    termDirty = true;
static uint8_t curFg   = 7;                // current foreground palette index
static uint8_t curBg   = 0;                // current background palette index
static bool    curBold = false;

// Powerline glyphs have no font coverage (Unicode PUA), so we decode them from
// the UTF-8 stream into these sentinel bytes (< 0x20, never real text) and draw
// them as vectors. The text byte holds the id; the color cell holds fg/bg.
enum {
  GL_RSOLID = 1,  // U+E0B0  right-pointing filled separator
  GL_RTHIN  = 2,  // U+E0B1  right-pointing thin separator
  GL_LSOLID = 3,  // U+E0B2  left-pointing filled separator
  GL_LTHIN  = 4,  // U+E0B3  left-pointing thin separator
  GL_BRANCH = 5,  // U+E0A0  git branch
  GL_LINE   = 6,  // U+E0A1  line number (LN)
  GL_LOCK   = 7,  // U+E0A2  padlock (read-only)
};
static char plGlyph(uint32_t cp) {
  switch (cp) {
    case 0xE0B0: return GL_RSOLID; case 0xE0B1: return GL_RTHIN;
    case 0xE0B2: return GL_LSOLID; case 0xE0B3: return GL_LTHIN;
    case 0xE0A0: return GL_BRANCH; case 0xE0A1: return GL_LINE;
    case 0xE0A2: return GL_LOCK;   default:     return 0;
  }
}

// 16-color xterm palette in RGB565 (0-7 normal, 8-15 bright).
static const uint16_t kPal[16] = {
  0x0000, 0xF800, 0x07E0, 0xFFE0, 0x041F, 0xF81F, 0x07FF, 0xC618,
  0x8410, 0xFB4C, 0x07E6, 0xFFE0, 0x349F, 0xFC1F, 0x07FF, 0xFFFF
};

// AI overlay
static String aiQuestion, aiAnswer, aiLastCmd;
static bool   aiFromTerminal = false;
static int    aiScroll = 0;

// Generic text editor
static String tiTitle, tiBuf;
static bool   tiMask = false;             // render as dots (passwords)
static Screen tiReturn = Screen::MENU;
static std::function<void(const String&)> tiDone;

// Terminal session action menu (trackball click in the terminal).
static const char* kSess[] = {
  "Ask AI about output", "Paste last AI cmd", "Ctrl-C  interrupt",
  "Tab  complete", "Esc", "Ctrl-D  EOF",
  "Scroll history", "Clear screen", "Disconnect"
};
static const int kSessN = 9;

static const char* kMenu[] = {"Connections", "AI Assistant", "WiFi", "Settings", "About"};
static const char* kMenuSub[] = {
  "Saved SSH hosts", "Ask Claude on-device", "Networks & signal",
  "Keys, API & model", "Version & credits"
};
static const int   kMenuN  = 5;

// ---------------------------------------------------------------------------
//  Small drawing helpers
// ---------------------------------------------------------------------------
static int barsFor(int r) {
  if (r >= -55) return 4;
  if (r >= -65) return 3;
  if (r >= -75) return 2;
  if (r >= -85) return 1;
  return 0;
}
static int wifiBars() { return WifiMgr::isConnected() ? barsFor(WifiMgr::rssi()) : 0; }

static void header(const String& title) {
  // Solid dark bar with a crisp accent underline — reads cleaner than a gradient
  // and avoids two-tone text-cell backgrounds with the proportional font.
  tft.fillRect(0, 0, 320, HDR_H, C_PANEL);
  tft.fillRect(0, HDR_H - 2, 320, 2, C_ACCENT_D);

  // Title in the proportional sans, falling back to the compact font for long
  // strings (e.g. the terminal's host line) so it never overflows.
  bool small = title.length() > 20;
  if (small) fontSmall(); else fontUi();
  tft.setTextColor(C_ACCENT, C_PANEL);
  tft.drawString(title, 8, small ? 7 : 4);

  // Right: WiFi signal glyph colored by strength + IP.
  int bars = wifiBars();
  uint16_t wc = bars >= 3 ? C_ACCENT : (bars >= 1 ? C_WARN : C_MUTED);
  Icons::wifi(300, 3, bars, wc);
  if (WifiMgr::isConnected() && !small) {
    fontSmall();
    tft.setTextColor(C_MUTED, C_PANEL);
    String ip = WifiMgr::ip();
    tft.drawString(ip, 294 - tft.textWidth(ip), 8);
  }
}

static void clearBody() { tft.fillRect(0, HDR_H, 320, 240 - HDR_H, C_BG); }

// Fill a rounded rect by blending `color` over whatever is already in the
// framebuffer (the rain) at `alpha`/255 — so cards look ~50% translucent and
// the katakana shows through, faded. Reads/writes the PSRAM sprite per pixel.
static void alphaRoundRect(int x, int y, int w, int h, int r, uint16_t color, uint8_t alpha) {
  int x0 = x + r, x1 = x + w - 1 - r, y0 = y + r, y1 = y + h - 1 - r;
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) {
      int cx = -1, cy = -1;                         // nearest corner centre, if any
      if      (xx < x0 && yy < y0) { cx = x0; cy = y0; }
      else if (xx > x1 && yy < y0) { cx = x1; cy = y0; }
      else if (xx < x0 && yy > y1) { cx = x0; cy = y1; }
      else if (xx > x1 && yy > y1) { cx = x1; cy = y1; }
      if (cx >= 0) { int dx = xx - cx, dy = yy - cy; if (dx * dx + dy * dy > r * r) continue; }
      uint16_t bg = tft.readPixel(xx, yy);
      tft.drawPixel(xx, yy, tft.alphaBlend(alpha, color, bg));
    }
  }
}

// Arduino String has no repeat-char ctor; build a run of '*' for masked fields.
static String dots(int n) { String s; while (n-- > 0) s += '*'; return s; }

// Wrap a long string to `cols` columns.
static std::vector<String> wrap(const String& s, int cols) {
  std::vector<String> out;
  String line;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\n') { out.push_back(line); line = ""; continue; }
    line += c;
    if ((int)line.length() >= cols) { out.push_back(line); line = ""; }
  }
  out.push_back(line);
  return out;
}

// Colorful main menu: each row is a rounded card with an accent strip, a
// web-style icon, and an elite-font label.
static void drawMainMenu() {
  tft.fillSprite(C_BG);
  rainStep();
  rainDraw(true);                       // dim katakana rain behind the menu
  header("NeoDeck");
  static const uint16_t col[] = {C_ACCENT, C_CYAN, C_BLUE, C_PURPLE, C_ACCENT_D};
  const int y0 = HDR_H + 6, rh = 37, ch = rh - 5;
  for (int i = 0; i < kMenuN; i++) {
    int y = y0 + i * rh;
    bool s = (i == menuSel);
    // ~50% translucent card over the rain; selected rows are a touch stronger.
    alphaRoundRect(8, y, 304, ch, 7, s ? C_PANEL2 : C_PANEL, s ? 165 : 120);
    tft.drawRoundRect(8, y, 304, ch, 7, s ? C_ACCENT : C_ACCENT_D);   // crisp edge
    tft.fillRect(11, y + 5, 4, ch - 10, col[i]);       // floating accent bar
    int iy = y + (ch - 16) / 2;
    switch (i) {
      case 0: Icons::server(26, iy, col[i]); break;
      case 1: Icons::robot(26, iy, col[i]); break;
      case 2: Icons::wifi(26, iy, wifiBars(), col[i]); break;
      case 3: Icons::gear(26, iy, col[i]); break;
      case 4: Icons::info(26, iy, col[i]); break;
    }
    // Transparent text background so the faded rain still shows around glyphs.
    fontUi();
    tft.setTextColor(s ? C_TEXT : C_MUTED);
    tft.drawString(kMenu[i], 50, y + 5);
    fontSmall();
    tft.setTextColor(s ? C_ACCENT : C_MUTED);
    tft.drawString(kMenuSub[i], 50, y + 21);
    if (s) Icons::chevron(292, iy, col[i]);
  }
}

// ---------------------------------------------------------------------------
//  Terminal emulator
// ---------------------------------------------------------------------------
// Pack the current attributes into a cell byte: (bg << 4) | fg.
static uint8_t effCell() {
  uint8_t fg = curFg;
  if (curBold && fg < 8) fg += 8;     // bold promotes normal colors to bright
  return (uint8_t)(((curBg & 0x0F) << 4) | (fg & 0x0F));
}

static void clearCell(int r, int c) { gCh[r][c] = ' '; gCol[r][c] = effCell(); }

static void clearRowRange(int r, int from, int to) {
  for (int c = from; c <= to && c < TERM_COLS; c++) clearCell(r, c);
}

static void termReset() {
  term.clear();
  curR = curC = savedR = savedC = 0;
  curFg = 7; curBg = 0; curBold = false;
  for (int r = 0; r < TERM_ROWS; r++) clearRowRange(r, 0, TERM_COLS - 1);
  termDirty = true;
}

// Copy a grid row into a compact TLine (trailing blanks trimmed) for scrollback.
static TLine rowToLine(int r) {
  TLine ln;
  int last = -1;
  for (int c = TERM_COLS - 1; c >= 0; c--)
    if (gCh[r][c] != ' ' || (gCol[r][c] >> 4) != 0) { last = c; break; }
  for (int c = 0; c <= last; c++) { ln.t += gCh[r][c]; ln.c.push_back(gCol[r][c]); }
  return ln;
}

// Scroll the whole screen up one line; the top row drops into scrollback.
static void scrollUp() {
  term.push_back(rowToLine(0));
  if ((int)term.size() > MAX_SCROLLBACK) term.erase(term.begin());
  for (int r = 1; r < TERM_ROWS; r++)
    for (int c = 0; c < TERM_COLS; c++) { gCh[r - 1][c] = gCh[r][c]; gCol[r - 1][c] = gCol[r][c]; }
  clearRowRange(TERM_ROWS - 1, 0, TERM_COLS - 1);
}

static void newLine() { if (++curR >= TERM_ROWS) { scrollUp(); curR = TERM_ROWS - 1; } }

static void putCell(char c) {
  if (curC >= TERM_COLS) { curC = 0; newLine(); }   // wrap at right margin
  gCh[curR][curC] = c; gCol[curR][curC] = effCell(); curC++;
}
static void putChar(char c)  { putCell(c); }
static void putGlyph(char id){ putCell(id); }

// Apply an SGR ("ESC[ ... m") sequence: foreground, background and bold.
static void applySgr(const String& params) {
  int i = 0, n = params.length();
  if (n == 0) { curFg = 7; curBg = 0; curBold = false; return; }  // ESC[m == reset
  while (i <= n) {
    int semi = params.indexOf(';', i);
    if (semi < 0) semi = n;
    int code = params.substring(i, semi).toInt();
    if      (code == 0)                  { curFg = 7; curBg = 0; curBold = false; }
    else if (code == 1)                  { curBold = true; }
    else if (code == 22)                 { curBold = false; }
    else if (code >= 30 && code <= 37)   { curFg = code - 30; }
    else if (code == 39)                 { curFg = 7; }
    else if (code >= 40 && code <= 47)   { curBg = code - 40; }
    else if (code == 49)                 { curBg = 0; }
    else if (code >= 90 && code <= 97)   { curFg = (code - 90) + 8; }
    else if (code >= 100 && code <= 107) { curBg = (code - 100) + 8; }
    // 256-color / truecolor (38/48;...) and others are ignored
    i = semi + 1;
  }
}

static void clampCursor() {
  curR = constrain(curR, 0, TERM_ROWS - 1);
  curC = constrain(curC, 0, TERM_COLS - 1);
}

static void eraseDisplay(int mode) {           // CSI J
  if (mode == 0) {                              // cursor -> end of screen
    clearRowRange(curR, curC, TERM_COLS - 1);
    for (int r = curR + 1; r < TERM_ROWS; r++) clearRowRange(r, 0, TERM_COLS - 1);
  } else if (mode == 1) {                        // start -> cursor
    for (int r = 0; r < curR; r++) clearRowRange(r, 0, TERM_COLS - 1);
    clearRowRange(curR, 0, curC);
  } else {                                       // whole screen
    for (int r = 0; r < TERM_ROWS; r++) clearRowRange(r, 0, TERM_COLS - 1);
  }
}

static void eraseLine(int mode) {              // CSI K
  if (mode == 0)      clearRowRange(curR, curC, TERM_COLS - 1);
  else if (mode == 1) clearRowRange(curR, 0, curC);
  else                clearRowRange(curR, 0, TERM_COLS - 1);
}

// Apply a CSI ("ESC[ ... <final>") sequence. `p` is the parameter string.
static void applyCsi(char fb, const String& p) {
  if (fb == 'm') { applySgr(p); return; }
  if (p.startsWith("?")) {                 // private modes (?25h cursor, ?1049h alt...)
    return;                                 // consumed but not acted on
  }
  int a = -1, b = -1, semi = p.indexOf(';');
  if (semi < 0) { if (p.length()) a = p.toInt(); }
  else          { a = p.substring(0, semi).toInt(); b = p.substring(semi + 1).toInt(); }
  int n = (a < 1) ? 1 : a;
  switch (fb) {
    case 'H': case 'f': curR = (a < 1 ? 1 : a) - 1; curC = (b < 1 ? 1 : b) - 1; clampCursor(); break;
    case 'A': curR -= n; clampCursor(); break;
    case 'B': curR += n; clampCursor(); break;
    case 'C': curC += n; clampCursor(); break;
    case 'D': curC -= n; clampCursor(); break;
    case 'E': curR += n; curC = 0; clampCursor(); break;
    case 'F': curR -= n; curC = 0; clampCursor(); break;
    case 'G': case '`': curC = (a < 1 ? 1 : a) - 1; clampCursor(); break;
    case 'd': curR = (a < 1 ? 1 : a) - 1; clampCursor(); break;
    case 'J': eraseDisplay(a < 0 ? 0 : a); break;
    case 'K': eraseLine(a < 0 ? 0 : a); break;
    case 's': savedR = curR; savedC = curC; break;
    case 'u': curR = savedR; curC = savedC; clampCursor(); break;
    default: break;                         // P/@/L/M/r etc. ignored for now
  }
}

// Append raw server bytes: a small VT100/xterm interpreter feeding the grid.
static void termFeed(const String& data) {
  static int esc = 0;        // 0 normal, 1 ESC, 2 CSI, 3 OSC, 4 skip-one, 5 OSC-ST
  static String csi;
  static int      u8need = 0;
  static uint32_t u8cp   = 0;
  for (size_t i = 0; i < data.length(); i++) {
    uint8_t c = (uint8_t)data[i];

    if (esc == 1) {                          // just saw ESC
      switch (c) {
        case '[': esc = 2; csi = ""; break;
        case ']': esc = 3; break;            // OSC (title etc.) — consume to BEL/ST
        case '(': case ')': case '*': case '+': esc = 4; break;  // charset: skip next
        case '7': savedR = curR; savedC = curC; esc = 0; break;
        case '8': curR = savedR; curC = savedC; clampCursor(); esc = 0; break;
        case 'M': if (curR > 0) curR--; esc = 0; break;          // reverse index
        default:  esc = 0; break;            // ESC = > etc.
      }
      continue;
    }
    if (esc == 2) {                          // inside CSI
      if (c >= 0x40 && c <= 0x7E) { applyCsi((char)c, csi); esc = 0; }
      else csi += (char)c;
      continue;
    }
    if (esc == 3) {                          // OSC: swallow until BEL or ST
      if (c == 0x07) esc = 0;
      else if (c == 0x1B) esc = 5;
      continue;
    }
    if (esc == 4) { esc = 0; continue; }     // charset designator byte
    if (esc == 5) { esc = 0; continue; }     // backslash of an ST terminator

    // UTF-8 assembly (Powerline glyphs and other multibyte sequences).
    if (u8need > 0) {
      if ((c & 0xC0) == 0x80) {
        u8cp = (u8cp << 6) | (c & 0x3F);
        if (--u8need == 0) { char g = plGlyph(u8cp); if (g) putGlyph(g); }
        continue;
      }
      u8need = 0;
    }
    if (c >= 0xC0) {
      if      ((c & 0xE0) == 0xC0) { u8cp = c & 0x1F; u8need = 1; }
      else if ((c & 0xF0) == 0xE0) { u8cp = c & 0x0F; u8need = 2; }
      else if ((c & 0xF8) == 0xF0) { u8cp = c & 0x07; u8need = 3; }
      else                         { u8need = 0; }
      continue;
    }

    switch (c) {
      case 0x1B: esc = 1; break;
      case '\n': newLine(); break;
      case '\r': curC = 0; break;
      case '\b': if (curC > 0) curC--; break;
      case '\t': curC = min(TERM_COLS - 1, (curC / 8 + 1) * 8); break;
      case 0x07: break;                       // bell — ignored
      default:   if (c >= 32 && c < 0x80) putChar((char)c);
    }
  }
  termDirty = true;
}

// Draw a single Powerline glyph in a 6x9 cell at (x,y).
static void drawGlyph(char id, int x, int y, uint16_t fg) {
  switch (id) {
    case GL_RSOLID: tft.fillTriangle(x, y, x, y + 8, x + 6, y + 4, fg); break;
    case GL_LSOLID: tft.fillTriangle(x + 6, y, x + 6, y + 8, x, y + 4, fg); break;
    case GL_RTHIN:  tft.drawLine(x, y, x + 6, y + 4, fg);
                    tft.drawLine(x + 6, y + 4, x, y + 8, fg); break;
    case GL_LTHIN:  tft.drawLine(x + 6, y, x, y + 4, fg);
                    tft.drawLine(x, y + 4, x + 6, y + 8, fg); break;
    case GL_BRANCH: tft.drawCircle(x + 2, y + 2, 1, fg);
                    tft.drawFastVLine(x + 2, y + 3, 4, fg);
                    tft.drawLine(x + 2, y + 5, x + 5, y + 2, fg);
                    tft.fillCircle(x + 5, y + 2, 1, fg); break;
    case GL_LOCK:   tft.drawRoundRect(x + 1, y + 1, 4, 4, 1, fg);
                    tft.fillRect(x, y + 4, 6, 4, fg); break;
    case GL_LINE:   tft.setTextColor(fg); tft.drawString("L", x, y); break;
  }
}

// Draw one line: runs of identical fg/bg drawn together; Powerline glyph cells
// drawn individually. Each cell is 6 wide, 9 tall (full fg+bg coloring).
static void drawTLine(const TLine& ln, int y) {
  int x = 4, i = 0, len = ln.t.length();
  while (i < len) {
    uint8_t packed = ln.c[i];
    uint16_t fg = kPal[packed & 0x0F];
    uint16_t bg = kPal[(packed >> 4) & 0x0F];
    if ((uint8_t)ln.t[i] < 0x20) {            // Powerline glyph cell
      tft.fillRect(x, y, 6, 9, bg);
      drawGlyph(ln.t[i], x, y, fg);
      x += 6; i++;
      continue;
    }
    String run;
    while (i < len && ln.c[i] == packed && (uint8_t)ln.t[i] >= 0x20) { run += ln.t[i]; i++; }
    tft.fillRect(x, y, 6 * run.length(), 9, bg);
    tft.setTextColor(fg, bg);
    tft.drawString(run, x, y);
    x += 6 * run.length();
  }
}

// In scroll mode we view scrollback + the live grid as one tall list of lines,
// so the deepest scroll is exactly the number of off-screen scrollback lines.
static int termMaxScroll() { return (int)term.size(); }

static void termDraw() {
  // Header shows the host, and flips to a SCROLL banner in scrollback mode.
  if (termScrollMode) header(String("scroll ") + editEnv.user + "@" + editEnv.host);
  else                header(String("ssh ")    + editEnv.user + "@" + editEnv.host);
  tft.fillRect(0, HDR_H, 320, 240 - HDR_H, C_BG);

  // Until the shell is live, show the connection status / error as a clear,
  // wrapped, full-width message (long libssh errors stay readable).
  SshSession::State st = gSsh.state();
  if (st != SshSession::State::CONNECTED) {
    uint16_t c = (st == SshSession::State::ERROR)  ? C_ERR
               : (st == SshSession::State::CLOSED) ? C_WARN : C_CYAN;
    fontSmall();
    tft.setTextColor(c, C_BG);
    int y = HDR_H + 22;
    for (auto& l : wrap(gSsh.status(), 50)) { tft.drawString(l, 8, y); y += 11; }
    tft.setTextColor(C_MUTED, C_BG);
    bool done = (st == SshSession::State::ERROR || st == SshSession::State::CLOSED);
    tft.drawString(done ? "click = menu    left = back" : "connecting, please wait…", 8, y + 10);
    termDirty = false;
    return;
  }

  tft.setTextFont(1);
  if (termScrollMode) {
    // Combined view: [scrollback ... live grid]. Window of TERM_ROWS lines.
    int sb = term.size();
    int total = sb + TERM_ROWS;
    int start = max(0, total - TERM_ROWS - termScrollOff);
    int y = HDR_H + 2;
    for (int i = start; i < total && i < start + TERM_ROWS; i++) {
      if (i < sb) drawTLine(term[i], y);
      else        drawTLine(rowToLine(i - sb), y);
      y += 9;
    }
  } else {
    // Live: draw the on-screen grid rows, then the blinking block cursor.
    int y = HDR_H + 2;
    for (int r = 0; r < TERM_ROWS; r++) { drawTLine(rowToLine(r), y); y += 9; }
    if ((millis() / 500) % 2 == 0) {
      int cx = 4 + curC * 6, cy = HDR_H + 2 + curR * 9;
      tft.fillRect(cx, cy, 6, 8, C_ACCENT);
    }
  }

  // Scrollback hint footer.
  if (termScrollMode) {
    fontSmall();
    tft.fillRect(0, 231, 320, 9, C_PANEL);
    tft.setTextColor(C_WARN, C_PANEL);
    tft.drawString(" -- SCROLL --  up/down move   left = back to live", 4, 232);
  }
  termDirty = false;
}

// Map trackball directions to ANSI cursor sequences for the shell.
static void termSendArrow(EvType t) {
  switch (t) {
    case EvType::UP:    gSsh.send("\x1b[A"); break;
    case EvType::DOWN:  gSsh.send("\x1b[B"); break;
    case EvType::RIGHT: gSsh.send("\x1b[C"); break;
    case EvType::LEFT:  gSsh.send("\x1b[D"); break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
//  Text editor
// ---------------------------------------------------------------------------
static void startEdit(const String& title, const String& initial, bool mask,
                      Screen ret, std::function<void(const String&)> done) {
  tiTitle = title; tiBuf = initial; tiMask = mask; tiReveal = false;
  tiReturn = ret; tiDone = done;
  screen = Screen::TEXT_INPUT; dirty = true;
}

static void drawTextInput() {
  header("Edit: " + tiTitle);
  clearBody();
  tft.setTextFont(1);
  bool masked = tiMask && !tiReveal;
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString(tiMask ? "type  Enter=save  Bksp=del  click=show/hide"
                        : "type, Enter=save, Backspace=del", 8, HDR_H + 6);

  // Value box — wraps so a long API key is fully visible when revealed.
  String shown = masked ? dots(tiBuf.length()) : tiBuf;
  tft.fillRect(8, HDR_H + 22, 304, 86, C_PANEL);
  tft.setTextColor(C_ACCENT, C_PANEL);
  int yy = HDR_H + 28, n = 0;
  for (auto& l : wrap(shown + "_", 50)) { if (n++ >= 7) break; tft.drawString(l, 12, yy); yy += 11; }

  if (tiMask) {
    tft.setTextColor(tiReveal ? C_WARN : C_MUTED, C_BG);
    tft.drawString(tiReveal ? "revealed" : "hidden", 8, HDR_H + 114);
  }
}

// ---------------------------------------------------------------------------
//  Screen renderers
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  Matrix "digital rain" — tumbling green katakana (glyphs from matrix_glyphs.h)
// ---------------------------------------------------------------------------
#define RAIN_COLS 40          // 320 / 8 px
#define RAIN_ROWS 27          // ~240 / 9 px

static int     rainHead[RAIN_COLS];          // head row per column (can be < 0)
static uint8_t rainCell[RAIN_COLS][RAIN_ROWS];// glyph index per cell
static bool    rainInit = false;

static void rainSetup() {
  for (int c = 0; c < RAIN_COLS; c++) {
    rainHead[c] = random(-RAIN_ROWS, 0);
    for (int r = 0; r < RAIN_ROWS; r++) rainCell[c][r] = random(MTX_GLYPH_N);
  }
  rainInit = true;
}

// Advance the streams one step and mutate a few glyphs so they shimmer.
static void rainStep() {
  for (int c = 0; c < RAIN_COLS; c++) {
    if (++rainHead[c] - 14 > RAIN_ROWS) rainHead[c] = random(-RAIN_ROWS, -2);
    if (rainHead[c] >= 0 && rainHead[c] < RAIN_ROWS)
      rainCell[c][rainHead[c]] = random(MTX_GLYPH_N);
  }
  for (int n = 0; n < 12; n++)               // random shimmer
    rainCell[random(RAIN_COLS)][random(RAIN_ROWS)] = random(MTX_GLYPH_N);
}

// Render rain into the framebuffer. `dim` is the menu background (low key);
// bright is the boot screen. Assumes the caller fills the background first.
static void rainDraw(bool dim) {
  if (!rainInit) rainSetup();
  for (int c = 0; c < RAIN_COLS; c++) {
    int x = c * 8;
    for (int k = 0; k < 14; k++) {           // fading tail behind each head
      int r = rainHead[c] - k;
      if (r < 0 || r >= RAIN_ROWS) continue;
      uint16_t col;
      if (dim) col = (k == 0) ? 0x0560 : (k < 5) ? 0x0400 : (k < 9) ? 0x0200 : 0x0140;
      else     col = (k == 0) ? TFT_WHITE : (k < 3) ? 0x07E0 : (k < 6) ? 0x0560
                   : (k < 10) ? 0x0400 : 0x0200;
      tft.drawBitmap(x, r * 9, mtxGlyphs[rainCell[c][r]], 8, 8, col);
    }
  }
}

static void drawSplash() {
  tft.fillSprite(C_BG);
  rainStep();
  rainDraw(false);

  // Title plate over the rain.
  int bw = 196, bh = 58, bx = (320 - bw) / 2, by = 88;
  tft.fillRect(bx, by, bw, bh, C_BG);
  tft.drawRect(bx, by, bw, bh, C_ACCENT);
  tft.drawRect(bx + 2, by + 2, bw - 4, bh - 4, C_ACCENT_D);
  fontTitle();
  tft.setTextColor(C_ACCENT, C_BG);
  String t = "NeoDeck";
  tft.drawString(t, (320 - tft.textWidth(t)) / 2, by + 12);
  fontSmall();
  tft.setTextColor(C_MUTED, C_BG);
  String s = "v" APP_VERSION "   wake up...";
  tft.drawString(s, (320 - tft.textWidth(s)) / 2, by + 42);
}

static void drawEnvList() {
  header("Connections");
  if (fresh) clearBody();
  int n = gConfig.envs.size();
  const int y0 = HDR_H + 6, rh = 30, ch = rh - 5;
  for (int i = 0; i < n; i++) {
    int y = y0 + i * rh;
    bool s = (i == listSel);
    tft.fillRoundRect(8, y, 304, ch, 5, s ? C_PANEL2 : C_PANEL);
    SshEnv& e = gConfig.envs[i];
    uint16_t ic = (e.auth == 1) ? C_CYAN : C_ACCENT;     // key vs password
    Icons::server(16, y + (ch - 16) / 2, ic);
    if (e.auth == 1) Icons::key(296, y + (ch - 16) / 2, C_CYAN);
    fontUi();
    tft.setTextColor(s ? C_TEXT : C_MUTED, s ? C_PANEL2 : C_PANEL);
    tft.drawString(e.name, 42, y + 3);
    fontSmall();
    tft.setTextColor(C_MUTED, s ? C_PANEL2 : C_PANEL);
    tft.drawString(e.user + "@" + e.host + ":" + String(e.port), 42, y + 17);
  }
  // "+ New" row
  int y = y0 + n * rh;
  bool s = (listSel == n);
  tft.fillRoundRect(8, y, 304, ch, 5, s ? C_PANEL2 : C_PANEL);
  Icons::plus(16, y + (ch - 16) / 2, C_ACCENT);
  fontUi();
  tft.setTextColor(s ? C_ACCENT : C_MUTED, s ? C_PANEL2 : C_PANEL);
  tft.drawString("New connection", 42, y + (ch - 13) / 2);

  fontSmall();
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("click=connect  right=edit  d=del  left=back", 8, 228);
}

#define ENV_FIELDS 9   // name,host,port,user,auth,pass,key,save,delete

static String editRow(int i) {
  switch (i) {
    case 0: return "Name: " + editEnv.name;
    case 1: return "Host: " + editEnv.host;
    case 2: return "Port: " + String(editEnv.port);
    case 3: return "User: " + editEnv.user;
    case 4: return String("Auth: ") + (editEnv.auth ? "Public key" : "Password");
    case 5: return "Pass: " + dots(editEnv.pass.length());
    case 6: return "Key:  " + (editEnv.keyName.length() ? editEnv.keyName : String("(none)"));
    case 7: return "[ Save ]";
    case 8: return editIndex >= 0 ? "[ Delete ]" : "";
  }
  return "";
}

static void drawEnvEdit() {
  header(editIndex >= 0 ? "Edit connection" : "New connection");
  clearBody();
  tft.setTextFont(1);
  for (int i = 0; i < ENV_FIELDS; i++) {
    String r = editRow(i);
    if (r.isEmpty()) continue;
    int y = HDR_H + 2 + i * ROW_H;
    bool s = (i == editSel);
    tft.fillRect(0, y, 320, ROW_H - 2, s ? C_PANEL2 : C_BG);
    if (s) tft.fillRect(0, y, 3, ROW_H - 2, C_ACCENT);
    // Dim the auth field that isn't in use.
    bool inactive = (i == 5 && editEnv.auth == 1) || (i == 6 && editEnv.auth == 0);
    uint16_t fg = (i == 8) ? C_ERR : (i == 7 ? C_ACCENT : (inactive ? C_MUTED : C_TEXT));
    tft.setTextColor(s ? C_ACCENT : fg, s ? C_PANEL2 : C_BG);
    tft.drawString(r, 10, y + 6);
  }
}

static void drawSettings() {
  header("Settings");
  if (fresh) clearBody();
  String rows[] = {
    "WiFi SSID: " + gConfig.wifiSsid,
    "WiFi Pass: " + (setReveal ? gConfig.wifiPass : dots(gConfig.wifiPass.length())),
    "API Key: "   + (setReveal ? gConfig.anthropicKey.substring(0, 30)
                               : dots(min((int)gConfig.anthropicKey.length(), 12))),
    "AI Model: "  + gConfig.aiModel,
    "Save & reconnect WiFi",
  };
  static const uint16_t ic[] = {C_BLUE, C_BLUE, C_PURPLE, C_CYAN, C_ACCENT};
  const int y0 = HDR_H + 6, rh = 30, ch = rh - 5;
  for (int i = 0; i < 5; i++) {
    int y = y0 + i * rh;
    bool s = (i == setSel);
    tft.fillRoundRect(8, y, 304, ch, 5, s ? C_PANEL2 : C_PANEL);
    tft.fillRect(8, y, 4, ch, ic[i]);
    int iy = y + (ch - 16) / 2;
    if (i == 0 || i == 1)      Icons::wifi(18, iy, 4, ic[i]);
    else if (i == 2)           Icons::lock(18, iy, ic[i]);
    else if (i == 3)           Icons::robot(18, iy, ic[i]);
    else                       Icons::gear(18, iy, ic[i]);
    fontSmall();
    tft.setTextColor(s ? C_TEXT : C_MUTED, s ? C_PANEL2 : C_PANEL);
    tft.drawString(rows[i], 44, y + (ch - 8) / 2);
  }

  // Peek panel: full, wrapped value of the selected field when revealing.
  if (setReveal && setSel <= 3) {
    String val;
    switch (setSel) {
      case 0: val = gConfig.wifiSsid;     break;
      case 1: val = gConfig.wifiPass;     break;
      case 2: val = gConfig.anthropicKey; break;
      case 3: val = gConfig.aiModel;      break;
    }
    int py = HDR_H + 6 + 5 * 30 - 2;
    tft.fillRect(6, py, 308, 44, C_PANEL);
    tft.drawRect(6, py, 308, 44, C_WARN);
    fontSmall();
    tft.setTextColor(C_WARN, C_PANEL);
    tft.drawString("peek:", 10, py + 3);
    tft.setTextColor(C_TEXT, C_PANEL);
    int yy = py + 14, shown = 0;
    for (auto& l : wrap(val.length() ? val : String("(empty)"), 50)) {
      if (shown++ >= 3) break;
      tft.drawString(l, 10, yy); yy += 10;
    }
  }

  // Footer: peek hint + storage + transient "saved" toast.
  fontSmall();
  tft.setTextColor(setReveal ? C_WARN : C_MUTED, C_BG);
  tft.drawString(setReveal ? "right = hide" : "right = peek secrets", 8, 230);
  tft.setTextColor(C_MUTED, C_BG);
  String where = fsx::usingSD() ? "SD" : "flash";
  tft.drawString(where, 312 - tft.textWidth(where), 230);
  if (millis() - savedFlash < 1500) {
    tft.fillRoundRect(200, 224, 70, 14, 4, C_ACCENT_D);
    tft.setTextColor(C_BG, C_ACCENT_D);
    tft.drawString("saved OK", 208, 227);
  }
}

static void drawSessionMenu() {
  termDraw();  // keep terminal visible behind
  const int px = 34, py = 26, pw = 252, ph = 206;
  tft.fillRect(px, py, pw, ph, C_PANEL);
  tft.drawRect(px, py, pw, ph, C_ACCENT);
  fontSmall();
  tft.setTextColor(C_ACCENT, C_PANEL);
  tft.drawString("SESSION", px + 10, py + 6);
  for (int i = 0; i < kSessN; i++) {
    bool s = (i == sessSel);
    int y = py + 20 + i * 20;
    tft.fillRect(px + 4, y, pw - 8, 18, s ? C_PANEL2 : C_PANEL);
    if (s) tft.fillRect(px + 4, y, 3, 18, C_ACCENT);
    tft.setTextColor(s ? C_ACCENT : C_TEXT, s ? C_PANEL2 : C_PANEL);
    tft.drawString(kSess[i], px + 12, y + 5);
  }
}

static void drawAiInput() {
  header("AI Assistant");
  clearBody();
  tft.setTextFont(1);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString(AI::configured() ? "Ask anything. Enter to send."
                                  : "Set an API key in Settings first.", 8, HDR_H + 6);
  tft.fillRect(8, HDR_H + 24, 304, 22, C_PANEL);
  tft.setTextColor(C_ACCENT, C_PANEL);
  tft.drawString(aiQuestion + "_", 12, HDR_H + 30);
}

static void drawAiResult() {
  header("Claude");
  clearBody();
  tft.setTextFont(1);
  tft.setTextColor(C_TEXT, C_BG);
  auto lines = wrap(aiAnswer, 52);
  int rows = (240 - HDR_H - 16) / 9;
  for (int i = 0; i + aiScroll < (int)lines.size() && i < rows; i++) {
    tft.drawString(lines[i + aiScroll], 6, HDR_H + 4 + i * 9);
  }
  tft.setTextColor(C_MUTED, C_BG);
  String hint = aiFromTerminal ? "click=run first line  left=back  up/dn=scroll"
                               : "left=back  up/dn=scroll";
  tft.drawString(hint, 6, 228);
}

static void drawHostKey() {
  bool changed = gSsh.hostKeyChanged();
  header(changed ? "! HOST KEY CHANGED" : "Verify host key");
  clearBody();
  tft.setTextFont(1);

  tft.setTextColor(changed ? C_ERR : C_MUTED, C_BG);
  tft.drawString(changed ? "WARNING: key differs from known_hosts!"
                         : "First connection to this host.", 8, HDR_H + 6);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(editEnv.user + "@" + editEnv.host + ":" + String(editEnv.port), 8, HDR_H + 20);
  tft.drawString("SHA256 fingerprint:", 8, HDR_H + 36);
  tft.setTextColor(C_ACCENT, C_BG);
  // fingerprint can be long — wrap it
  auto fl = wrap(gSsh.fingerprint(), 52);
  int y = HDR_H + 48;
  for (auto& l : fl) { tft.drawString(l, 8, y); y += 9; }

  const char* opts[] = {"Accept & save", "Reject"};
  for (int i = 0; i < 2; i++) {
    int yy = 200 + i * 0;  // place side by side
    int xx = 8 + i * 160;
    bool s = (i == hkSel);
    tft.fillRect(xx, 200, 150, 22, s ? C_PANEL2 : C_PANEL);
    tft.setTextColor(s ? C_ACCENT : (i == 1 ? C_ERR : C_TEXT), s ? C_PANEL2 : C_PANEL);
    tft.drawString(opts[i], xx + 10, 206);
    (void)yy;
  }
}

static void startWifiScan() {
  header("WiFi");
  clearBody();
  fontUi();
  tft.setTextColor(C_CYAN, C_BG);
  tft.drawString("Scanning…", 12, HDR_H + 40);
  present();                          // show it before the blocking scan
  wifiNets = WifiMgr::scan();
  wifiSel = 0;
  dirty = true;
}

static void drawWifi() {
  header("WiFi");
  int top = HDR_H + 16, rh = 26, ch = rh - 4;
  int rows = (226 - top) / rh;
  int n = wifiNets.size();
  int off = max(0, wifiSel - (rows - 1));
  // Wipe only when entering the screen or when the scroll window shifts; within
  // the window rows repaint over themselves so scrolling doesn't flicker.
  static int lastOff = -1;
  if (fresh || off != lastOff) clearBody();
  lastOff = off;

  // Current connection status line (own thin strip so it can shrink cleanly).
  tft.fillRect(0, HDR_H + 1, 320, 13, C_BG);
  fontSmall();
  if (WifiMgr::isConnected()) {
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString("connected: " + WifiMgr::ssid() + "  " + WifiMgr::ip(), 8, HDR_H + 3);
  } else {
    tft.setTextColor(wifiMsg.startsWith("Failed") ? C_ERR : C_MUTED, C_BG);
    tft.drawString(wifiMsg.length() ? wifiMsg : "not connected", 8, HDR_H + 3);
  }

  for (int i = 0; i < rows && off + i < n; i++) {
    int idx = off + i;
    WifiMgr::Net& net = wifiNets[idx];
    int y = top + i * rh;
    bool s = (idx == wifiSel);
    tft.fillRoundRect(8, y, 304, ch, 5, s ? C_PANEL2 : C_PANEL);
    Icons::wifi(16, y + (ch - 16) / 2, barsFor(net.rssi), s ? C_ACCENT : C_CYAN);
    if (net.secure) Icons::lock(292, y + (ch - 16) / 2, C_MUTED);
    fontUi();
    tft.setTextColor(s ? C_TEXT : C_MUTED, s ? C_PANEL2 : C_PANEL);
    tft.drawString(net.ssid, 40, y + (ch - 13) / 2);
  }
  fontSmall();
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("click=join  r=rescan  left=back", 8, 230);
}

static void wifiConnectNow() {
  // Immediate feedback before the blocking connect.
  header("WiFi"); clearBody();
  fontUi(); tft.setTextColor(C_CYAN, C_BG);
  tft.drawString("Connecting to", 12, HDR_H + 30);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(gConfig.wifiSsid + "…", 12, HDR_H + 50);
  present();                          // show it before the blocking connect

  bool ok = WifiMgr::connect(12000);
  wifiMsg = ok ? "Connected" : ("Failed: " + WifiMgr::lastError());
  dirty = true;
}

static void drawAbout() {
  header("About");
  if (fresh) clearBody();

  fontTitle();
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("NeoDeck", 14, HDR_H + 10);
  fontSmall();
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("v" APP_VERSION "  ·  LilyGo T-Deck Plus", 14, HDR_H + 36);

  struct { const char* k; const char* v; uint16_t c; } rows[] = {
    {"SSH",    "libssh · password + key auth", C_ACCENT},
    {"Crypto", "host-key verify · pinned TLS", C_CYAN},
    {"AI",     "Claude on-device assistant",   C_PURPLE},
    {"Input",  "keyboard · trackball · touch", C_WARN},
  };
  int y = HDR_H + 56;
  for (auto& r : rows) {
    tft.fillRect(14, y + 4, 4, 12, r.c);
    fontUi();  tft.setTextColor(C_TEXT, C_BG); tft.drawString(r.k, 26, y);
    fontSmall(); tft.setTextColor(C_MUTED, C_BG); tft.drawString(r.v, 92, y + 3);
    y += 22;
  }
  fontSmall();
  tft.setTextColor(Touch::present() ? C_ACCENT : C_WARN, C_BG);
  tft.drawString(Touch::present() ? "touch: ready" : "touch: not detected", 14, 214);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("I2C: " + Touch::report(), 14, 228);
}

// ---------------------------------------------------------------------------
//  Public: begin / render
// ---------------------------------------------------------------------------
void begin() {
  tft_device.init();
  tft_device.setRotation(1);          // landscape 320x240
  tft_device.fillScreen(C_BG);

  // Allocate the off-screen framebuffer in PSRAM (150 KB). With 8 MB PSRAM this
  // does not fail in practice; if it ever did, the sprite has no backing buffer
  // and draws become no-ops, so surface that loudly instead of a blank screen.
  tft.setColorDepth(16);
  fbReady = (tft.createSprite(320, 240) != nullptr);
  if (!fbReady) {
    Serial.println("FB: sprite alloc FAILED");
    tft_device.fillScreen(TFT_RED);
    tft_device.setTextColor(TFT_WHITE, TFT_RED);
    tft_device.drawString("Framebuffer alloc failed", 10, 110, 2);
  }
  tft.setTextDatum(TL_DATUM);          // top-left origin for all drawString()
  tft.fillScreen(C_BG);

  screen = Screen::SPLASH;
  splashAt = millis();
  dirty = true;
}

// Push the finished off-screen frame to the panel in one shot.
static void present() { if (fbReady) tft.pushSprite(0, 0); }

void render() {
  if (!dirty && screen != Screen::TERMINAL) return;
  // A frame is "fresh" (warrants a full body wipe) only when the screen changed
  // or something explicitly asked for a clear; otherwise we repaint in place.
  fresh = (screen != lastDrawn) || needClear;
  needClear = false;
  lastDrawn = screen;
  bool drew = (screen == Screen::TERMINAL) ? (termDirty || dirty) : true;
  switch (screen) {
    case Screen::SPLASH:       drawSplash(); break;
    case Screen::MENU:         drawMainMenu(); break;
    case Screen::ENV_LIST:     drawEnvList(); break;
    case Screen::ENV_EDIT:     drawEnvEdit(); break;
    case Screen::TERMINAL:     if (termDirty || dirty) termDraw(); break;
    case Screen::SESSION_MENU: drawSessionMenu(); break;
    case Screen::AI_INPUT:     drawAiInput(); break;
    case Screen::AI_RESULT:    drawAiResult(); break;
    case Screen::SETTINGS:     drawSettings(); break;
    case Screen::TEXT_INPUT:   drawTextInput(); break;
    case Screen::HOSTKEY:      drawHostKey(); break;
    case Screen::WIFI:         drawWifi(); break;
    case Screen::ABOUT:        drawAbout(); break;
  }
  if (drew) present();
  dirty = false;
}

// ---------------------------------------------------------------------------
//  Actions
// ---------------------------------------------------------------------------
static void connectTo(int i) {
  editEnv = gConfig.envs[i];
  termReset();
  termScrollMode = false; termScrollOff = 0;
  gSsh.resize(TERM_COLS, TERM_ROWS);
  gSsh.start(editEnv);
  screen = Screen::TERMINAL; dirty = termDirty = true;
}

static void runAi() {
  // Blocking call — show a placeholder first so the user sees feedback.
  aiAnswer = "Asking Claude…"; screen = Screen::AI_RESULT; aiScroll = 0;
  dirty = true; render();

  String ctx;
  if (aiFromTerminal) {
    int from = max(0, (int)term.size() - 18);
    for (int i = from; i < (int)term.size(); i++) ctx += term[i].t + "\n";
  }
  aiAnswer = AI::ask(aiQuestion, ctx);
  // First line is treated as a runnable command suggestion.
  int nl = aiAnswer.indexOf('\n');
  aiLastCmd = (nl < 0) ? aiAnswer : aiAnswer.substring(0, nl);
  dirty = true;
}

// ---------------------------------------------------------------------------
//  Input handling
// ---------------------------------------------------------------------------
static void handleMenu(const InputEvent& ev) {
  if (ev.type == EvType::UP)   { menuSel = (menuSel + kMenuN - 1) % kMenuN; dirty = true; }
  if (ev.type == EvType::DOWN) { menuSel = (menuSel + 1) % kMenuN;          dirty = true; }
  if (ev.type == EvType::SELECT || ev.ch == '\r') {
    switch (menuSel) {
      case 0: screen = Screen::ENV_LIST; listSel = 0; break;
      case 1: aiFromTerminal = false; aiQuestion = ""; screen = Screen::AI_INPUT; break;
      case 2: wifiMsg = ""; screen = Screen::WIFI; startWifiScan(); break;
      case 3: screen = Screen::SETTINGS; setSel = 0; break;
      case 4: screen = Screen::ABOUT; break;
    }
    dirty = true;
  }
}

static void handleAbout(const InputEvent& ev) {
  if (ev.type == EvType::LEFT || ev.type == EvType::SELECT || ev.ch == '`' || ev.ch == '\r') {
    screen = Screen::MENU; dirty = true;
  }
}

static void handleEnvList(const InputEvent& ev) {
  int n = gConfig.envs.size();
  if (ev.type == EvType::UP)   { listSel = max(0, listSel - 1); dirty = true; }
  if (ev.type == EvType::DOWN) { listSel = min(n, listSel + 1); dirty = true; }
  if (ev.type == EvType::LEFT || ev.ch == '`') { screen = Screen::MENU; dirty = true; }
  if (ev.ch == 'd' && listSel < n) { Storage::removeEnv(listSel); listSel = max(0, listSel - 1); dirty = needClear = true; }
  if ((ev.type == EvType::RIGHT || ev.ch == 'e') && listSel < n) {
    editEnv = gConfig.envs[listSel]; editIndex = listSel; editSel = 0;
    screen = Screen::ENV_EDIT; dirty = true;
  }
  if (ev.type == EvType::SELECT || ev.ch == '\r') {
    if (listSel == n) { editEnv = SshEnv(); editIndex = -1; editSel = 0; screen = Screen::ENV_EDIT; }
    else connectTo(listSel);
    dirty = true;
  }
}

// Cycle the connection's key through "(none)" + the files in /keys.
static void cycleKey() {
  std::vector<String> opts;
  opts.push_back("");                 // (none)
  for (auto& k : Keys::list()) opts.push_back(k);
  int cur = 0;
  for (size_t i = 0; i < opts.size(); i++) if (opts[i] == editEnv.keyName) { cur = i; break; }
  int next = (cur + 1) % opts.size();
  editEnv.keyName = opts[next];
  editEnv.auth = editEnv.keyName.length() ? 1 : 0;  // picking a key implies key auth
}

static void handleEnvEdit(const InputEvent& ev) {
  if (ev.type == EvType::UP)   { editSel = (editSel + ENV_FIELDS - 1) % ENV_FIELDS; dirty = true; }
  if (ev.type == EvType::DOWN) { editSel = (editSel + 1) % ENV_FIELDS;              dirty = true; }
  if (ev.type == EvType::LEFT || ev.ch == '`') { screen = Screen::ENV_LIST; dirty = true; }
  if (ev.type == EvType::SELECT || ev.ch == '\r') {
    switch (editSel) {
      case 0: startEdit("name", editEnv.name, false, Screen::ENV_EDIT, [](const String& v){ editEnv.name = v; }); break;
      case 1: startEdit("host", editEnv.host, false, Screen::ENV_EDIT, [](const String& v){ editEnv.host = v; }); break;
      case 2: startEdit("port", String(editEnv.port), false, Screen::ENV_EDIT, [](const String& v){ editEnv.port = (uint16_t)v.toInt(); }); break;
      case 3: startEdit("user", editEnv.user, false, Screen::ENV_EDIT, [](const String& v){ editEnv.user = v; }); break;
      case 4: editEnv.auth ^= 1; dirty = true; break;                 // toggle Password/Key
      case 5: startEdit("pass", editEnv.pass, true,  Screen::ENV_EDIT, [](const String& v){ editEnv.pass = v; }); break;
      case 6: cycleKey(); dirty = true; break;                        // pick key file
      case 7:
        if (editIndex >= 0) Storage::updateEnv(editIndex, editEnv);
        else                Storage::addEnv(editEnv);
        screen = Screen::ENV_LIST; dirty = true; break;
      case 8:
        if (editIndex >= 0) { Storage::removeEnv(editIndex); screen = Screen::ENV_LIST; dirty = needClear = true; }
        break;
    }
  }
}

static void handleTerminal(const InputEvent& ev) {
  // Scrollback mode hijacks the trackball: up/down pan history, left/click exit.
  if (termScrollMode) {
    if (ev.type == EvType::UP)   { termScrollOff = min(termMaxScroll(), termScrollOff + 3); termDirty = true; }
    if (ev.type == EvType::DOWN) { termScrollOff = max(0, termScrollOff - 3);                termDirty = true; }
    if (ev.type == EvType::LEFT || ev.type == EvType::SELECT || ev.ch == '`') {
      termScrollMode = false; termScrollOff = 0; termDirty = true;
    }
    return;
  }
  if (ev.type == EvType::SELECT) { sessSel = 0; screen = Screen::SESSION_MENU; dirty = true; return; }
  if (ev.type == EvType::UP || ev.type == EvType::DOWN ||
      ev.type == EvType::LEFT || ev.type == EvType::RIGHT) { termSendArrow(ev.type); return; }
  if (ev.type == EvType::CHAR) {
    char c = ev.ch;
    if (c == '\r') gSsh.send("\n");      // map keyboard Enter to newline
    else { char b[1] = {c}; gSsh.send(b, 1); }
  }
}

static void handleSessionMenu(const InputEvent& ev) {
  if (ev.type == EvType::UP)   { sessSel = (sessSel + kSessN - 1) % kSessN; dirty = true; }
  if (ev.type == EvType::DOWN) { sessSel = (sessSel + 1) % kSessN;          dirty = true; }
  if (ev.type == EvType::LEFT || ev.ch == '`') { screen = Screen::TERMINAL; dirty = termDirty = true; }
  if (ev.type == EvType::SELECT || ev.ch == '\r') {
    screen = Screen::TERMINAL; termDirty = true;       // default: back to shell
    switch (sessSel) {
      case 0: aiFromTerminal = true; aiQuestion = ""; screen = Screen::AI_INPUT; break;
      case 1: if (aiLastCmd.length()) { gSsh.send(aiLastCmd); gSsh.send("\n"); } break;
      case 2: gSsh.send("\x03"); break;                // Ctrl-C
      case 3: gSsh.send("\t");   break;                // Tab
      case 4: gSsh.send("\x1b"); break;                // Esc
      case 5: gSsh.send("\x04"); break;                // Ctrl-D
      case 6: termScrollMode = true; termScrollOff = 0; break;
      case 7: termReset(); break;                      // local clear
      case 8: gSsh.stop(); screen = Screen::MENU; break;
    }
    dirty = true;
  }
}

static void handleAiInput(const InputEvent& ev) {
  if (ev.type == EvType::LEFT) { screen = aiFromTerminal ? Screen::TERMINAL : Screen::MENU; dirty = termDirty = true; return; }
  if (ev.type == EvType::CHAR) {
    if (ev.ch == '\r') { if (aiQuestion.length() && AI::configured()) runAi(); }
    else if (ev.ch == 0x08) { if (aiQuestion.length()) aiQuestion.remove(aiQuestion.length() - 1); dirty = true; }
    else if (ev.ch >= 32)   { aiQuestion += ev.ch; dirty = true; }
  }
}

static void handleAiResult(const InputEvent& ev) {
  if (ev.type == EvType::UP)   { aiScroll = max(0, aiScroll - 1); dirty = true; }
  if (ev.type == EvType::DOWN) { aiScroll++; dirty = true; }
  if (ev.type == EvType::LEFT || ev.ch == '`') {
    screen = aiFromTerminal ? Screen::TERMINAL : Screen::AI_INPUT; dirty = termDirty = true;
  }
  if ((ev.type == EvType::SELECT || ev.ch == '\r') && aiFromTerminal && aiLastCmd.length()) {
    gSsh.send(aiLastCmd); gSsh.send("\n");
    screen = Screen::TERMINAL; termDirty = true;
  }
}

static void handleSettings(const InputEvent& ev) {
  if (ev.type == EvType::UP)    { setSel = (setSel + 4) % 5; dirty = true; }
  if (ev.type == EvType::DOWN)  { setSel = (setSel + 1) % 5; dirty = true; }
  if (ev.type == EvType::RIGHT) { setReveal = !setReveal;    dirty = true; }   // peek secrets
  if (ev.type == EvType::LEFT || ev.ch == '`') { setReveal = false; screen = Screen::MENU; dirty = true; }
  if (ev.type == EvType::SELECT || ev.ch == '\r') {
    switch (setSel) {
      case 0: startEdit("WiFi SSID", gConfig.wifiSsid, false, Screen::SETTINGS, [](const String& v){ gConfig.wifiSsid = v; savedFlash = Storage::save() ? millis() : 0; }); break;
      case 1: startEdit("WiFi Pass", gConfig.wifiPass, true,  Screen::SETTINGS, [](const String& v){ gConfig.wifiPass = v; savedFlash = Storage::save() ? millis() : 0; }); break;
      case 2: startEdit("API Key",   gConfig.anthropicKey, true, Screen::SETTINGS, [](const String& v){ gConfig.anthropicKey = v; savedFlash = Storage::save() ? millis() : 0; }); break;
      case 3: startEdit("AI Model",  gConfig.aiModel, false, Screen::SETTINGS, [](const String& v){ gConfig.aiModel = v; savedFlash = Storage::save() ? millis() : 0; }); break;
      case 4: savedFlash = Storage::save() ? millis() : 0; WifiMgr::connect(); break;
    }
    dirty = true;
  }
}

static void handleWifi(const InputEvent& ev) {
  int n = wifiNets.size();
  if (ev.type == EvType::UP)   { wifiSel = max(0, wifiSel - 1); dirty = true; }
  if (ev.type == EvType::DOWN) { wifiSel = min(n - 1, wifiSel + 1); dirty = true; }
  if (ev.type == EvType::LEFT || ev.ch == '`') { screen = Screen::MENU; dirty = true; }
  if (ev.ch == 'r') { startWifiScan(); }
  if ((ev.type == EvType::SELECT || ev.ch == '\r') && n > 0 && wifiSel < n) {
    WifiMgr::Net net = wifiNets[wifiSel];
    bool samePass = (net.ssid == gConfig.wifiSsid);
    gConfig.wifiSsid = net.ssid;
    if (net.secure) {
      startEdit("WiFi pass: " + net.ssid, samePass ? gConfig.wifiPass : "", true, Screen::WIFI,
                [](const String& v) { gConfig.wifiPass = v; Storage::save(); wifiConnectNow(); });
    } else {
      gConfig.wifiPass = ""; Storage::save(); wifiConnectNow();
    }
  }
}

static void handleHostKey(const InputEvent& ev) {
  if (ev.type == EvType::LEFT)  { hkSel = 0; dirty = true; }
  if (ev.type == EvType::RIGHT) { hkSel = 1; dirty = true; }
  if (ev.type == EvType::UP || ev.type == EvType::DOWN) { hkSel ^= 1; dirty = true; }
  if (ev.type == EvType::SELECT || ev.ch == '\r') {
    gSsh.resolveHostKey(hkSel == 0);     // 0 = accept, 1 = reject
    screen = Screen::TERMINAL; termDirty = dirty = true;
  }
}

static void handleTextInput(const InputEvent& ev) {
  if (ev.type == EvType::SELECT) { tiReveal = !tiReveal; dirty = true; return; }  // peek
  if (ev.type != EvType::CHAR) return;
  if (ev.ch == '\r')        { if (tiDone) tiDone(tiBuf); screen = tiReturn; dirty = true; }
  else if (ev.ch == 0x08)   { if (tiBuf.length()) tiBuf.remove(tiBuf.length() - 1); dirty = true; }
  else if (ev.ch >= 32)     { tiBuf += ev.ch; dirty = true; }
}

void handle(const InputEvent& ev) {
  if (ev.type == EvType::NONE) return;
  switch (screen) {
    case Screen::SPLASH:       break;
    case Screen::MENU:         handleMenu(ev); break;
    case Screen::ENV_LIST:     handleEnvList(ev); break;
    case Screen::ENV_EDIT:     handleEnvEdit(ev); break;
    case Screen::TERMINAL:     handleTerminal(ev); break;
    case Screen::SESSION_MENU: handleSessionMenu(ev); break;
    case Screen::AI_INPUT:     handleAiInput(ev); break;
    case Screen::AI_RESULT:    handleAiResult(ev); break;
    case Screen::SETTINGS:     handleSettings(ev); break;
    case Screen::TEXT_INPUT:   handleTextInput(ev); break;
    case Screen::HOSTKEY:      handleHostKey(ev); break;
    case Screen::WIFI:         handleWifi(ev); break;
    case Screen::ABOUT:        handleAbout(ev); break;
  }
}

// ---------------------------------------------------------------------------
//  Touch — map a tap to the current screen's controls. Big targets so finger
//  taps land reliably even if the GT911 mapping needs calibration. Most taps
//  reuse the existing keyboard/trackball handlers by synthesizing events.
// ---------------------------------------------------------------------------
void touch(int x, int y) {
  // Left side of the header = universal "back" (not in the terminal, where the
  // trackball arrows belong to the shell).
  if (y < HDR_H && x < 90 && screen != Screen::TERMINAL) {
    handle({EvType::LEFT, 0});
    return;
  }
  switch (screen) {
    case Screen::MENU: {
      const int y0 = HDR_H + 6, rh = 37, ch = rh - 5;
      for (int i = 0; i < kMenuN; i++) {
        int ry = y0 + i * rh;
        if (y >= ry && y < ry + ch) { menuSel = i; dirty = true; handleMenu({EvType::SELECT, 0}); break; }
      }
      break;
    }
    case Screen::ENV_LIST: {
      int n = gConfig.envs.size();
      const int y0 = HDR_H + 6, rh = 30, ch = rh - 5;
      for (int i = 0; i <= n; i++) {
        int ry = y0 + i * rh;
        if (y >= ry && y < ry + ch) {
          listSel = i;
          if (x > 252 && i < n) {        // right edge of a row = edit
            editEnv = gConfig.envs[i]; editIndex = i; editSel = 0; screen = Screen::ENV_EDIT;
          } else {
            handleEnvList({EvType::SELECT, 0});
          }
          dirty = true; break;
        }
      }
      break;
    }
    case Screen::ENV_EDIT: {
      for (int i = 0; i < ENV_FIELDS; i++) {
        int ry = HDR_H + 2 + i * ROW_H;
        if (y >= ry && y < ry + ROW_H - 2) { editSel = i; dirty = true; handleEnvEdit({EvType::SELECT, 0}); break; }
      }
      break;
    }
    case Screen::SETTINGS: {
      const int y0 = HDR_H + 6, rh = 30, ch = rh - 5;
      for (int i = 0; i < 5; i++) {
        int ry = y0 + i * rh;
        if (y >= ry && y < ry + ch) { setSel = i; dirty = true; handleSettings({EvType::SELECT, 0}); break; }
      }
      break;
    }
    case Screen::WIFI: {
      int n = wifiNets.size();
      int top = HDR_H + 16, rh = 26, ch = rh - 4;
      int rows = (226 - top) / rh;
      int off = max(0, wifiSel - (rows - 1));
      for (int i = 0; i < rows && off + i < n; i++) {
        int ry = top + i * rh;
        if (y >= ry && y < ry + ch) { wifiSel = off + i; dirty = true; handleWifi({EvType::SELECT, 0}); break; }
      }
      break;
    }
    case Screen::SESSION_MENU: {
      const int px = 34, py = 26, pw = 252;
      for (int i = 0; i < kSessN; i++) {
        int ry = py + 20 + i * 20;
        if (y >= ry && y < ry + 18 && x >= px && x <= px + pw) { sessSel = i; dirty = true; handleSessionMenu({EvType::SELECT, 0}); break; }
      }
      break;
    }
    case Screen::HOSTKEY:
      if (y >= 200 && y <= 222) { hkSel = (x < 160) ? 0 : 1; handleHostKey({EvType::SELECT, 0}); }
      break;
    case Screen::TERMINAL:    handleTerminal({EvType::SELECT, 0}); break;  // tap = session menu
    case Screen::AI_RESULT:   handleAiResult({EvType::LEFT, 0});   break;  // tap = back
    case Screen::ABOUT:       screen = Screen::MENU; dirty = true; break;
    default: break;   // AI_INPUT / TEXT_INPUT need the keyboard
  }
}

// ---------------------------------------------------------------------------
//  Per-loop tick
// ---------------------------------------------------------------------------
void tick() {
  if (screen == Screen::SPLASH) {
    if (millis() - splashAt > 3000) { screen = Screen::MENU; dirty = true; }
    else                            { dirty = true; }   // keep the rain animating
  }
  // Animate the menu's rain background at ~14 fps.
  if (screen == Screen::MENU) {
    static uint32_t lastRain = 0;
    if (millis() - lastRain > 70) { lastRain = millis(); dirty = true; }
  }
  // Blink the terminal cursor (~2 Hz) when the shell is live and not scrolling.
  if (screen == Screen::TERMINAL && !termScrollMode &&
      gSsh.state() == SshSession::State::CONNECTED) {
    static uint32_t blink = 0;
    if (millis() - blink > 500) { blink = millis(); termDirty = true; }
  }
  // The SSH task blocks on the UI for host-key approval — pop the prompt.
  if (screen == Screen::TERMINAL && gSsh.state() == SshSession::State::HOSTKEY) {
    hkSel = 0; screen = Screen::HOSTKEY; dirty = true;
  }
  // Refresh the Settings screen while the "saved" toast is visible.
  if (screen == Screen::SETTINGS && savedFlash && millis() - savedFlash < 1600) dirty = true;
  // Drain SSH output into the terminal whenever a session is live.
  if (gSsh.state() == SshSession::State::CONNECTED ||
      gSsh.state() == SshSession::State::AUTH ||
      gSsh.state() == SshSession::State::CONNECTING) {
    String d = gSsh.drain();
    if (d.length()) termFeed(d);
  }
}

}  // namespace UI
