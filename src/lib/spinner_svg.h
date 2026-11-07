#pragma once

#include <array>
#include <string_view>

// The icons of the status of a program as in-memory SVG documents (raw strings),
// for wxBitmapBundle::FromSVG(): the spinner of a working program, the pulse of
// a program that waits for the user, and the check mark of a finished program.
// Copied from the agent-spinner-N.svg, agent-attention-blocked-N.svg and
// agent-attention-done.svg icons of CodeLite. "Dark" is for a dark appearance,
// "Light" for a light one.
namespace spinner_svg {

inline constexpr size_t kFrameCount = 16;

inline constexpr std::array<std::string_view, kFrameCount> kSpinnerDark = {
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(0 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(22.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(45 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(67.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(90 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(112.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(135 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(157.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(180 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(202.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(225 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(247.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(270 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(292.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(315 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#3A4658" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#60A5FA" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(337.5 8 8)"/>
</svg>)SVG",
};

inline constexpr std::array<std::string_view, kFrameCount> kSpinnerLight = {
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(0 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(22.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(45 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(67.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(90 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(112.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(135 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(157.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(180 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(202.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(225 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(247.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(270 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(292.5 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(315 8 8)"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6" fill="none" stroke="#D1D5DB" stroke-width="2"/>
  <circle cx="8" cy="8" r="6" fill="none" stroke="#2563EB" stroke-width="2" stroke-linecap="round" stroke-dasharray="12.57 25.13" transform="rotate(337.5 8 8)"/>
</svg>)SVG",
};

inline constexpr std::array<std::string_view, kFrameCount> kBlockedDark = {
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="4.60" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.75"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="1.00"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="4.80" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.70"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.98"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.00" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.66"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.94"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.20" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.61"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.88"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.40" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.56"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.80"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.60" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.52"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.72"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.80" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.47"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.66"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.00" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.42"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.62"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.20" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.38"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.60"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.40" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.33"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.62"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.60" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.28"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.66"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.80" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.23"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.72"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7.00" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.19"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.80"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7.20" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.14"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.88"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7.40" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.09"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.94"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7.60" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.05"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.98"/>
</svg>)SVG",
};

inline constexpr std::array<std::string_view, kFrameCount> kBlockedLight = {
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="4.60" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.75"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="1.00"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="4.80" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.70"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.98"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.00" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.66"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.94"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.20" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.61"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.88"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.40" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.56"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.80"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.60" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.52"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.72"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="5.80" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.47"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.66"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.00" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.42"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.62"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.20" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.38"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.60"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.40" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.33"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.62"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.60" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.28"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.66"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="6.80" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.23"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.72"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7.00" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.19"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.80"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7.20" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.14"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.88"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7.40" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.09"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.94"/>
</svg>)SVG",
R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7.60" fill="none" stroke="#F59E0B" stroke-width="1.2" opacity="0.05"/>
  <circle cx="8" cy="8" r="4" fill="#F59E0B" opacity="0.98"/>
</svg>)SVG",
};

inline constexpr std::string_view kDoneDark = R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7" fill="#4CAF50"/>
  <polyline points="4.6,8.2 7.0,10.6 11.4,5.8" fill="none" stroke="#0D1117" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)SVG";

inline constexpr std::string_view kDoneLight = R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">
  <circle cx="8" cy="8" r="7" fill="#4CAF50"/>
  <polyline points="4.6,8.2 7.0,10.6 11.4,5.8" fill="none" stroke="#0D1117" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)SVG";

} // namespace spinner_svg
