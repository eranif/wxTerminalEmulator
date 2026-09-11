#include "keyboard_layout.h"

#ifdef __WXMAC__

#include <Carbon/Carbon.h>

namespace terminal {
namespace {

// NSEvent modifier flags. They are redefined here so that this file stays plain
// C++ and does not need Objective-C headers.
constexpr unsigned int kNSCapsLock = 1u << 16;
constexpr unsigned int kNSShift = 1u << 17;
constexpr unsigned int kNSControl = 1u << 18;
constexpr unsigned int kNSOption = 1u << 19;
constexpr unsigned int kNSCommand = 1u << 20;

/// Convert NSEvent modifier flags to the modifier value expected by
/// UCKeyTranslate(), which uses the old Carbon bits shifted right by 8.
UInt32 ToUCKeyModifiers(unsigned int nsFlags) {
  UInt32 carbon = 0;
  if (nsFlags & kNSShift) {
    carbon |= shiftKey;
  }
  if (nsFlags & kNSCapsLock) {
    carbon |= alphaLock;
  }
  if (nsFlags & kNSOption) {
    carbon |= optionKey;
  }
  if (nsFlags & kNSControl) {
    carbon |= controlKey;
  }
  if (nsFlags & kNSCommand) {
    carbon |= cmdKey;
  }
  return (carbon >> 8) & 0xffu;
}

/// True for code points that are text, not a control or a special key.
///
/// macOS reports arrows, function keys and similar keys as code points in the
/// Unicode private use area (U+F700..U+F8FF).
bool IsTextCodePoint(char32_t cp) {
  if (cp < 0x20 || cp == 0x7f) {
    return false;
  }
  if (cp >= 0xf700 && cp <= 0xf8ff) {
    return false;
  }
  return true;
}

void AppendUtf8(char32_t cp, std::string *out) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out->push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

} // namespace

KeyTranslation TranslateKeyWithActiveLayout(unsigned int nativeKeyCode,
                                            unsigned int nativeModifiers,
                                            std::string *utf8) {
  utf8->clear();

  TISInputSourceRef source = TISCopyCurrentKeyboardInputSource();
  if (source == nullptr) {
    return KeyTranslation::kUnavailable;
  }
  // An input method (Japanese, Chinese, ...) has no layout data. In that case
  // we let wxWidgets handle the key through the normal text input path.
  CFDataRef layoutData = static_cast<CFDataRef>(
      TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
  if (layoutData == nullptr) {
    CFRelease(source);
    return KeyTranslation::kUnavailable;
  }
  const UCKeyboardLayout *layout =
      reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(layoutData));

  // Kept between calls so that dead keys compose with the next key press.
  static UInt32 deadKeyState = 0;

  UniChar chars[16] = {0};
  UniCharCount charCount = 0;
  OSStatus status =
      UCKeyTranslate(layout, static_cast<UInt16>(nativeKeyCode),
                     kUCKeyActionDown, ToUCKeyModifiers(nativeModifiers),
                     LMGetKbdType(), 0, &deadKeyState,
                     sizeof(chars) / sizeof(chars[0]), &charCount, chars);
  CFRelease(source);

  if (status != noErr) {
    return KeyTranslation::kUnavailable;
  }
  if (charCount == 0) {
    // Either a dead key was pressed, or the layout has nothing mapped to this
    // key and modifier combination.
    return KeyTranslation::kNoOutput;
  }

  for (UniCharCount i = 0; i < charCount; ++i) {
    char32_t cp = chars[i];
    // Combine a UTF-16 surrogate pair into a single code point.
    if (cp >= 0xd800 && cp <= 0xdbff && (i + 1) < charCount) {
      const char32_t low = chars[i + 1];
      if (low >= 0xdc00 && low <= 0xdfff) {
        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        ++i;
      }
    }
    if (!IsTextCodePoint(cp)) {
      utf8->clear();
      return KeyTranslation::kUnavailable;
    }
    AppendUtf8(cp, utf8);
  }
  return KeyTranslation::kText;
}

} // namespace terminal

#endif // __WXMAC__
