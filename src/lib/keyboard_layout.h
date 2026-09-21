#pragma once

// For __WXMAC__ and friends.
#include <wx/defs.h>

#include <string>

namespace terminal {

/// Result of translating a physical key with the active keyboard layout.
enum class KeyTranslation {
  /// No translation was possible (no layout data, an input method is active, or
  /// the key is not a text key). The caller should fall back to the normal
  /// wxWidgets key handling.
  kUnavailable,
  /// The layout maps this key plus modifiers to no text at all. Examples:
  /// Shift + a letter key in the Hebrew layout, or the first key of a dead-key
  /// sequence. The caller should send nothing and consume the event.
  kNoOutput,
  /// The layout produced text. It is stored in the output string as UTF-8.
  kText,
};

#if defined(__WXMAC__) || defined(__WXMSW__)
/// Translate a physical key press using the keyboard layout that is active
/// right now.
///
/// On macOS wxWidgets reports `wxKeyEvent::GetKeyCode()` and
/// `GetUnicodeKey()` after translating the key through an ASCII capable
/// layout. With a non-Latin layout (Hebrew, Russian, Greek, ...) both return
/// the Latin letter of the physical key, not the character the user typed.
/// On Windows, `wxKeyEvent::GetKeyCode()` reports a layout-independent
/// virtual key identity that likewise cannot represent umlauts, AltGr
/// symbols, or non-Latin layouts. This function asks the OS for the real
/// character instead, on both platforms.
///
/// @param nativeKeyCode On macOS, the value of `wxKeyEvent::GetRawKeyCode()`
/// (the macOS virtual key code). On Windows, the value of
/// `wxKeyEvent::GetRawKeyCode()` (the Win32 virtual-key code, VK_*).
/// @param nativeModifiers On macOS, the value of `wxKeyEvent::GetRawKeyFlags()`
/// (the `NSEvent` modifier flags). On Windows, `wxKeyEvent::GetModifiers()`
/// (the `wxMOD_*` bitmask); note `wxMOD_ALTGR` is `wxMOD_ALT | wxMOD_CONTROL`,
/// matching how Windows itself reports the AltGr key.
/// @param utf8 Receives the typed text, encoded as UTF-8. It is cleared first.
/// @return See `KeyTranslation`.
///
/// The function keeps the dead-key state between calls, so a dead key followed
/// by a base key returns the composed character.
KeyTranslation TranslateKeyWithActiveLayout(unsigned int nativeKeyCode,
                                            unsigned int nativeModifiers,
                                            std::string *utf8);
#endif

} // namespace terminal
