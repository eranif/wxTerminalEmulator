#pragma once

#include <cstdint>
#include <wx/colour.h>
#include <wx/font.h>

#ifdef __WXMAC__
constexpr int kDefaultFontSize = 20;
#else
constexpr int kDefaultFontSize = 14;
#endif

struct wxTerminalTheme {
  // Default foreground/background
  wxColour fg{0xC0, 0xC0, 0xC0};
  wxColour bg{0x00, 0x00, 0x00};

  // Base font used by the terminal view
  wxFont font{MakeDefaultFont()};

  // Normal colours
  wxColour black{0x00, 0x00, 0x00};
  wxColour red{0x80, 0x00, 0x00};
  wxColour green{0x00, 0x80, 0x00};
  wxColour yellow{0x80, 0x80, 0x00};
  wxColour blue{0x00, 0x00, 0x80};
  wxColour magenta{0x80, 0x00, 0x80};
  wxColour cyan{0x00, 0x80, 0x80};
  wxColour white{0xC0, 0xC0, 0xC0};

  // Bright colours
  wxColour brightBlack{0x80, 0x80, 0x80};
  wxColour brightRed{0xFF, 0x00, 0x00};
  wxColour brightGreen{0x00, 0xFF, 0x00};
  wxColour brightYellow{0xFF, 0xFF, 0x00};
  wxColour brightBlue{0x00, 0x00, 0xFF};
  wxColour brightMagenta{0xFF, 0x00, 0xFF};
  wxColour brightCyan{0x00, 0xFF, 0xFF};
  wxColour brightWhite{0xFF, 0xFF, 0xFF};

  // Selection colours (with alpha)
  wxColour selectionBg{70, 130, 180, 100};
  wxColour highlightBg{180, 140, 50, 100};

  // Cursor
  wxColour cursorColour{255, 255, 255};

  // Convert wxColour to packed uint32_t (RGB)
  static std::uint32_t ToU32(const wxColour &c) {
    return (c.Red() << 16) | (c.Green() << 8) | c.Blue();
  }

  static inline wxFont MakeDefaultFont() {
#ifdef __WXMAC__
    return wxFont(kDefaultFontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                  wxFONTWEIGHT_NORMAL, false, "Menlo");
#elif defined(__WXMSW__)
    return wxFont(kDefaultFontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                  wxFONTWEIGHT_NORMAL, false, "Consolas");
#else
    return wxFont(wxFontInfo(kDefaultFontSize).Family(wxFONTFAMILY_TELETYPE));
#endif
  }

  // Helper: get ANSI colour by index (0-7) + bright flag
  std::uint32_t GetAnsiColor(int index, bool bright = false) const {
    static const wxColour wxTerminalTheme::*normal[8] = {
        &wxTerminalTheme::black, &wxTerminalTheme::red,
        &wxTerminalTheme::green, &wxTerminalTheme::yellow,
        &wxTerminalTheme::blue,  &wxTerminalTheme::magenta,
        &wxTerminalTheme::cyan,  &wxTerminalTheme::white};
    static const wxColour wxTerminalTheme::*brights[8] = {
        &wxTerminalTheme::brightBlack, &wxTerminalTheme::brightRed,
        &wxTerminalTheme::brightGreen, &wxTerminalTheme::brightYellow,
        &wxTerminalTheme::brightBlue,  &wxTerminalTheme::brightMagenta,
        &wxTerminalTheme::brightCyan,  &wxTerminalTheme::brightWhite};
    if (index < 0 || index > 7)
      return ToU32(fg);
    return ToU32(this->*(bright ? brights[index] : normal[index]));
  }

  // Helper: 256-colour palette (first 16 from theme, rest computed)
  std::uint32_t Get256Color(int index) const {
    if (index < 16)
      return GetAnsiColor(index % 8, index >= 8);
    if (index < 232) {
      int idx = index - 16;
      int r = (idx / 36) * 51, g = ((idx / 6) % 6) * 51, b = (idx % 6) * 51;
      return (r << 16) | (g << 8) | b;
    }
    int gray = 8 + (index - 232) * 10;
    return (gray << 16) | (gray << 8) | gray;
  }

  static inline wxTerminalTheme MakeDarkTheme() { return wxTerminalTheme{}; }
  static inline wxTerminalTheme MakeLightTheme() {
    wxTerminalTheme t;
    t.fg = wxColour(0x00, 0x00, 0x00);
    t.bg = wxColour(0xFF, 0xFF, 0xFF);
    t.font = MakeDefaultFont();
    t.black = wxColour(0x00, 0x00, 0x00);
    t.red = wxColour(0xC0, 0x00, 0x00);
    t.green = wxColour(0x00, 0x80, 0x00);
    t.yellow = wxColour(0x80, 0x80, 0x00);
    t.blue = wxColour(0x00, 0x00, 0xC0);
    t.magenta = wxColour(0xC0, 0x00, 0xC0);
    t.cyan = wxColour(0x00, 0x80, 0x80);
    t.white = wxColour(0xC0, 0xC0, 0xC0);
    t.brightBlack = wxColour(0x80, 0x80, 0x80);
    t.brightRed = wxColour(0xFF, 0x00, 0x00);
    t.brightGreen = wxColour(0x00, 0xB0, 0x00);
    t.brightYellow = wxColour(0xFF, 0xFF, 0x00);
    t.brightBlue = wxColour(0x00, 0x00, 0xFF);
    t.brightMagenta = wxColour(0xFF, 0x00, 0xFF);
    t.brightCyan = wxColour(0x00, 0xFF, 0xFF);
    t.brightWhite = wxColour(0xFF, 0xFF, 0xFF);
    t.selectionBg = wxColour(51, 153, 255, 80);
    t.highlightBg = wxColour(255, 200, 50, 80);
    t.cursorColour = wxColour(0, 0, 0);
    return t;
  }
};
