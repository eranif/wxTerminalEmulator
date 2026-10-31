#include "terminal_gl_renderer.h"

#if USE_OPENGL

#include "terminal_logger.h"

#include "terminal_colour_glyph.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/image.h>

// Platform GL headers.
// • macOS  – OpenGL 3.2 core profile ships in the framework; no loader needed.
// • Windows – opengl32.dll only exports OpenGL 1.1; GLEW loads the rest.
// • Linux  – GLEW loads core profile functions.
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
// Windows and Linux: GLEW must be included before any other GL header.
#include <GL/glew.h>
#include <wx/glcanvas.h>
#endif

namespace {

constexpr int kAtlasSize = 1024;
// The atlas doubles up to this size when it is full (64 MB of RGBA).
constexpr int kAtlasMaxSize = 4096;
// At its largest size, the atlas is cleared at most this often.
constexpr std::chrono::seconds kAtlasClearInterval{1};

const char *kVertexShaderSrc = R"(#version 150
in vec2 a_pos;
in vec2 a_uv;
in vec4 a_color;
uniform mat4 u_ortho;
out vec2 v_uv;
out vec4 v_color;
void main() {
    gl_Position = u_ortho * vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
)";

const char *kFragmentShaderSrc = R"(#version 150
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_atlas;
uniform int u_useTexture;
out vec4 fragColor;
// The atlas holds premultiplied RGBA. A monochrome glyph is stored as
// (cov, cov, cov, cov) and v_color is the text colour, so the product tints it.
// A colour glyph (emoji) is stored with its own colours and v_color is white.
// Blending uses GL_ONE, GL_ONE_MINUS_SRC_ALPHA (premultiplied alpha).
void main() {
    if (u_useTexture == 1) {
        fragColor = texture(u_atlas, v_uv) * v_color;
    } else {
        fragColor = vec4(v_color.rgb * v_color.a, v_color.a);
    }
}
)";

GLuint CompileShader(GLenum type, const char *src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {0};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    TLOG_ERROR() << "GL shader compile failed: " << log << std::endl;
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

// Code point ranges where fonts often have colour (emoji) glyphs. Only these
// are checked for colour. Outside them, colour pixels are ClearType fringes on
// Windows and must be treated as grey coverage.
bool IsColourGlyphCandidate(char32_t ch) {
  return ch >= 0x1F000 || (ch >= 0x2300 && ch <= 0x23FF) ||
         (ch >= 0x2600 && ch <= 0x27BF) || (ch >= 0x2B00 && ch <= 0x2BFF) ||
         (ch >= 0x3000 && ch <= 0x303F) || (ch >= 0x3290 && ch <= 0x32FF);
}

using terminal::IsEmojiGlyph;
using terminal::ScaleFont;

// True if the white-on-black render has pixels that are not grey.
bool HasColourPixels(const unsigned char *rgb, size_t pixelCount) {
  constexpr int kMinChroma = 32;
  for (size_t i = 0; i < pixelCount; ++i) {
    const int r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
    const int hi = std::max({r, g, b});
    const int lo = std::min({r, g, b});
    if (hi - lo > kMinChroma) {
      return true;
    }
  }
  return false;
}

// Box-drawing (U+2500-U+257F) and block (U+2580-U+259F) characters are drawn
// here and not by the font. The cell width is a whole number of pixels, but
// the advance of a font is not, so the lines of a font glyph may stop short of
// the tile edge or be shifted when the glyph is resized. A table then shows
// gaps, or lines that do not meet, for some font sizes. Drawn to fill the
// tile, the lines of two cells always meet.
class TileMask {
public:
  TileMask(int w, int h) : m_w(w), m_h(h), m_px(size_t(w) * h, 0) {}

  // Set the pixels of [x0, x1) x [y0, y1).
  void Fill(int x0, int y0, int x1, int y1) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, m_w);
    y1 = std::min(y1, m_h);
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        m_px[size_t(y) * m_w + x] = 255;
      }
    }
  }

  // White on black, like the render of a font glyph.
  wxImage ToImage() const {
    wxImage img(m_w, m_h, false);
    unsigned char *data = img.GetData();
    for (size_t i = 0; i < m_px.size(); ++i) {
      data[i * 3] = data[i * 3 + 1] = data[i * 3 + 2] = m_px[i];
    }
    return img;
  }

private:
  int m_w;
  int m_h;
  std::vector<unsigned char> m_px;
};

