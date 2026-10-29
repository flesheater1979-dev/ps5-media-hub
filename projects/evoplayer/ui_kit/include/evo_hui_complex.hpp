/*
 * evo_hui_complex - Thai, Indic and CJK text for the kit screens.
 *
 * The kit's baked atlases cannot draw scripts that need shaping. This is the
 * kit's ComplexText provider: HarfBuzz shapes a run, the glyphs are drawn as
 * signed distance fields (stb_truetype) into an atlas that grows as text
 * appears, and the kit draws that atlas like any other font. Thai and the
 * Indic scripts use Noto faces embedded in the binary; Chinese, Japanese and
 * Korean use the Noto Sans CJK files in /data/ps5-media-hub/fonts when they are
 * there (4-8 MB each, too big to embed).
 */
#pragma once

#include "gfx/font.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace evo::kit
{

class ComplexText final : public hui::gfx::ComplexText
{
  public:
    static constexpr int kAtlas = 2048; /* square, one byte a pixel */

    ComplexText();
    ~ComplexText() override;
    ComplexText(const ComplexText &) = delete;
    ComplexText &operator=(const ComplexText &) = delete;

    /* The atlas, to upload once into a font slot (blank at first). */
    const std::uint8_t *atlas() const
    {
        return atlas_.data();
    }
    void set_texture(std::uint32_t handle)
    {
        texture_ = handle;
    }
    /* True once after glyphs were added: upload atlas() before drawing. */
    bool take_dirty()
    {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }
    /* Call after the frame is drawn: a full atlas is cleared for the next one. */
    void end_frame();

    bool handles(char32_t codepoint) const override;
    float measure(std::u32string_view run, float size) override;
    float layout(std::u32string_view run, float pen_x, float y, float size,
                 std::vector<hui::gfx::GlyphQuad> &quads) override;
    std::uint32_t texture() const override
    {
        return texture_;
    }
    float sdf_range(float size) const override;

  private:
    struct Impl;
    Impl *impl_;
    std::vector<std::uint8_t> atlas_;
    std::uint32_t texture_ = 0;
    bool dirty_ = false;
};

} // namespace evo::kit
