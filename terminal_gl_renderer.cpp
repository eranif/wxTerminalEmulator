#include "terminal_gl_renderer.h"

#if USE_OPENGL

#include "terminal_logger.h"

#include <cmath>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/image.h>

// Platform GL headers.
// • macOS  – OpenGL 3.2 core profile ships in the framework; no loader needed.
// • Windows – opengl32.dll only exports OpenGL 1.1; GLEW loads the rest.
// • Linux  – GL_GLEXT_PROTOTYPES lets the driver .so export symbols directly.
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
// GLEW must be included before any other GL header.
#include <GL/glew.h>
#include <wx/glcanvas.h>
#else
// Linux
#include <wx/glcanvas.h>
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace {

constexpr int kAtlasSize = 1024;

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
void main() {
    if (u_useTexture == 1) {
        float coverage = texture(u_atlas, v_uv).r;
        fragColor = vec4(v_color.rgb, v_color.a * coverage);
    } else {
        fragColor = v_color;
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

} // namespace

TerminalGLRenderer::~TerminalGLRenderer() {
  // Destroy() must be called with a current context; if it was not we still
  // null out the handles so we don't try to use stale ones.
  Destroy();
}

bool TerminalGLRenderer::Init() {
  if (m_program != 0) {
    return true;
  }

#ifdef _WIN32
  // glewInit() must be called once per GL context. Requires a current context.
  static bool s_glewInitialized = false;
  if (!s_glewInitialized) {
    GLenum err = glewInit();
    if (err != GLEW_OK) {
      TLOG_ERROR() << "glewInit failed: " << glewGetErrorString(err) << std::endl;
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

  // Single-channel coverage atlas.
  m_atlasW = kAtlasSize;
  m_atlasH = kAtlasSize;
  glGenTextures(1, &m_atlasTex);
  glBindTexture(GL_TEXTURE_2D, m_atlasTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // Single-component texture: replicate red into rgb via swizzle is not needed
  // because the shader samples .r directly.
  std::vector<unsigned char> zero(static_cast<size_t>(m_atlasW) * m_atlasH, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_atlasW, m_atlasH, 0, GL_RED,
               GL_UNSIGNED_BYTE, zero.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  m_penX = m_penY = m_shelfH = 0;

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

void TerminalGLRenderer::SetFonts(const wxFont &regular, const wxFont &bold,
                                  const wxFont &underlined,
                                  const wxFont &boldUnderlined) {
  m_fontRegular = regular;
  m_fontBold = bold;
  m_fontUnderlined = underlined;
  m_fontBoldUnderlined = boldUnderlined;
}

void TerminalGLRenderer::ClearGlyphCache() {
  m_glyphs.clear();
  m_penX = m_penY = m_shelfH = 0;
  if (m_atlasTex) {
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);
    std::vector<unsigned char> zero(static_cast<size_t>(m_atlasW) * m_atlasH,
                                    0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_atlasW, m_atlasH, GL_RED,
                    GL_UNSIGNED_BYTE, zero.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  }
}

std::uint64_t TerminalGLRenderer::MakeKey(char32_t ch, bool bold,
                                          bool underlined) {
  std::uint64_t key = static_cast<std::uint64_t>(ch);
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
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

void TerminalGLRenderer::AddGlyph(char32_t ch, bool bold, bool underlined,
                                  int x, int y, int cellW, int cellH,
                                  const wxColour &fg) {
  const GlyphInfo &g = GetGlyph(ch, bold, underlined, cellW, cellH);
  if (!g.ok)
    return;

  // The atlas tile is a full-cell render, so stretch it across the whole
  // logical cell rectangle. Sizing the quad to the cell (not the tile's
  // rasterized pixel size) keeps glyphs aligned with their background rects
  // independent of how ConvertToImage reports the DPI-scaled tile size.
  PushQuad(m_glyphVerts, static_cast<float>(x), static_cast<float>(y),
           static_cast<float>(cellW), static_cast<float>(cellH), g.u0, g.v0,
           g.u1, g.v1, fg.Red() / 255.0f, fg.Green() / 255.0f,
           fg.Blue() / 255.0f, 1.0f);
}

const TerminalGLRenderer::GlyphInfo &
TerminalGLRenderer::GetGlyph(char32_t ch, bool bold, bool underlined, int cellW,
                             int cellH) {
  static const GlyphInfo kBlank{};

  // Whitespace contributes no glyph quad.
  if (ch == U' ' || ch == 0 || ch == U'\t') {
    return kBlank;
  }

  const std::uint64_t key = MakeKey(ch, bold, underlined);
  auto it = m_glyphs.find(key);
  if (it != m_glyphs.end()) {
    return it->second;
  }

  const wxFont &font = (bold && underlined)  ? m_fontBoldUnderlined
                       : bold                ? m_fontBold
                       : underlined          ? m_fontUnderlined
                                             : m_fontRegular;

  const int cellWPhys =
      std::max(1, static_cast<int>(std::ceil(cellW * m_scale)));
  const int cellHPhys =
      std::max(1, static_cast<int>(std::ceil(cellH * m_scale)));

  GlyphInfo info =
      RasterizeAndPack(ch, font, cellWPhys, cellHPhys, m_scale);
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
  wxBitmap bmp(cellWPhys, cellHPhys, 32);
  {
    wxMemoryDC dc(bmp);
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.Clear();

    // Measure the base font's natural line height in device pixels, then scale
    // the point size so the rendered line height matches the physical tile.
    dc.SetFont(font);
    const int baseLineH = std::max(1, dc.GetCharHeight());
    const double basePt = font.GetFractionalPointSize();
    wxFont scaledFont = font;
    if (basePt > 0 && baseLineH != cellHPhys) {
      scaledFont.SetFractionalPointSize(basePt *
                                        (double(cellHPhys) / baseLineH));
    }

    dc.SetFont(scaledFont);
    dc.SetTextForeground(*wxWHITE);
    dc.SetTextBackground(*wxBLACK);
    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    dc.DrawText(wxString(wxUniChar(ch)), 0, 0);
  }

  wxImage img = bmp.ConvertToImage();
  if (!img.IsOk()) {
    return info;
  }

  const int w = img.GetWidth();
  const int h = img.GetHeight();
  // Coverage = luminance of the white-on-black render (R channel is enough for
  // grayscale anti-aliasing).
  std::vector<unsigned char> coverage(static_cast<size_t>(w) * h, 0);
  const unsigned char *rgb = img.GetData();
  for (int py = 0; py < h; ++py) {
    for (int px = 0; px < w; ++px) {
      const size_t srcIdx = (static_cast<size_t>(py) * w + px) * 3;
      coverage[static_cast<size_t>(py) * w + px] = rgb[srcIdx]; // red == cov
    }
  }

  // Shelf-pack into the atlas.
  if (m_penX + w > m_atlasW) {
    m_penX = 0;
    m_penY += m_shelfH;
    m_shelfH = 0;
  }
  if (m_penY + h > m_atlasH) {
    // Atlas exhausted. For a terminal's bounded glyph set this is very rare;
    // log once and skip rather than evicting.
    TLOG_DEBUG() << "GL glyph atlas full; skipping glyph U+" << std::hex
                 << static_cast<uint32_t>(ch) << std::dec << std::endl;
    return info;
  }

  glBindTexture(GL_TEXTURE_2D, m_atlasTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, m_penX, m_penY, w, h, GL_RED,
                  GL_UNSIGNED_BYTE, coverage.data());
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
    glDrawArrays(GL_TRIANGLES, 0,
                 static_cast<GLsizei>(m_solidVerts.size()));
  }

  // Pass 2: textured glyph quads.
  if (!m_glyphVerts.empty()) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);
    glUniform1i(m_locSampler, 0);
    glUniform1i(m_locUseTexture, 1);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_glyphVerts.size() * sizeof(Vertex)),
                 m_glyphVerts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0,
                 static_cast<GLsizei>(m_glyphVerts.size()));
  }

  glBindVertexArray(0);
  glUseProgram(0);
}

#endif // USE_OPENGL