// U+2500-U+257F: the style of the arms of each character, as "lrud" (left,
// right, up, down): 0 none, 1 light, 2 heavy, 3 double. "0000" is a character
// that is left to the font (dashed lines, arcs and diagonals).
constexpr const char *kBoxLines[128] = {
    // U+2500
    "1100", "2200", "0011", "0022", "0000", "0000", "0000", "0000", "0000",
    "0000", "0000", "0000", "0101", "0201", "0102", "0202",
    // U+2510
    "1001", "2001", "1002", "2002", "0110", "0210", "0120", "0220", "1010",
    "2010", "1020", "2020", "0111", "0211", "0121", "0112",
    // U+2520
    "0122", "0221", "0212", "0222", "1011", "2011", "1021", "1012", "1022",
    "2021", "2012", "2022", "1101", "2101", "1201", "2201",
    // U+2530
    "1102", "2102", "1202", "2202", "1110", "2110", "1210", "2210", "1120",
    "2120", "1220", "2220", "1111", "2111", "1211", "2211",
    // U+2540
    "1121", "1112", "1122", "2121", "1221", "2112", "1212", "2221", "2212",
    "2122", "1222", "2222", "0000", "0000", "0000", "0000",
    // U+2550
    "3300", "0033", "0301", "0103", "0303", "3001", "1003", "3003", "0310",
    "0130", "0330", "3010", "1030", "3030", "0311", "0133",
    // U+2560
    "0333", "3011", "1033", "3033", "3301", "1103", "3303", "3310", "1130",
    "3330", "3311", "1133", "3333", "0000", "0000", "0000",
    // U+2570
    "0000", "0000", "0000", "0000", "1000", "0010", "0100", "0001", "2000",
    "0020", "0200", "0002", "1200", "0012", "2100", "0021"};

void DrawBoxLines(TileMask &mask, const char *spec, int w, int h) {
  enum { L, R, U, D };
  const int style[4] = {spec[0] - '0', spec[1] - '0', spec[2] - '0',
                        spec[3] - '0'};
  const int light = std::max(1, (h + 10) / 20);

  // The bars of an arm, across its direction: one for a light or heavy line,
  // two for a double line. @p extent is the size of the tile across the arm.
  struct Bars {
    int n = 0;
    int off[2] = {0, 0};
    int len = 0;
  };
  auto barsOf = [&](int s, int extent) {
    Bars b;
    if (s == 3) {
      b.n = 2;
      b.len = light;
      b.off[0] = (extent - 3 * light) / 2;
      b.off[1] = b.off[0] + 2 * light;
    } else if (s != 0) {
      b.n = 1;
      b.len = s == 2 ? 2 * light : light;
      b.off[0] = (extent - b.len) / 2;
    }
    return b;
  };
  const Bars bars[4] = {barsOf(style[L], h), barsOf(style[R], h),
                        barsOf(style[U], w), barsOf(style[D], w)};

  // The junction in the middle: where the bars of the arms along one axis
  // are, with nothing in the other axis it is a point in the centre.
  auto junction = [](const Bars &a, const Bars &b, int centre, int &lo,
                     int &hi) {
    lo = INT_MAX;
    hi = INT_MIN;
    for (const Bars *p : {&a, &b}) {
      for (int i = 0; i < p->n; ++i) {
        lo = std::min(lo, p->off[i]);
        hi = std::max(hi, p->off[i] + p->len);
      }
    }
    if (lo > hi) {
      lo = hi = centre;
    }
  };
  int xLo, xHi, yLo, yHi;
  junction(bars[U], bars[D], w / 2, xLo, xHi);
  junction(bars[L], bars[R], h / 2, yLo, yHi);

  for (int arm = 0; arm < 4; ++arm) {
    if (!style[arm]) {
      continue;
    }
    const bool horizontal = arm == L || arm == R;
    // An arm from the right or the bottom runs from its inner end to the
    // tile edge; the others from the edge to their inner end.
    const bool fromHigh = arm == R || arm == D;
    const int travelLo = horizontal ? xLo : yLo;
    const int travelHi = horizontal ? xHi : yHi;
    const int centre = horizontal ? w / 2 : h / 2;
    const bool crossDouble = horizontal ? (style[U] == 3 || style[D] == 3)
                                        : (style[L] == 3 || style[R] == 3);
    const Bars &b = bars[arm];
    for (int i = 0; i < b.n; ++i) {
      int inner;
      if (style[arm] == 3) {
        // The bar on one side stops at the line that crosses there, so that
        // the double lines keep the gap between them.
        const int cross = horizontal ? (i == 0 ? U : D) : (i == 0 ? L : R);
        if (style[cross]) {
          const Bars &c = bars[cross];
          inner = fromHigh ? c.off[c.n - 1] : c.off[0] + c.len;
        } else {
          inner = fromHigh ? travelLo : travelHi;
        }
      } else {
        inner = crossDouble ? centre : (fromHigh ? travelLo : travelHi);
      }
      const int a = b.off[i];
      const int z = a + b.len;
      switch (arm) {
      case L:
        mask.Fill(0, a, inner, z);
        break;
      case R:
        mask.Fill(inner, a, w, z);
        break;
      case U:
        mask.Fill(a, 0, z, inner);
        break;
      default:
        mask.Fill(a, inner, z, h);
        break;
      }
    }
  }
}

