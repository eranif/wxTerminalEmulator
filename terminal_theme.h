#pragma once

#include <cstdint>
#include <wx/colour.h>

struct wxTerminalTheme {
  // Default foreground/background
  std::uint32_t fg{0x00C0C0C0};
  std::uint32_t bg{0x00000000};

  // Normal colours
  std::uint32_t black{0x000000};
  std::uint32_t red{0x800000};
  std::uint32_t green{0x008000};
  std::uint32_t yellow{0x808000};
  std::uint32_t blue{0x000080};
  std::uint32_t magenta{0x800080};
  std::uint32_t cyan{0x008080};
  std::uint32_t white{0xC0C0C0};

  // Bright colours
  std::uint32_t brightBlack{0x808080};
  std::uint32_t brightRed{0xFF0000};
  std::uint32_t brightGreen{0x00FF00};
  std::uint32_t brightYellow{0xFFFF00};
  std::uint32_t brightBlue{0x0000FF};
  std::uint32_t brightMagenta{0xFF00FF};
  std::uint32_t brightCyan{0x00FFFF};
  std::uint32_t brightWhite{0xFFFFFF};

  // Selection colours (with alpha)
  wxColour selectionBg{70, 130, 180, 100};
  wxColour highlightBg{180, 140, 50, 100};

  // Cursor
  wxColour cursorColour{255, 255, 255};

  // Helper: get ANSI colour by index (0-7) + bright flag
  std::uint32_t GetAnsiColor(int index, bool bright = false) const {
    static const std::uint32_t wxTerminalTheme::*normal[8] = {
        &wxTerminalTheme::black,   &wxTerminalTheme::red,
        &wxTerminalTheme::green,   &wxTerminalTheme::yellow,
        &wxTerminalTheme::blue,    &wxTerminalTheme::magenta,
        &wxTerminalTheme::cyan,    &wxTerminalTheme::white};
    static const std::uint32_t wxTerminalTheme::*brights[8] = {
        &wxTerminalTheme::brightBlack,   &wxTerminalTheme::brightRed,
        &wxTerminalTheme::brightGreen,   &wxTerminalTheme::brightYellow,
        &wxTerminalTheme::brightBlue,    &wxTerminalTheme::brightMagenta,
        &wxTerminalTheme::brightCyan,    &wxTerminalTheme::brightWhite};
    if (index < 0 || index > 7)
      return fg;
    return this->*(bright ? brights[index] : normal[index]);
  }

  // Helper: 256-colour palette (first 16 from theme, rest computed)
  std::uint32_t Get256Color(int index) const {
    if (index < 16)
      return GetAnsiColor(index % 8, index >= 8);
    if (index < 232) {
      int idx = index - 16;
      int r = (idx / 36) * 51, g = ((idx / 6) % 6) * 51,
          b = (idx % 6) * 51;
      return (r << 16) | (g << 8) | b;
    }
    int gray = 8 + (index - 232) * 10;
    return (gray << 16) | (gray << 8) | gray;
  }
};
