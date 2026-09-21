#include "keyboard_layout.h"

#ifdef __WXMSW__

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace terminal {
namespace {

/// Set both the generic and the left/right-specific virtual key entries in
/// `state`, since some keyboard layout DLLs consult the handed variant (in
/// particular VK_RMENU for AltGr) while others only look at the generic one.
void SetModifierDown(BYTE *state, int vkGeneric, int vkLeft, int vkRight) {
  state[vkGeneric] = 0x80;
  state[vkLeft] = 0x80;
  state[vkRight] = 0x80;
}

} // namespace

KeyTranslation TranslateKeyWithActiveLayout(unsigned int nativeKeyCode,
                                            unsigned int nativeModifiers,
                                            std::string *utf8) {
  utf8->clear();

  const UINT vk = nativeKeyCode;
  const UINT scanCode = ::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);

  BYTE state[256] = {0};
  if (nativeModifiers & wxMOD_SHIFT) {
    SetModifierDown(state, VK_SHIFT, VK_LSHIFT, VK_RSHIFT);
  }
  if (nativeModifiers & wxMOD_CONTROL) {
    SetModifierDown(state, VK_CONTROL, VK_LCONTROL, VK_RCONTROL);
  }
  if (nativeModifiers & wxMOD_ALT) {
    SetModifierDown(state, VK_MENU, VK_LMENU, VK_RMENU);
  }
  // Caps Lock affects letter case; it's a toggle, so the "on" state is the
  // low bit of the virtual key state, not the "down" high bit.
  if (::GetKeyState(VK_CAPITAL) & 0x0001) {
    state[VK_CAPITAL] = 0x01;
  }

  wchar_t buffer[8] = {0};
  const int result =
      ::ToUnicode(vk, scanCode, state, buffer,
                 sizeof(buffer) / sizeof(buffer[0]), 0);

  if (result < 0) {
    // The first key of a dead-key sequence (e.g. the accent in a compose
    // sequence for accented Latin letters). Windows keeps the dead-key state
    // internally and will combine it with the next call to ToUnicode().
    return KeyTranslation::kNoOutput;
  }
  if (result == 0) {
    // The layout has nothing mapped to this key and modifier combination.
    return KeyTranslation::kNoOutput;
  }

  const int byteCount = ::WideCharToMultiByte(
      CP_UTF8, 0, buffer, result, nullptr, 0, nullptr, nullptr);
  if (byteCount <= 0) {
    return KeyTranslation::kUnavailable;
  }
  utf8->resize(static_cast<size_t>(byteCount));
  ::WideCharToMultiByte(CP_UTF8, 0, buffer, result, utf8->data(), byteCount,
                       nullptr, nullptr);
  return KeyTranslation::kText;
}

} // namespace terminal

#endif // __WXMSW__