// U+2580-U+259F, except the shades (U+2591-U+2593). Returns false for a
// character that is left to the font.
bool DrawBlock(TileMask &mask, char32_t ch, int w, int h) {
  // Positions in eighths of the tile.
  auto x = [&](int e) { return (w * e + 4) / 8; };
  auto y = [&](int e) { return (h * e + 4) / 8; };
  auto fill = [&](int x0, int y0, int x1, int y1) {
    mask.Fill(x(x0), y(y0), x(x1), y(y1));
  };
  if (ch == 0x2580) { // Upper half.
    fill(0, 0, 8, 4);
  } else if (ch >= 0x2581 && ch <= 0x2588) { // Lower 1/8 ... full.
    fill(0, 8 - int(ch - 0x2580), 8, 8);
  } else if (ch >= 0x2589 && ch <= 0x258F) { // Left 7/8 ... 1/8.
    fill(0, 0, 8 - int(ch - 0x2588), 8);
  } else if (ch == 0x2590) { // Right half.
    fill(4, 0, 8, 8);
  } else if (ch == 0x2594) { // Upper 1/8.
    fill(0, 0, 8, 1);
  } else if (ch == 0x2595) { // Right 1/8.
    fill(7, 0, 8, 8);
  } else if (ch >= 0x2596 && ch <= 0x259F) { // Quadrants.
    // Bits: 1 upper left, 2 upper right, 4 lower left, 8 lower right.
    constexpr int kQuadrants[10] = {4, 8, 1, 13, 9, 7, 11, 2, 6, 14};
    const int q = kQuadrants[ch - 0x2596];
    if (q & 1) {
      fill(0, 0, 4, 4);
    }
    if (q & 2) {
      fill(4, 0, 8, 4);
    }
    if (q & 4) {
      fill(0, 4, 4, 8);
    }
    if (q & 8) {
      fill(4, 4, 8, 8);
    }
  } else {
    return false;
  }
  return true;
}

// The tile of a box-drawing or block character, white on black. An empty
// image for any other character, and for those that the font draws.
wxImage RenderBoxGlyph(char32_t ch, int w, int h) {
  if (ch < 0x2500 || ch > 0x259F || w < 1 || h < 1) {
    return wxImage();
  }
  TileMask mask(w, h);
  if (ch <= 0x257F) {
    const char *spec = kBoxLines[ch - 0x2500];
    if (std::string_view(spec) == "0000") {
      return wxImage();
    }
    DrawBoxLines(mask, spec, w, h);
  } else if (!DrawBlock(mask, ch, w, h)) {
    return wxImage();
  }
  return mask.ToImage();
}

} // namespace

TerminalGLRenderer::~TerminalGLRenderer() {
  // No GL calls here. Destroy() must be called by the owner while its context
  // is current. If it was not (the canvas was hidden), the context that is
  // current now belongs to another control: the GL names are small numbers,
  // the same in every context, so deleting them here would delete the objects
  // of that control. The driver frees ours with the context.
  m_vbo = 0;
  m_vao = 0;
  m_atlasTex = 0;
  m_program = 0;
  m_glyphs.clear();
}

bool TerminalGLRenderer::Init(bool withGlyphs) {
  if (m_program != 0) {
    return true;
  }

#if !defined(__APPLE__)
  // glewInit() must be called once per GL context. Requires a current context.
  // (macOS uses native OpenGL, not GLEW)
  static bool s_glewInitialized = false;
  if (!s_glewInitialized) {
    GLenum err = glewInit();
    // A GLX-built GLEW returns GLEW_ERROR_NO_GLX_DISPLAY with an EGL context
    // (wxGTK on Wayland). The core functions are already loaded, so this is ok.
    if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY) {
      TLOG_ERROR() << "glewInit failed: " << glewGetErrorString(err)
                   << std::endl;
      return false;
    }
    s_glewInitialized = true;
  }
