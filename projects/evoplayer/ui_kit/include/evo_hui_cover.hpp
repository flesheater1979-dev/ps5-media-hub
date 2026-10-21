/* Cover-fit UV crop shared by every screen that draws provider artwork. */
#pragma once

#include "gfx/draw_list.hpp"

#include <cstdint>

namespace evo::kit
{

/* A cover-fit crop of a w x h picture into a box of the given aspect. */
inline ::hui::gfx::Rect cover_uv(int w, int h, float box_w, float box_h)
{
    if (w <= 0 || h <= 0 || box_w <= 0 || box_h <= 0)
        return ::hui::gfx::kFullUv;
    const float img = static_cast<float>(w) / static_cast<float>(h);
    const float box = box_w / box_h;
    if (img > box)
    {
        const float f = box / img;
        return {(1.0f - f) * 0.5f, 0.0f, f, 1.0f};
    }
    const float f = img / box;
    return {0.0f, (1.0f - f) * 0.5f, 1.0f, f};
}

/* Home artwork: a picture close to the box's shape is cover-cropped. A portrait poster in a wide
 * box would lose most of itself to the crop, so it is shown whole, centred, on a plate. */
inline void draw_art_fit(::hui::gfx::DrawList &list, std::uint32_t tex, int w, int h, const ::hui::gfx::Rect &box,
                         float radius, const ::hui::gfx::Color &tint, const ::hui::gfx::Color &plate_top,
                         const ::hui::gfx::Color &plate_bottom)
{
    const bool portrait_in_wide = w > 0 && h > 0 && box.h > 0 &&
                                  (static_cast<float>(w) / static_cast<float>(h)) < 0.6f * (box.w / box.h);
    if (!portrait_in_wide)
    {
        list.image(tex, box, cover_uv(w, h, box.w, box.h), tint, radius);
        return;
    }
    list.gradient_rect(box, radius, plate_top, plate_bottom);
    const float ww = box.h * static_cast<float>(w) / static_cast<float>(h);
    const ::hui::gfx::Rect img{box.x + (box.w - ww) * 0.5f, box.y, ww, box.h};
    list.image(tex, img, ::hui::gfx::kFullUv, tint, radius * 0.5f);
}

} // namespace evo::kit
