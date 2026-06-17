#pragma once

// This header is only meaningful when the OpenGL rendering path is enabled at
// configure time (cmake -DUSE_OPENGL=ON). When USE_OPENGL is 0 the whole class
// compiles to nothing so the rest of the code base can include it freely.
#ifndef USE_OPENGL
#define USE_OPENGL 0
#endif

#if USE_OPENGL

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>

/**
 * @brief GPU glyph-atlas renderer for the terminal grid.
 *
 * The renderer keeps a single-channel (GL_R8) texture atlas of rasterized
 * glyphs and batches the whole visible screen into two draw calls per frame:
 * one for the solid background/cursor/border quads and one for the textured
 * glyph quads. Glyphs are rasterized lazily on first sighting using the
 * wxWidgets font engine (wxMemoryDC -> wxImage) so the metrics match the
 * legacy wxDC path, then cached for the lifetime of the atlas.
 *
 * All coordinates passed to Add* are in logical pixels (the same units the
 * wxDC path uses); the renderer multiplies by the DPI scale supplied to
 * BeginFrame to position quads in the physical-pixel GL drawable.
 *
 * A current GL context must be bound by the caller before any method that
 * touches GL state (Init/BeginFrame/EndFrame/AddGlyph/ClearGlyphCache).
 */
class TerminalGLRenderer {
public:
  TerminalGLRenderer() = default;
  ~TerminalGLRenderer();

  TerminalGLRenderer(const TerminalGLRenderer &) = delete;
  TerminalGLRenderer &operator=(const TerminalGLRenderer &) = delete;

  /// Compile the shader program and allocate the atlas texture + vertex
  /// buffers. Requires a current GL context. Returns false (and logs) on
  /// failure; subsequent frames become no-ops.
  bool Init();
  void Destroy();
  bool IsInitialized() const { return m_program != 0; }

  /// Provide the four font variants used to rasterize glyphs. Does not clear
  /// already-cached glyphs; call ClearGlyphCache() after a real font change.
  void SetFonts(const wxFont &regular, const wxFont &bold,
                const wxFont &underlined, const wxFont &boldUnderlined);

  /// Drop every cached glyph and reset the atlas (e.g. after a font/theme
  /// change or a cell-size change). Requires a current GL context.
  void ClearGlyphCache();

  /// Start a new frame: set the viewport + orthographic projection for a
  /// physW x physH physical drawable and clear it to @p bg.
  void BeginFrame(int physW, int physH, double scale, const wxColour &bg);

  /// Queue a solid colored rectangle (logical pixel coordinates).
  void AddSolidRect(int x, int y, int w, int h, const wxColour &color);

  /// Queue a glyph drawn at the top-left of a logical cell rectangle.
  void AddGlyph(char32_t ch, bool bold, bool underlined, int x, int y,
                int cellW, int cellH, const wxColour &fg);

  /// Upload the queued geometry and issue the batched draw calls.
  void EndFrame();

private:
  struct Vertex {
    float x, y; // position (physical pixels)
    float u, v; // atlas texture coordinates
    float r, g, b, a;
  };

  struct GlyphInfo {
    float u0{0}, v0{0}, u1{0}, v1{0};
    int w{0}, h{0}; // physical pixel size of the tile
    bool ok{false}; // false => blank / failed, do not draw
  };

  // Build a key combining codepoint and style bits.
  static std::uint64_t MakeKey(char32_t ch, bool bold, bool underlined);

  // Look up (or rasterize + insert) a glyph for the given cell size.
  const GlyphInfo &GetGlyph(char32_t ch, bool bold, bool underlined,
                            int cellW, int cellH);

  // Rasterize a single glyph into a coverage buffer and pack it into the
  // atlas. Returns the resulting GlyphInfo.
  GlyphInfo RasterizeAndPack(char32_t ch, const wxFont &font, int cellWPhys,
                             int cellHPhys, double scale);

  void PushQuad(std::vector<Vertex> &out, float x, float y, float w, float h,
                float u0, float v0, float u1, float v1, float r, float g,
                float b, float a);

  // GL handles (0 == uninitialized).
  unsigned int m_program{0};
  unsigned int m_vao{0};
  unsigned int m_vbo{0};
  unsigned int m_atlasTex{0};
  int m_locOrtho{-1};
  int m_locUseTexture{-1};
  int m_locSampler{-1};

  // Atlas packing state (shelf packer).
  int m_atlasW{0};
  int m_atlasH{0};
  int m_penX{0};
  int m_penY{0};
  int m_shelfH{0};

  // Per-frame state.
  double m_scale{1.0};
  float m_ortho[16] = {0};
  float m_bgR{0}, m_bgG{0}, m_bgB{0};
  std::vector<Vertex> m_solidVerts;
  std::vector<Vertex> m_glyphVerts;
  std::size_t m_solidVertsHWM{0};
  std::size_t m_glyphVertsHWM{0};

  // Fonts + glyph cache.
  wxFont m_fontRegular;
  wxFont m_fontBold;
  wxFont m_fontUnderlined;
  wxFont m_fontBoldUnderlined;
  std::unordered_map<std::uint64_t, GlyphInfo> m_glyphs;
};

#endif // USE_OPENGL