#endif

  GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
  if (vs == 0 || fs == 0) {
    if (vs)
      glDeleteShader(vs);
    if (fs)
      glDeleteShader(fs);
    return false;
  }

  m_program = glCreateProgram();
  glAttachShader(m_program, vs);
  glAttachShader(m_program, fs);
  // Bind attribute locations before linking (GLSL 150 has no layout
  // qualifiers).
  glBindAttribLocation(m_program, 0, "a_pos");
  glBindAttribLocation(m_program, 1, "a_uv");
  glBindAttribLocation(m_program, 2, "a_color");
  glLinkProgram(m_program);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint linked = GL_FALSE;
  glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    char log[1024] = {0};
    glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
    TLOG_ERROR() << "GL program link failed: " << log << std::endl;
    glDeleteProgram(m_program);
    m_program = 0;
    return false;
  }

  m_locOrtho = glGetUniformLocation(m_program, "u_ortho");
  m_locUseTexture = glGetUniformLocation(m_program, "u_useTexture");
  m_locSampler = glGetUniformLocation(m_program, "u_atlas");

  // The sampler always reads texture unit 0; the value stays in the program.
  glUseProgram(m_program);
  glUniform1i(m_locSampler, 0);
  glUseProgram(0);

  // Vertex array + dynamic vertex buffer.
  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);
  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, x));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, u));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, r));
  glBindVertexArray(0);

  if (!withGlyphs) {
    TLOG_DEBUG() << "TerminalGLRenderer initialized (solid quads only)"
                 << std::endl;
    return true;
  }

  // Premultiplied RGBA atlas (see the fragment shader).
  GLint maxTextureSize = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  m_atlasMaxSize = std::clamp(static_cast<int>(maxTextureSize), kAtlasSize,
                              kAtlasMaxSize);
  glGenTextures(1, &m_atlasTex);
  glBindTexture(GL_TEXTURE_2D, m_atlasTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  AllocateAtlas(kAtlasSize);

  TLOG_DEBUG() << "TerminalGLRenderer initialized (atlas " << m_atlasW << "x"
               << m_atlasH << ")" << std::endl;
  return true;
}

void TerminalGLRenderer::Destroy() {
  if (m_vbo) {
    glDeleteBuffers(1, &m_vbo);
    m_vbo = 0;
  }
  if (m_vao) {
    glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
  }
  if (m_atlasTex) {
    glDeleteTextures(1, &m_atlasTex);
    m_atlasTex = 0;
  }
  if (m_program) {
    glDeleteProgram(m_program);
    m_program = 0;
  }
  m_glyphs.clear();
}

void TerminalGLRenderer::ReleaseContext() {
  glFinish();
#ifdef _WIN32
  wglMakeCurrent(nullptr, nullptr);
#endif
}

void TerminalGLRenderer::SetFonts(const wxFont &regular, const wxFont &bold,
                                  const wxFont &underlined,
                                  const wxFont &boldUnderlined) {
  m_fontRegular = regular;
  m_fontBold = bold;
  m_fontUnderlined = underlined;
  m_fontBoldUnderlined = boldUnderlined;

#ifdef __WXMSW__
  m_fontFallback =
      wxFont(regular.GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
             wxFONTWEIGHT_NORMAL, false, "Segoe UI Emoji");
#elif defined(__WXGTK__)
  m_fontFallback =
      wxFont(regular.GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
             wxFONTWEIGHT_NORMAL, false, "Noto Color Emoji");
#else
  m_fontFallback = regular;
#endif
}

void TerminalGLRenderer::AllocateAtlas(int size) {
  m_atlasW = size;
  m_atlasH = size;
  m_glyphs.clear();
  m_penX = m_penY = m_shelfH = 0;
  m_atlasFull = false;
  glBindTexture(GL_TEXTURE_2D, m_atlasTex);
  std::vector<unsigned char> zero(static_cast<size_t>(m_atlasW) * m_atlasH * 4,
                                  0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_atlasW, m_atlasH, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, zero.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}

bool TerminalGLRenderer::RecoverFullAtlas() {
  if (!m_atlasFull) {
    return false;
  }
  if (m_atlasW < m_atlasMaxSize) {
    AllocateAtlas(m_atlasW * 2);
    TLOG_DEBUG() << "GL glyph atlas full; grown to " << m_atlasW << "x"
                 << m_atlasH << std::endl;
    return true;
  }
  // When one frame needs more glyphs than the largest atlas holds, the frame
  // drawn on a cleared atlas fills it again. Clearing it on every paint would
  // rebuild the whole atlas each time, so clear it at most once a second. Until
  // then the atlas stays full: GetGlyph() does not try new glyphs, and they
  // stay blank.
  const auto now = std::chrono::steady_clock::now();
  if (now - m_atlasLastClear < kAtlasClearInterval) {
    return false;
  }
  // Drop the old tiles (glyphs no longer on the screen, old strip fonts).
  AllocateAtlas(m_atlasW);
  m_atlasLastClear = now;
  TLOG_DEBUG() << "GL glyph atlas full; cleared" << std::endl;
  return true;
}

std::chrono::milliseconds TerminalGLRenderer::AtlasRetryDelay() const {
  if (!m_atlasFull || m_atlasW < m_atlasMaxSize) {
    return std::chrono::milliseconds{0};
  }
  const auto left = m_atlasLastClear + kAtlasClearInterval -
                    std::chrono::steady_clock::now();
  // At least 1 ms: the second may have ended since RecoverFullAtlas() looked.
  return std::max(std::chrono::ceil<std::chrono::milliseconds>(left),
                  std::chrono::milliseconds{1});
}

void TerminalGLRenderer::ClearGlyphCache() {
  m_atlasLastClear = {};
  if (m_atlasTex) {
    // Back to the first size: the atlas may have grown for a font that needed
    // more room than the new one.
    AllocateAtlas(kAtlasSize);
  } else {
    m_glyphs.clear();
    m_penX = m_penY = m_shelfH = 0;
    m_atlasFull = false;
  }
}

std::uint64_t TerminalGLRenderer::MakeKey(char32_t ch, bool bold,
                                          bool underlined, int cellW) {
  std::uint64_t key = static_cast<std::uint64_t>(ch);
  // The same character can be one cell wide (a symbol) or two (an emoji with
  // the VS16 selector), so the tile width is part of the key.
  key |= (static_cast<std::uint64_t>(cellW) & 0xFFFF) << 40;
  if (bold)
    key |= (std::uint64_t{1} << 33);
  if (underlined)
    key |= (std::uint64_t{1} << 34);
  return key;
}

void TerminalGLRenderer::BeginFrame(int logicalW, int logicalH, double scale,
                                    const wxColour &bg) {
  m_scale = scale > 0 ? scale : 1.0;
  m_bgR = bg.Red() / 255.0f;
  m_bgG = bg.Green() / 255.0f;
  m_bgB = bg.Blue() / 255.0f;

  m_solidVerts.clear();
  m_glyphVerts.clear();
  m_solidVerts.reserve(m_solidVertsHWM);
  m_glyphVerts.reserve(m_glyphVertsHWM);

  // The wxGLCanvas drawable is a physical-pixel (best-resolution) surface, so
  // the viewport must cover the full physical framebuffer. Geometry is authored
  // in logical pixels (see the ortho below); the viewport's physical size maps
  // those logical units across the whole window and gives crisp HiDPI output.
  const int physW = static_cast<int>(std::lround(logicalW * m_scale));
  const int physH = static_cast<int>(std::lround(logicalH * m_scale));
  glViewport(0, 0, physW, physH);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  glClearColor(bg.Red() / 255.0f, bg.Green() / 255.0f, bg.Blue() / 255.0f,
               1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const float w = static_cast<float>(logicalW > 0 ? logicalW : 1);
  const float h = static_cast<float>(logicalH > 0 ? logicalH : 1);
  for (float &f : m_ortho)
    f = 0.0f;
  m_ortho[0] = 2.0f / w;
  m_ortho[5] = -2.0f / h;
  m_ortho[10] = -1.0f;
  m_ortho[12] = -1.0f;
  m_ortho[13] = 1.0f;
  m_ortho[15] = 1.0f;
}

void TerminalGLRenderer::PushQuad(std::vector<Vertex> &out, float x, float y,
                                  float w, float h, float u0, float v0,
                                  float u1, float v1, float r, float g, float b,
                                  float a) {
  const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  // Two triangles (CCW not required, culling disabled).
  out.push_back({x0, y0, u0, v0, r, g, b, a});
  out.push_back({x1, y0, u1, v0, r, g, b, a});
  out.push_back({x1, y1, u1, v1, r, g, b, a});

  out.push_back({x0, y0, u0, v0, r, g, b, a});
  out.push_back({x1, y1, u1, v1, r, g, b, a});
  out.push_back({x0, y1, u0, v1, r, g, b, a});
}

void TerminalGLRenderer::AddSolidRect(int x, int y, int w, int h,
                                      const wxColour &color) {
  if (w <= 0 || h <= 0)
    return;
  const float r = color.Red() / 255.0f;
  const float g = color.Green() / 255.0f;
  const float b = color.Blue() / 255.0f;
  if (r == m_bgR && g == m_bgG && b == m_bgB)
    return;
  PushQuad(m_solidVerts, static_cast<float>(x), static_cast<float>(y),
           static_cast<float>(w), static_cast<float>(h), 0, 0, 0, 0, r, g, b,
           1.0f);
}

void TerminalGLRenderer::AddSolidRect(int x, int y, int w, int h,
                                      const wxColour &color, float alpha) {
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  if (w <= 0 || h <= 0 || alpha <= 0.0f)
    return;
  // The shader premultiplies the colour with the alpha.
  PushQuad(m_solidVerts, static_cast<float>(x), static_cast<float>(y),
           static_cast<float>(w), static_cast<float>(h), 0, 0, 0, 0,
           color.Red() / 255.0f, color.Green() / 255.0f, color.Blue() / 255.0f,
           alpha);
}

void TerminalGLRenderer::AddGlyph(char32_t ch, bool bold, bool underlined,
                                  int x, int y, int cellW, int cellH,
                                  const wxColour &fg, const wxFont *font) {
  const GlyphInfo &g = GetGlyph(ch, bold, underlined, cellW, cellH, font);
  if (!g.ok)
    return;

  // The atlas tile is a full-cell render, so stretch it across the whole
  // logical cell rectangle. Sizing the quad to the cell (not the tile's
  // rasterized pixel size) keeps glyphs aligned with their background rects
  // independent of how ConvertToImage reports the DPI-scaled tile size.
  // Colour glyphs keep their own colours, so they are not tinted.
  const float r = g.colour ? 1.0f : fg.Red() / 255.0f;
  const float gr = g.colour ? 1.0f : fg.Green() / 255.0f;
  const float b = g.colour ? 1.0f : fg.Blue() / 255.0f;
  PushQuad(m_glyphVerts, static_cast<float>(x), static_cast<float>(y),
           static_cast<float>(cellW), static_cast<float>(cellH), g.u0, g.v0,
           g.u1, g.v1, r, gr, b, 1.0f);
}

const TerminalGLRenderer::GlyphInfo &
TerminalGLRenderer::GetGlyph(char32_t ch, bool bold, bool underlined, int cellW,
                             int cellH, const wxFont *customFont) {
  static const GlyphInfo kBlank{};

  // Whitespace contributes no glyph quad.
  if (ch == U' ' || ch == 0 || ch == U'\t') {
    return kBlank;
  }

  std::uint64_t key = MakeKey(ch, bold, underlined, cellW);
  if (customFont) {
    // The tile also depends on the font and on the tile height (a custom font
    // has its own line height), so mix both into the key. Building the font
    // description is slow, so keep the hash of the last font used: a strip
    // draws all its glyphs with one font. IsSameAs() compares the shared font
    // data, and the copy we hold makes a later change to the font get new data.
    if (!m_customFontKey.font.IsSameAs(*customFont) ||
        m_customFontKey.cellH != cellH) {
      const std::string desc =
          customFont->GetNativeFontInfoDesc().ToStdString() + "@" +
          std::to_string(cellH);
      m_customFontKey.font = *customFont;
      m_customFontKey.cellH = cellH;
      m_customFontKey.hash = std::hash<std::string>{}(desc);
    }
    key ^= m_customFontKey.hash * 0x9E3779B97F4A7C15ull;
  }
  auto it = m_glyphs.find(key);
  if (it != m_glyphs.end()) {
    return it->second;
  }
  // No room for new tiles until RecoverFullAtlas() grows or clears the atlas
  // (at the largest size, at most once a second).
  if (m_atlasFull) {
    return kBlank;
  }

#if 0
  bool preferFallback = m_fontFallback.IsOk() &&
                        (ch >= 0x10000 ||
                         (ch >= 0x2600 && ch <= 0x27BF) ||
                         (ch >= 0x1F300 && ch <= 0x1FAFF));
#else
  bool preferFallback{false};
#endif

  const wxFont &font = customFont             ? *customFont
                       : preferFallback       ? m_fontFallback
                       : (bold && underlined) ? m_fontBoldUnderlined
                       : bold                 ? m_fontBold
                       : underlined           ? m_fontUnderlined
                                              : m_fontRegular;

  const int cellWPhys =
      std::max(1, static_cast<int>(std::ceil(cellW * m_scale)));
  const int cellHPhys =
      std::max(1, static_cast<int>(std::ceil(cellH * m_scale)));

  GlyphInfo info = RasterizeAndPack(ch, font, cellWPhys, cellHPhys, m_scale);
  if (m_atlasFull) {
    // Not cached, so the glyph is tried again once the atlas has room.
    return kBlank;
  }
  auto res = m_glyphs.emplace(key, info);
  return res.first->second;
}

TerminalGLRenderer::GlyphInfo
TerminalGLRenderer::RasterizeAndPack(char32_t ch, const wxFont &font,
                                     int cellWPhys, int cellHPhys,
                                     double scale) {
  GlyphInfo info;
  if (!font.IsOk() || m_atlasTex == 0) {
    return info;
  }

  // Rasterize the glyph supersampled at physical resolution. We do NOT use
  // wxBitmap::SetScaleFactor (on macOS the wxMemoryDC ignores it for text and
  // draws at logical size into the top of the physical tile). Instead we pick a
  // font point size that makes the font's *measured* line height equal the
  // physical tile height: point size does not map 1:1 to pixels, so we measure
  // the base font's line height in this very DC and scale by the exact ratio
  // needed. The glyph then fills the physical tile and is minified onto the
  // logical cell with GL_LINEAR, staying sharp on HiDPI displays.
  // Offset that centres the visible pixels of a shrunk glyph or an emoji.
  // Measured on the first render, reused by the second (white) render.
  wxPoint inkShift{0, 0};
  bool inkMeasured = false;
  auto render = [&](const wxColour &bg) -> wxImage {
    wxBitmap bmp(cellWPhys, cellHPhys, 32);
    {
      wxMemoryDC dc(bmp);
      dc.SetBackground(wxBrush(bg));
      dc.Clear();

      // Measure the base font's natural line height in device pixels, then
      // scale the point size so the rendered line height matches the physical
      // tile.
      dc.SetFont(font);
      const int baseLineH = std::max(1, dc.GetCharHeight());
      const double basePt = font.GetFractionalPointSize();
      wxFont scaledFont = font;
      if (basePt > 0 && baseLineH != cellHPhys) {
        scaledFont.SetFractionalPointSize(basePt *
                                          (double(cellHPhys) / baseLineH));
      }

      const wxString text = wxString(wxUniChar(ch));
      const bool isEmoji = IsEmojiGlyph(ch);
      bool drawn = false;
#ifdef __WXMSW__
      if (isEmoji) {
        drawn = terminal::DrawColourGlyphD2D(
            dc, text, scaledFont, *wxWHITE, wxRect(0, 0, cellWPhys, cellHPhys));
      }
#endif
      if (!drawn) {
        dc.SetFont(scaledFont);
        wxPoint pos{0, 0};
        // A glyph can be wider than the tile (emoji, or a symbol that the
        // font draws wider than one cell): shrink it, or the tile clips it.
        // Never touch Latin-1 (U+0000-U+00FF): that is normal text, which
        // must stay on the cell grid exactly as the font draws it. The same
        // goes for any glyph that fits its tile: only emoji and glyphs that
        // are too wide are resized and centred. Box-drawing and block
        // characters that the font still draws (dashed lines, arcs, shades)
        // stay where the font puts them: centring their ink would move them
        // off the lines of their neighbours.
        wxSize extent = dc.GetTextExtent(text);
        bool shrunk = false;
        const bool isBoxOrBlock = ch >= 0x2500 && ch <= 0x259F;
        if (ch > 0xFF && !isBoxOrBlock && extent.x > cellWPhys) {
          shrunk = true;
          scaledFont = ScaleFont(scaledFont, double(cellWPhys) / extent.x);
          dc.SetFont(scaledFont);
          extent = dc.GetTextExtent(text);
        }
        if (isEmoji || shrunk) {
          // Centre emoji (like the Direct2D path does) and shrunk glyphs: the
          // smaller font would otherwise sit at the top-left of the tile.
          pos.x = (cellWPhys - extent.x) / 2;
          pos.y = (cellHPhys - extent.y) / 2;
          // GetTextExtent() is the advance box, not the ink. Fallback fonts
          // often draw off-centre inside it, so centre the visible pixels.
          if (!inkMeasured) {
            inkMeasured = true;
            wxBitmap probe(cellWPhys, cellHPhys, 32);
            {
              wxMemoryDC pdc(probe);
              pdc.SetBackground(*wxBLACK_BRUSH);
              pdc.Clear();
              pdc.SetFont(scaledFont);
              pdc.SetTextForeground(*wxWHITE);
              pdc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
              pdc.DrawText(text, pos);
            }
            const wxImage pimg = probe.ConvertToImage();
            if (pimg.IsOk()) {
              const unsigned char *d = pimg.GetData();
              int minX = cellWPhys, maxX = -1, minY = cellHPhys, maxY = -1;
              for (int y = 0; y < cellHPhys; ++y) {
                for (int x = 0; x < cellWPhys; ++x) {
                  const unsigned char *px = d + (y * cellWPhys + x) * 3;
                  if (px[0] | px[1] | px[2]) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                  }
                }
              }
              if (maxX >= minX && maxY >= minY) {
                inkShift.x = (cellWPhys - (maxX - minX + 1)) / 2 - minX;
                inkShift.y = (cellHPhys - (maxY - minY + 1)) / 2 - minY;
              }
            }
          }
          pos += inkShift;
        }
        dc.SetTextForeground(*wxWHITE);
        dc.SetTextBackground(bg);
        dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        dc.DrawText(text, pos);
      }
    }
    return bmp.ConvertToImage();
  };

  // Box-drawing and block characters fill the tile exactly (see
  // RenderBoxGlyph()), so they need no font.
  wxImage img = RenderBoxGlyph(ch, cellWPhys, cellHPhys);
  if (!img.IsOk()) {
    img = render(*wxBLACK);
  }
  if (!img.IsOk()) {
    return info;
  }

  const int w = img.GetWidth();
  const int h = img.GetHeight();
  const size_t pixelCount = static_cast<size_t>(w) * h;
  const unsigned char *onBlack = img.GetData();

  // Premultiplied RGBA tile for the atlas.
  std::vector<unsigned char> rgba(pixelCount * 4, 0);
  bool hasPixels = false;

  if (IsColourGlyphCandidate(ch) && HasColourPixels(onBlack, pixelCount)) {
    // A colour glyph (emoji). The render on black is already the premultiplied
    // colour. Render again on white to get alpha: a pixel with alpha A shows
    // C on black and C + (1 - A) * 255 on white.
    wxImage imgWhite = render(*wxWHITE);
    if (imgWhite.IsOk() && imgWhite.GetWidth() == w &&
        imgWhite.GetHeight() == h) {
      const unsigned char *onWhite = imgWhite.GetData();
      for (size_t i = 0; i < pixelCount; ++i) {
        const unsigned char *bp = onBlack + i * 3;
        const unsigned char *wp = onWhite + i * 3;
        const int diff =
            ((wp[0] - bp[0]) + (wp[1] - bp[1]) + (wp[2] - bp[2])) / 3;
        int a = std::clamp(255 - diff, 0, 255);
        a = std::max({a, int(bp[0]), int(bp[1]), int(bp[2])});
        rgba[i * 4 + 0] = bp[0];
        rgba[i * 4 + 1] = bp[1];
        rgba[i * 4 + 2] = bp[2];
        rgba[i * 4 + 3] = static_cast<unsigned char>(a);
        hasPixels = hasPixels || a > 0;
      }
      info.colour = true;
    }
  }

  if (!info.colour) {
    // Coverage = luminance of the white-on-black render (R channel is enough
    // for grayscale anti-aliasing).
    for (size_t i = 0; i < pixelCount; ++i) {
      const unsigned char cov = onBlack[i * 3];
      rgba[i * 4 + 0] = cov;
      rgba[i * 4 + 1] = cov;
      rgba[i * 4 + 2] = cov;
      rgba[i * 4 + 3] = cov;
      hasPixels = hasPixels || cov > 0;
    }
  }

  // If the tile is entirely empty the font has no glyph for this codepoint.
  if (!hasPixels) {
    info.colour = false;
    return info;
  }

  // Shelf-pack into the atlas.
  if (m_penX + w > m_atlasW) {
    m_penX = 0;
    m_penY += m_shelfH;
    m_shelfH = 0;
  }
  if (m_penY + h > m_atlasH) {
    // Atlas exhausted. The owner grows or clears it after the frame, with
    // RecoverFullAtlas().
    m_atlasFull = true;
    return info;
  }

  glBindTexture(GL_TEXTURE_2D, m_atlasTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, m_penX, m_penY, w, h, GL_RGBA,
                  GL_UNSIGNED_BYTE, rgba.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  info.u0 = static_cast<float>(m_penX) / m_atlasW;
  info.v0 = static_cast<float>(m_penY) / m_atlasH;
  info.u1 = static_cast<float>(m_penX + w) / m_atlasW;
  info.v1 = static_cast<float>(m_penY + h) / m_atlasH;
  info.w = w;
  info.h = h;
  info.ok = true;

  m_penX += w;
  m_shelfH = std::max(m_shelfH, h);
  return info;
}

void TerminalGLRenderer::EndFrame() {
  if (m_program == 0) {
    return;
  }

  m_solidVertsHWM = std::max(m_solidVertsHWM, m_solidVerts.size());
  m_glyphVertsHWM = std::max(m_glyphVertsHWM, m_glyphVerts.size());

  glUseProgram(m_program);
  glUniformMatrix4fv(m_locOrtho, 1, GL_FALSE, m_ortho);
  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

  // Pass 1: solid quads (backgrounds, cursor, focus border).
  if (!m_solidVerts.empty()) {
    glUniform1i(m_locUseTexture, 0);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_solidVerts.size() * sizeof(Vertex)),
                 m_solidVerts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_solidVerts.size()));
  }

  // Pass 2: textured glyph quads.
  if (!m_glyphVerts.empty()) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);
    glUniform1i(m_locUseTexture, 1);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_glyphVerts.size() * sizeof(Vertex)),
                 m_glyphVerts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_glyphVerts.size()));
  }

  glBindVertexArray(0);
  glUseProgram(0);
}

#endif // USE_OPENGL
