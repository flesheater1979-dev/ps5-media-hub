/*
 * evo_hui_settings_preview.cpp - the Settings preview card (#158). See the header.
 *
 * Shared pieces first (card frame, header, tag, chips, mock still), then one
 * function per preview kind, then the dispatcher at the bottom. A kind with no
 * drawing yet falls back to the INFO card.
 */
#include "evo_hui_settings_preview.hpp"
#include "ui/widgets.hpp"

#include "evo_rmlui_bridge.h"

#include <algorithm>
#include <cmath>

namespace evo::kit
{

using ::hui::gfx::Align;
using ::hui::gfx::Color;
using ::hui::gfx::DrawList;
using ::hui::gfx::Rect;
namespace ui = ::hui::ui;

namespace
{

const Color kWhite = Color::rgb(0xffffff);
const Color kInk = Color::rgb(0x0b0d16);

/* The card is 600 x ~640 (see kPreviewW in evo_hui_settings.cpp); content sits in a 24 px inset. */
struct Area
{
    Rect box;
    float x, w;  /* content column */
    float top;   /* first free y below the header */
};

Area begin_card(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;
    list.rounded_rect(box, 24, kWhite.with_alpha(0.05f));
    list.bordered_rect(box, 24, Color::rgb(0x000000, 0.0f), 1.0f, p.border);
    Area a{box, box.x + 24.0f, box.w - 48.0f, box.y + 112.0f};
    ui::text(list, fonts.semibold, "PREVIEW", a.x, box.y + 40, 16, p.text_muted, Align::left, 2.0f);
    ui::text(list, fonts.display, fonts.display.font->fit(pv.title, 32, a.w), a.x, box.y + 84, 32, kWhite);
    return a;
}

/* A small pill over a frame: dark for the baseline frame, accent for the result. */
void tag(DrawList &list, const Context &ctx, const Rect &frame, const std::string &label, bool accent)
{
    const ui::Fonts &fonts = ctx.fonts;
    const float lw = fonts.semibold.measure(label, 16) + 24.0f;
    Rect t{frame.x + 10, frame.y + 10, lw, 28};
    list.rounded_rect(t, 14, accent ? ctx.palette.accent : Color::rgb(0x000000, 0.6f));
    ui::text(list, fonts.semibold, label, t.cx(), t.cy() + 6, 16, accent ? kInk : kWhite, Align::center);
}

/* A frame border: thin for the baseline, accent for the result. */
void frame_border(DrawList &list, const Context &ctx, const Rect &r, bool accent)
{
    list.bordered_rect(r, 12, Color::rgb(0x000000, 0.0f), accent ? 2.0f : 1.0f, accent ? ctx.palette.accent : ctx.palette.border);
}

/* Choice chips, the highlighted one filled; wraps to a second row. Returns the y below them. */
float chips(DrawList &list, const Context &ctx, const Area &a, float y, const std::vector<std::string> &labels, int selected)
{
    const ui::Fonts &fonts = ctx.fonts;
    ui::Painter paint(list, fonts, ctx.palette.kit_theme());
    float cx = a.x;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        const float cw = fonts.semibold.measure(labels[i], 18) + 36.0f;
        if (cx + cw > a.x + a.w && cx > a.x)
        {
            cx = a.x;
            y += 46.0f;
        }
        paint.chip({cx, y, cw, 36}, labels[i], static_cast<int>(i) == selected ? 1.0f : 0.0f, {});
        cx += cw + 10.0f;
    }
    return y + 36.0f;
}

/* Wrapped benefit text under the content. */
void benefit(DrawList &list, const Context &ctx, const Area &a, float y, const std::string &text)
{
    ui::paragraph(list, ctx.fonts.regular, text, a.x, y, 20, a.w, 28, ctx.palette.text_faint, 5);
}

/* The labels the chips show: the setting's own choices, or OFF / ON for a toggle. */
std::vector<std::string> chip_labels(const SettingsPreview &pv)
{
    if (!pv.choices.empty())
    {
        std::vector<std::string> out;
        for (const std::string &c : pv.choices)
            out.push_back(ui::upper(c));
        return out;
    }
    return {"OFF", "ON"};
}

/* Mock still: sky, moon, three ridges, foreground. `dark` 0..1 pulls the lifted
 * blacks toward true black; `coarse` overlays a pixel grid for the low-res look. */
void mock_scene(DrawList &list, const Rect &r, float dark, bool coarse, bool hdr = false)
{
    auto mix = [&](uint32_t lifted, uint32_t deep) {
        Color a = Color::rgb(lifted), b = Color::rgb(deep);
        return Color{a.r + (b.r - a.r) * dark, a.g + (b.g - a.g) * dark, a.b + (b.b - a.b) * dark, 1.0f};
    };
    list.push_clip(r);
    if (hdr)
        list.gradient_rect(r, 0, Color::rgb(0x384a75), Color::rgb(0xe8995a));
    else
        list.gradient_rect(r, 0, mix(0x2c3550, 0x0a1226), mix(0xc9884f, 0xb8793f));
    const float mx = r.x + r.w * 0.72f, my = r.y + r.h * 0.30f, mr = r.h * 0.09f;
    if (hdr)
        list.glow({mx - mr, my - mr, mr * 2.0f, mr * 2.0f}, mr, 18.0f, Color::rgb(0xffffff, 0.85f));
    list.circle(mx, my, mr, hdr ? Color::rgb(0xffffff) : Color::rgb(0xf6e3b0));
    const uint32_t lift[3] = {0x4a4e5c, 0x3e424f, 0x353946}, deep[3] = {0x1b1722, 0x120f17, 0x09070c};
    const float cx[3] = {0.18f, 0.48f, 0.80f}, top[3] = {0.40f, 0.30f, 0.46f}, wd[3] = {0.6f, 0.5f, 0.55f};
    for (int i = 0; i < 3; ++i)
    {
        float xy[6] = {r.x + r.w * (cx[i] - wd[i] * 0.5f), r.y + r.h, r.x + r.w * cx[i], r.y + r.h * (0.30f + top[i] * 0.4f),
                       r.x + r.w * (cx[i] + wd[i] * 0.5f), r.y + r.h};
        list.polygon(xy, 3, mix(lift[i], deep[i]));
    }
    list.rounded_rect({r.x, r.y + r.h * 0.86f, r.w, r.h * 0.14f}, 0, mix(0x42434d, 0x050407));
    if (coarse)
        for (float gy = r.y; gy < r.y + r.h; gy += 9.0f)
            for (float gx = r.x + (static_cast<int>((gy - r.y) / 9.0f) & 1) * 9.0f; gx < r.x + r.w; gx += 18.0f)
                list.rounded_rect({gx, gy, 9.0f, 9.0f}, 0, Color::rgb(0x808080, 0.10f));
    list.pop_clip();
}

/* Two 16:9 frames side by side starting at a.top. Returns them and the y below. */
float frame_pair(const Area &a, Rect &left, Rect &right)
{
    const float sw = (a.w - 16.0f) * 0.5f, sh = sw * 9.0f / 16.0f;
    left = {a.x, a.top, sw, sh};
    right = {a.x + sw + 16.0f, a.top, sw, sh};
    return a.top + sh;
}

/* ---- INFO: icon and the benefit in plain words. Also the fallback. ---- */
void draw_info(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    Rect tile{a.x, a.top, 96, 96};
    list.rounded_rect(tile, 24, kWhite.with_alpha(0.08f));
    if (!pv.icon.empty())
        if (uint32_t icon = ctx.textures.icon(pv.icon))
            list.image(icon, {tile.x + 20, tile.y + 20, 56, 56}, ::hui::gfx::kFullUv, kWhite);
    if (!pv.choices.empty() || pv.toggle)
        chips(list, ctx, a, a.top + 124.0f, chip_labels(pv), pv.index);
    benefit(list, ctx, a, a.top + (pv.choices.empty() && !pv.toggle ? 148.0f : 208.0f), pv.benefit);
}

/* ---- DEEP BLACKS: OFF next to the highlighted choice. ---- */
void draw_deep_blacks(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    Rect l, r;
    const float bottom = frame_pair(a, l, r);
    const int lvl = std::min(std::max(pv.index, 0), 2);
    mock_scene(list, l, 0.0f, false);
    mock_scene(list, r, 0.5f * lvl, false);
    frame_border(list, ctx, l, false);
    frame_border(list, ctx, r, true);
    const auto labels = chip_labels(pv);
    tag(list, ctx, l, "OFF", false);
    tag(list, ctx, r, labels[std::min<size_t>(lvl, labels.size() - 1)], true);
    const float y = chips(list, ctx, a, bottom + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- UPSCALING: a blocky low-res crop next to the highlighted choice. ---- */
void draw_upscaling(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    Rect l, r;
    const float bottom = frame_pair(a, l, r);
    const int lvl = std::max(pv.index, 0);
    mock_scene(list, l, 0.0f, true);
    mock_scene(list, r, 0.0f, lvl == 0);
    frame_border(list, ctx, l, false);
    frame_border(list, ctx, r, true);
    const auto labels = chip_labels(pv);
    tag(list, ctx, l, "OFF", false);
    tag(list, ctx, r, labels[std::min<size_t>(lvl, labels.size() - 1)], true);
    const float y = chips(list, ctx, a, bottom + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 1. AI NETWORK: four rising bars for DETAIL and thinner GPU COST. ---- */
void draw_ai_network(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;

    ui::text(list, fonts.semibold, "DETAIL", a.x, a.top + 16.0f, 16, p.text_muted, Align::left, 2.0f);
    const float base1 = a.top + 105.0f;
    list.line(a.x, base1 + 1.0f, a.x + a.w, base1 + 1.0f, 1.0f, kWhite.with_alpha(0.08f));
    const float h1[4] = {25.0f, 45.0f, 65.0f, 85.0f};

    ui::text(list, fonts.semibold, "GPU COST", a.x, a.top + 135.0f, 16, p.text_muted, Align::left, 2.0f);
    const float base2 = a.top + 215.0f;
    list.line(a.x, base2 + 1.0f, a.x + a.w, base2 + 1.0f, 1.0f, kWhite.with_alpha(0.08f));
    const float h2[4] = {14.0f, 30.0f, 55.0f, 85.0f};

    const float col_w = a.w / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        const bool sel = (i == pv.index);
        const Color col = sel ? p.accent : kWhite.with_alpha(0.18f);
        const float cx = a.x + static_cast<float>(i) * col_w + col_w * 0.5f;

        Rect r1{cx - 24.0f, base1 - h1[i], 48.0f, h1[i]};
        list.rounded_rect(r1, 6, col);
        if (sel)
            list.glow(r1, 6, 10.0f, p.accent.with_alpha(0.4f));

        Rect r2{cx - 12.0f, base2 - h2[i], 24.0f, h2[i]};
        list.rounded_rect(r2, 4, col);
        if (sel)
            list.glow(r2, 4, 8.0f, p.accent.with_alpha(0.4f));
    }

    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, base2 + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 2. HDR OUTPUT: SDR vs bloomed HDR10 scene. ---- */
void draw_hdr(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    Rect l, r;
    const float bottom = frame_pair(a, l, r);
    /* Real choices are AUTO (MATCH CONTENT) and OFF (TONE-MAP TO SDR): decide by the label. */
    const auto hdr_labels = chip_labels(pv);
    const bool is_hdr = pv.index >= 0 && pv.index < static_cast<int>(hdr_labels.size())
                            ? hdr_labels[pv.index].find("OFF") == std::string::npos
                            : pv.index == 1;
    mock_scene(list, l, 0.0f, false, false);
    mock_scene(list, r, 0.0f, false, is_hdr);
    frame_border(list, ctx, l, false);
    frame_border(list, ctx, r, is_hdr);
    tag(list, ctx, l, "SDR", false);
    tag(list, ctx, r, is_hdr ? "HDR10" : "SDR", is_hdr);
    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, bottom + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 3. MOTION SMOOTHING: 24 fps steps vs smoothed trail. ---- */
void draw_motion(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const Palette &p = ctx.palette;
    const float sh = 68.0f;
    Rect s1{a.x, a.top, a.w, sh};
    Rect s2{a.x, a.top + sh + 16.0f, a.w, sh};

    list.rounded_rect(s1, 12, kWhite.with_alpha(0.04f));
    list.rounded_rect(s2, 12, kWhite.with_alpha(0.04f));
    frame_border(list, ctx, s1, false);
    tag(list, ctx, s1, "24 FPS", false);

    const float span_x0 = s1.x + 150.0f;
    const float span_w = s1.w - 180.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float bx = span_x0 + (span_w / 3.0f) * static_cast<float>(i);
        list.rounded_rect({bx - 6.0f, s1.cy() - 18.0f, 12.0f, 36.0f}, 4, kWhite);
    }

    const bool smoothed = (pv.index > 0);
    frame_border(list, ctx, s2, smoothed);
    tag(list, ctx, s2, smoothed ? "SMOOTHED" : "OFF", smoothed);

    if (!smoothed)
    {
        for (int i = 0; i < 4; ++i)
        {
            const float bx = span_x0 + (span_w / 3.0f) * static_cast<float>(i);
            list.rounded_rect({bx - 6.0f, s2.cy() - 18.0f, 12.0f, 36.0f}, 4, kWhite.with_alpha(0.8f));
        }
    }
    else
    {
        for (int i = 0; i < 12; ++i)
        {
            const float t = static_cast<float>(i) / 11.0f;
            const float bx = span_x0 + span_w * t;
            const float alpha = 0.12f + 0.88f * t;
            list.rounded_rect({bx - 5.0f, s2.cy() - 18.0f, 10.0f, 36.0f}, 3, p.accent.with_alpha(alpha));
            if (i == 11)
                list.glow({bx - 5.0f, s2.cy() - 18.0f, 10.0f, 36.0f}, 3, 8.0f, p.accent.with_alpha(0.5f));
        }
    }

    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, s2.y + s2.h + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 4. REFRESH RATE (120 Hz output): frame cadence timeline. ---- */
void draw_refresh(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;

    ui::text(list, fonts.semibold, "24 FPS FILM", a.x, a.top + 14.0f, 14, p.text_muted, Align::left, 2.0f);
    const float row1_y = a.top + 24.0f;
    for (int i = 0; i < 24; ++i)
    {
        const float tx = a.x + (a.w - 2.0f) * (static_cast<float>(i) / 23.0f);
        list.line(tx, row1_y, tx, row1_y + 10.0f, 1.5f, kWhite.with_alpha(0.6f));
    }

    ui::text(list, fonts.semibold, "120 HZ OUTPUT", a.x, a.top + 50.0f, 14, p.text_muted, Align::left, 2.0f);
    const float row2_y = a.top + 60.0f;
    for (int i = 0; i < 120; ++i)
    {
        const float tx = a.x + (a.w - 1.0f) * (static_cast<float>(i) / 119.0f);
        list.line(tx, row2_y, tx, row2_y + 8.0f, 1.0f, kWhite.with_alpha(0.35f));
    }

    const bool is_60hz = (pv.index == 0);
    const bool is_120hz = (pv.index > 0);
    const Color col_60 = is_60hz ? p.accent : p.text_muted;
    const Color col_120 = is_120hz ? p.accent : p.text_muted;

    ui::text(list, fonts.semibold, "60 HZ: UNEVEN", a.x, a.top + 94.0f, 14, col_60, Align::left, 2.0f);
    const int cadence_60[4] = {2, 3, 2, 3};
    const float block_y1 = a.top + 104.0f;
    const float block_gap = 4.0f;
    const float avail_w = a.w - 3.0f * block_gap;
    float cur_x1 = a.x;
    for (int k = 0; k < 4; ++k)
    {
        const float bw = avail_w * (static_cast<float>(cadence_60[k]) / 10.0f);
        Rect br{cur_x1, block_y1, bw, 30.0f};
        list.rounded_rect(br, 6, is_60hz ? p.accent.with_alpha(0.25f) : kWhite.with_alpha(0.06f));
        list.bordered_rect(br, 6, Color::rgb(0,0), is_60hz ? 2.0f : 1.0f, is_60hz ? p.accent : p.border);
        ui::text(list, fonts.semibold, std::to_string(cadence_60[k]), br.cx(), br.cy() + 5.0f, 14, is_60hz ? kWhite : p.text_faint, Align::center);
        cur_x1 += bw + block_gap;
    }

    ui::text(list, fonts.semibold, "120 HZ: EVEN", a.x, a.top + 158.0f, 14, col_120, Align::left, 2.0f);
    const float block_y2 = a.top + 168.0f;
    float cur_x2 = a.x;
    for (int k = 0; k < 4; ++k)
    {
        const float bw = avail_w * 0.25f;
        Rect br{cur_x2, block_y2, bw, 30.0f};
        list.rounded_rect(br, 6, is_120hz ? p.accent.with_alpha(0.25f) : kWhite.with_alpha(0.06f));
        list.bordered_rect(br, 6, Color::rgb(0,0), is_120hz ? 2.0f : 1.0f, is_120hz ? p.accent : p.border);
        ui::text(list, fonts.semibold, "5", br.cx(), br.cy() + 5.0f, 14, is_120hz ? kWhite : p.text_faint, Align::center);
        cur_x2 += bw + block_gap;
    }

    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, block_y2 + 30.0f + 24.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 5. MATCH VIDEO RATE: 60 Hz judder vs 1:1 smooth timeline. ---- */
void draw_match_rate(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;

    const bool on = (pv.index == 1);
    const Color col_off = !on ? p.accent : p.text_muted;
    const Color col_on = on ? p.accent : p.text_muted;

    ui::text(list, fonts.semibold, "OFF: 60 HZ OUTPUT (3:2 PULLDOWN)", a.x, a.top + 16.0f, 14, col_off, Align::left, 2.0f);
    const float block_gap = 4.0f;
    const float avail_w = a.w - 3.0f * block_gap - 80.0f;
    const int cadence[4] = {2, 3, 2, 3};
    const float y1 = a.top + 28.0f;
    float cx1 = a.x;
    for (int k = 0; k < 4; ++k)
    {
        const float bw = avail_w * (static_cast<float>(cadence[k]) / 10.0f);
        Rect br{cx1, y1, bw, 32.0f};
        list.rounded_rect(br, 6, !on ? p.accent.with_alpha(0.25f) : kWhite.with_alpha(0.06f));
        list.bordered_rect(br, 6, Color::rgb(0,0), !on ? 2.0f : 1.0f, !on ? p.accent : p.border);
        ui::text(list, fonts.semibold, std::to_string(cadence[k]), br.cx(), br.cy() + 5.0f, 14, !on ? kWhite : p.text_faint, Align::center);
        cx1 += bw + block_gap;
    }
    Rect tag1{cx1 + 8.0f, y1 + 2.0f, 68.0f, 28.0f};
    list.rounded_rect(tag1, 14, !on ? Color::rgb(0xff6b4a) : Color::rgb(0x000000, 0.5f));
    ui::text(list, fonts.semibold, "JUDDER", tag1.cx(), tag1.cy() + 5.0f, 13, kWhite, Align::center);

    ui::text(list, fonts.semibold, "ON: 1:1 MATCHED RATE (24P)", a.x, a.top + 90.0f, 14, col_on, Align::left, 2.0f);
    const float y2 = a.top + 102.0f;
    float cx2 = a.x;
    for (int k = 0; k < 4; ++k)
    {
        const float bw = avail_w * 0.25f;
        Rect br{cx2, y2, bw, 32.0f};
        list.rounded_rect(br, 6, on ? p.accent.with_alpha(0.25f) : kWhite.with_alpha(0.06f));
        list.bordered_rect(br, 6, Color::rgb(0,0), on ? 2.0f : 1.0f, on ? p.accent : p.border);
        ui::text(list, fonts.semibold, "1:1", br.cx(), br.cy() + 5.0f, 14, on ? kWhite : p.text_faint, Align::center);
        cx2 += bw + block_gap;
    }
    Rect tag2{cx2 + 8.0f, y2 + 2.0f, 68.0f, 28.0f};
    list.rounded_rect(tag2, 14, on ? p.accent : Color::rgb(0x000000, 0.5f));
    ui::text(list, fonts.semibold, "SMOOTH", tag2.cx(), tag2.cy() + 5.0f, 13, on ? kInk : kWhite, Align::center);

    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, y2 + 32.0f + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 6. ASPECT RATIO: 16:9 screen with fit, fill or stretch picture. ---- */
void draw_aspect(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const float sw = 384.0f, sh = 216.0f;
    Rect screen{a.x + (a.w - sw) * 0.5f, a.top, sw, sh};
    list.rounded_rect(screen, 12, Color::rgb(0x050508));
    frame_border(list, ctx, screen, true);

    int mode = pv.index;
    if (pv.index < static_cast<int>(pv.choices.size()))
    {
        const std::string u = ui::upper(pv.choices[pv.index]);
        if (u.find("FIT") != std::string::npos)
            mode = 0;
        else if (u.find("FILL") != std::string::npos)
            mode = 1;
        else if (u.find("STRETCH") != std::string::npos)
            mode = 2;
    }

    list.push_clip(screen);
    Rect pic;
    float rx = 0.0f, ry = 0.0f;
    if (mode == 0) // FIT
    {
        const float pw = sh * (4.0f / 3.0f);
        pic = {screen.x + (sw - pw) * 0.5f, screen.y, pw, sh};
        rx = ry = sh * 0.22f;
    }
    else if (mode == 1) // FILL
    {
        const float ph = sw * (3.0f / 4.0f);
        pic = {screen.x, screen.y + (sh - ph) * 0.5f, sw, ph};
        rx = ry = ph * 0.22f;
    }
    else // STRETCH
    {
        pic = screen;
        ry = sh * 0.22f;
        rx = ry * (16.0f / 9.0f) / (4.0f / 3.0f);
    }

    const float hhalf = pic.h * 0.5f;
    list.gradient_rect({pic.x, pic.y, pic.w, hhalf}, 0, Color::rgb(0x1e3c72), Color::rgb(0x2a5298));
    list.gradient_rect({pic.x, pic.y + hhalf, pic.w, hhalf}, 0, Color::rgb(0x2a5298), Color::rgb(0x0f2027));

    float xy[64];
    for (int i = 0; i < 32; ++i)
    {
        const float th = static_cast<float>(i) * (6.2831853f / 32.0f);
        xy[i * 2] = pic.cx() + rx * std::cos(th);
        xy[i * 2 + 1] = pic.cy() + ry * std::sin(th);
    }
    list.polygon(xy, 32, Color::rgb(0xffe066));
    list.pop_clip();

    const auto labels = chip_labels(pv);
    const std::string tag_lbl = (pv.index < static_cast<int>(labels.size())) ? labels[pv.index] : "FIT";
    tag(list, ctx, screen, tag_lbl, true);

    const float y = chips(list, ctx, a, screen.y + screen.h + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 7. AUTO SUBTITLES: sample line shown vs empty scene note. ---- */
void draw_sub_auto(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;
    const float sw = 400.0f, sh = 225.0f;
    Rect frame{a.x + (a.w - sw) * 0.5f, a.top, sw, sh};
    mock_scene(list, frame, 0.0f, false);
    const bool on = (pv.index == 1);
    frame_border(list, ctx, frame, on);
    tag(list, ctx, frame, on ? "ON" : "OFF", on);

    if (on)
    {
        const char *line = "You were right about the lighthouse.";
        const float tx = frame.cx(), ty = frame.y + frame.h - 24.0f;
        ui::text(list, fonts.semibold, line, tx + 1.5f, ty + 1.5f, 22, Color::rgb(0x000000, 0.9f), Align::center);
        ui::text(list, fonts.semibold, line, tx - 1.0f, ty - 1.0f, 22, Color::rgb(0x000000, 0.9f), Align::center);
        ui::text(list, fonts.semibold, line, tx, ty, 22, kWhite, Align::center);
    }
    else
    {
        ui::text(list, fonts.semibold, "NO SUBTITLES", frame.cx(), frame.y + frame.h - 24.0f, 16, p.text_muted, Align::center);
    }

    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, frame.y + frame.h + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 8. SUBTITLE FONT: sample line in the chosen typeface. ---- */
void draw_sub_font(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const float sw = 400.0f, sh = 225.0f;
    Rect frame{a.x + (a.w - sw) * 0.5f, a.top, sw, sh};
    mock_scene(list, frame, 0.0f, false);
    frame_border(list, ctx, frame, true);

    const auto labels = chip_labels(pv);
    const std::string tag_lbl = (pv.index < static_cast<int>(labels.size())) ? labels[pv.index] : "STANDARD";
    tag(list, ctx, frame, tag_lbl, true);

    const char *line = "You were right about the lighthouse.";
    const float tx = frame.cx(), ty = frame.y + frame.h - 24.0f;
    const ui::FontRef *face = &fonts.regular;
    float fsize = 26.0f;
    float tracking = 0.0f;
    if (pv.index == 1)
        face = &fonts.display;
    else if (pv.index == 2)
        face = &fonts.semibold;
    else if (pv.index == 3)
    {
        face = &fonts.regular;
        fsize = 22.0f;
        tracking = -0.5f;
    }

    ui::text(list, *face, line, tx + 1.5f, ty + 1.5f, fsize, Color::rgb(0x000000, 0.9f), Align::center, tracking);
    ui::text(list, *face, line, tx, ty, fsize, kWhite, Align::center, tracking);

    const float y = chips(list, ctx, a, frame.y + frame.h + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 9. SECONDARY POSITION: stacked above primary vs top of screen. ---- */
void draw_sub_pos(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;
    const float sw = 400.0f, sh = 225.0f;
    Rect frame{a.x + (a.w - sw) * 0.5f, a.top, sw, sh};
    mock_scene(list, frame, 0.0f, false);
    frame_border(list, ctx, frame, true);

    const auto labels = chip_labels(pv);
    const std::string tag_lbl = (pv.index < static_cast<int>(labels.size())) ? labels[pv.index] : "POSITION";
    tag(list, ctx, frame, tag_lbl, true);

    const char *line1 = "You were right about the lighthouse.";
    const char *line2 = "Tu avais raison pour le phare.";
    const float cx = frame.cx();
    const float l1_y = frame.y + frame.h - 22.0f;
    const float l2_y = (pv.index == 1) ? frame.y + 66.0f : frame.y + frame.h - 52.0f;

    ui::text(list, fonts.semibold, line2, cx + 1.5f, l2_y + 1.5f, 18, Color::rgb(0x000000, 0.9f), Align::center);
    ui::text(list, fonts.semibold, line2, cx, l2_y, 18, p.accent, Align::center);

    ui::text(list, fonts.semibold, line1, cx + 1.5f, l1_y + 1.5f, 22, Color::rgb(0x000000, 0.9f), Align::center);
    ui::text(list, fonts.semibold, line1, cx, l1_y, 22, kWhite, Align::center);

    const float y = chips(list, ctx, a, frame.y + frame.h + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 10. SECONDARY COLOUR: yellow, cyan, or white second line. ---- */
void draw_sub_colour(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const float sw = 400.0f, sh = 225.0f;
    Rect frame{a.x + (a.w - sw) * 0.5f, a.top, sw, sh};
    mock_scene(list, frame, 0.0f, false);
    frame_border(list, ctx, frame, true);

    const auto labels = chip_labels(pv);
    const std::string tag_lbl = (pv.index < static_cast<int>(labels.size())) ? labels[pv.index] : "COLOUR";
    tag(list, ctx, frame, tag_lbl, true);

    Color sec_color = Color::rgb(0xffd84a); // index 0 yellow
    if (pv.index == 1)
        sec_color = Color::rgb(0x4adfff); // index 1 cyan
    else if (pv.index == 2)
        sec_color = Color::rgb(0xffffff); // index 2 white

    const char *line1 = "You were right about the lighthouse.";
    const char *line2 = "Tu avais raison pour le phare.";
    const float cx = frame.cx();
    const float l2_y = frame.y + frame.h - 52.0f;
    const float l1_y = frame.y + frame.h - 22.0f;

    ui::text(list, fonts.semibold, line2, cx + 1.5f, l2_y + 1.5f, 18, Color::rgb(0x000000, 0.9f), Align::center);
    ui::text(list, fonts.semibold, line2, cx, l2_y, 18, sec_color, Align::center);

    ui::text(list, fonts.semibold, line1, cx + 1.5f, l1_y + 1.5f, 22, Color::rgb(0x000000, 0.9f), Align::center);
    ui::text(list, fonts.semibold, line1, cx, l1_y, 22, kWhite, Align::center);

    const float y = chips(list, ctx, a, frame.y + frame.h + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 11. THEME: mini screen mock + palette swatch strip. ---- */
void draw_theme(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const Palette &p = ctx.palette;

    const size_t idx = std::min<size_t>(std::max(pv.index, 0), pv.choice_bg.empty() ? 0 : pv.choice_bg.size() - 1);
    const uint32_t bg_hex = (!pv.choice_bg.empty()) ? pv.choice_bg[idx] : 0x080d16;
    const uint32_t acc_hex = (!pv.choice_accent.empty()) ? pv.choice_accent[idx] : 0xffcd00;
    const Color t_bg = Color::rgb(bg_hex);
    const Color t_acc = Color::rgb(acc_hex);

    Rect mock{a.x + (a.w - 380.0f) * 0.5f, a.top, 380.0f, 160.0f};
    list.rounded_rect(mock, 14, t_bg);
    list.bordered_rect(mock, 14, Color::rgb(0,0), 1.5f, t_acc.with_alpha(0.5f));

    list.rounded_rect({mock.x + 18, mock.y + 14, 80, 8}, 4, kWhite.with_alpha(0.7f));
    list.line(mock.x + 18, mock.y + 30, mock.x + mock.w - 18, mock.y + 30, 1.0f, kWhite.with_alpha(0.1f));

    const float ry0 = mock.y + 38.0f;
    for (int r = 0; r < 3; ++r)
    {
        Rect row_r{mock.x + 18, ry0 + static_cast<float>(r) * 38.0f, mock.w - 36, 32};
        const bool row_sel = (r == 1);
        list.rounded_rect(row_r, 6, row_sel ? t_acc.with_alpha(0.20f) : kWhite.with_alpha(0.06f));
        if (row_sel)
        {
            list.bordered_rect(row_r, 6, Color::rgb(0,0), 1.5f, t_acc);
            Rect tog{row_r.x + row_r.w - 38, row_r.y + 7, 28, 18};
            list.rounded_rect(tog, 9, t_acc);
            list.circle(tog.x + 19, tog.cy(), 6, t_bg);
        }
        list.rounded_rect({row_r.x + 12, row_r.y + 11, row_sel ? 80.0f : 50.0f, 10}, 5, kWhite.with_alpha(row_sel ? 0.9f : 0.4f));
    }

    const float swatch_y = mock.y + mock.h + 20.0f;
    size_t n_choices = std::max<size_t>(pv.choices.size(), pv.choice_bg.size());
    if (n_choices == 0)
        n_choices = 4;
    const float step = std::min(52.0f, a.w / static_cast<float>(n_choices));
    const float strip_w = step * static_cast<float>(n_choices);
    const float sx0 = a.x + (a.w - strip_w) * 0.5f;
    for (size_t c = 0; c < n_choices; ++c)
    {
        const float cx = sx0 + static_cast<float>(c) * step + step * 0.5f;
        const uint32_t c_bg_hex = (c < pv.choice_bg.size()) ? pv.choice_bg[c] : 0x1a1a1a;
        const uint32_t c_acc_hex = (c < pv.choice_accent.size()) ? pv.choice_accent[c] : 0xffcd00;
        list.circle(cx - 7.0f, swatch_y + 12.0f, 7.0f, Color::rgb(c_bg_hex));
        list.circle(cx + 7.0f, swatch_y + 12.0f, 7.0f, Color::rgb(c_acc_hex));
        if (static_cast<int>(c) == pv.index)
            list.ring(cx, swatch_y + 12.0f, 18.0f, 2.0f, p.accent);
    }

    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, swatch_y + 36.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 12. CONTROLLER LIGHTBAR: DualSense top-down outline. ---- */
void draw_lightbar(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const Palette &p = ctx.palette;
    const float cx = a.x + a.w * 0.5f, cy = a.top + 80.0f;

    list.rounded_rect({cx - 95.0f, cy - 10.0f, 40.0f, 85.0f}, 16, Color::rgb(0x181a20));
    list.rounded_rect({cx + 95.0f - 40.0f, cy - 10.0f, 40.0f, 85.0f}, 16, Color::rgb(0x181a20));

    Rect body{cx - 80.0f, cy - 45.0f, 160.0f, 95.0f};
    list.rounded_rect(body, 22, Color::rgb(0x20242c));
    list.bordered_rect(body, 22, Color::rgb(0,0), 1.5f, Color::rgb(0x353b47));

    Rect pad{cx - 40.0f, cy - 38.0f, 80.0f, 48.0f};
    list.rounded_rect(pad, 8, Color::rgb(0x16181e));

    Rect strip{cx - 34.0f, cy - 42.0f, 68.0f, 6.0f};
    const bool on = (pv.index == 1);
    if (on)
    {
        list.glow(strip, 3, 14.0f, p.accent.with_alpha(0.85f));
        list.rounded_rect(strip, 3, p.accent);
    }
    else
    {
        list.rounded_rect(strip, 3, Color::rgb(0x4a505e));
    }

    list.circle(cx - 26.0f, cy + 22.0f, 13.0f, Color::rgb(0x16181e));
    list.circle(cx + 26.0f, cy + 22.0f, 13.0f, Color::rgb(0x16181e));

    tag(list, ctx, {cx - 105.0f, cy - 55.0f, 210.0f, 140.0f}, on ? "ON" : "OFF", on);

    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, cy + 95.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 13. OUTPUT CHANNELS: listener and speaker layout. ---- */
void draw_channels(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;
    const float cx = a.x + a.w * 0.5f, cy = a.top + 95.0f;

    list.circle(cx, cy, 14.0f, kWhite.with_alpha(0.25f));
    list.triangle({cx - 5.0f, cy - 20.0f, 10.0f, 8.0f}, kWhite.with_alpha(0.5f));

    int layout = 1; // 0 stereo, 1 5.1, 2 7.1; AUTO plays what the file has
    if (!pv.choices.empty() && pv.index < static_cast<int>(pv.choices.size()))
    {
        const std::string u = ui::upper(pv.choices[pv.index]);
        if (u.find("7.1") != std::string::npos)
            layout = 2;
        else if (u.find("5.1") != std::string::npos)
            layout = 1;
        else if (u.find("STEREO") != std::string::npos || u.find("2.0") != std::string::npos)
            layout = 0;
    }

    struct Spk { const char *name; float x, y; int min_layout; };
    const Spk spks[8] = {
        {"L", cx - 80.0f, cy - 60.0f, 0},
        {"R", cx + 80.0f, cy - 60.0f, 0},
        {"C", cx, cy - 70.0f, 1},
        {"SUB", cx, cy + 40.0f, 1},
        {"SL", cx - 100.0f, cy, 1},
        {"SR", cx + 100.0f, cy, 1},
        {"RL", cx - 70.0f, cy + 65.0f, 2},
        {"RR", cx + 70.0f, cy + 65.0f, 2},
    };

    for (const auto &s : spks)
    {
        const bool lit = (layout >= s.min_layout);
        const Color col = lit ? p.accent : kWhite.with_alpha(0.18f);
        if (lit)
            list.glow({s.x - 12.0f, s.y - 12.0f, 24.0f, 24.0f}, 12, 10.0f, p.accent.with_alpha(0.5f));
        list.circle(s.x, s.y, 11.0f, col);
        ui::text(list, fonts.semibold, s.name, s.x, s.y + 5.0f, 12, lit ? kInk : p.text_faint, Align::center);
    }

    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, cy + 105.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 14. AUDIO PASSTHROUGH: decoded PCM vs bitstream flow diagram. ---- */
void draw_passthrough(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;
    const float rw = a.w, rh = 68.0f;
    Rect row1{a.x, a.top, rw, rh};
    Rect row2{a.x, a.top + rh + 16.0f, rw, rh};
    const bool on = (pv.index == 1);

    // Row 1: OFF
    list.rounded_rect(row1, 12, kWhite.with_alpha(0.04f));
    frame_border(list, ctx, row1, !on);
    const Color col1 = (!on) ? p.accent : p.text_muted;
    const Color ink1 = (!on) ? kWhite : p.text_faint;

    Rect b1{row1.x + 16.0f, row1.cy() - 20.0f, 68.0f, 40.0f};
    list.rounded_rect(b1, 8, col1.with_alpha(!on ? 0.25f : 0.08f));
    list.bordered_rect(b1, 8, Color::rgb(0,0), 1.0f, col1);
    ui::text(list, fonts.semibold, "EVO", b1.cx(), b1.cy() + 6.0f, 16, ink1, Align::center);

    const float a1_start = b1.x + b1.w + 10.0f;
    const float a1_end = a1_start + 70.0f;
    list.line(a1_start, row1.cy(), a1_end, row1.cy(), 2.0f, col1);
    list.triangle({a1_end - 6.0f, row1.cy() - 6.0f, 8.0f, 12.0f}, col1, 0.0f, 1.5708f);
    ui::text(list, fonts.semibold, "DECODES", (a1_start + a1_end) * 0.5f, row1.cy() - 8.0f, 12, col1, Align::center);

    Rect b2{a1_end + 12.0f, row1.cy() - 20.0f, 95.0f, 40.0f};
    list.rounded_rect(b2, 8, col1.with_alpha(!on ? 0.25f : 0.08f));
    list.bordered_rect(b2, 8, Color::rgb(0,0), 1.0f, col1);
    ui::text(list, fonts.semibold, "2.0 PCM", b2.cx(), b2.cy() + 6.0f, 16, ink1, Align::center);

    const float a2_start = b2.x + b2.w + 10.0f;
    const float a2_end = a2_start + 50.0f;
    list.line(a2_start, row1.cy(), a2_end, row1.cy(), 2.0f, col1);
    list.triangle({a2_end - 6.0f, row1.cy() - 6.0f, 8.0f, 12.0f}, col1, 0.0f, 1.5708f);

    Rect b3{a2_end + 12.0f, row1.cy() - 20.0f, row1.x + row1.w - 16.0f - (a2_end + 12.0f), 40.0f};
    list.rounded_rect(b3, 8, col1.with_alpha(!on ? 0.25f : 0.08f));
    list.bordered_rect(b3, 8, Color::rgb(0,0), 1.0f, col1);
    ui::text(list, fonts.semibold, "RECEIVER", b3.cx(), b3.cy() + 6.0f, 16, ink1, Align::center);

    // Row 2: ON
    list.rounded_rect(row2, 12, kWhite.with_alpha(0.04f));
    frame_border(list, ctx, row2, on);
    const Color col2 = on ? p.accent : p.text_muted;
    const Color ink2 = on ? kWhite : p.text_faint;

    Rect r2_b1{row2.x + 16.0f, row2.cy() - 20.0f, 68.0f, 40.0f};
    list.rounded_rect(r2_b1, 8, col2.with_alpha(on ? 0.25f : 0.08f));
    list.bordered_rect(r2_b1, 8, Color::rgb(0,0), 1.0f, col2);
    ui::text(list, fonts.semibold, "EVO", r2_b1.cx(), r2_b1.cy() + 6.0f, 16, ink2, Align::center);

    const float r2_a_start = r2_b1.x + r2_b1.w + 14.0f;
    const float r2_a_end = row2.x + row2.w - 180.0f - 14.0f;
    list.line(r2_a_start, row2.cy(), r2_a_end, row2.cy(), 2.0f, col2);
    list.triangle({r2_a_end - 6.0f, row2.cy() - 6.0f, 8.0f, 12.0f}, col2, 0.0f, 1.5708f);
    ui::text(list, fonts.semibold, "BITSTREAM", (r2_a_start + r2_a_end) * 0.5f, row2.cy() - 8.0f, 12, col2, Align::center);

    Rect r2_b2{r2_a_end + 14.0f, row2.cy() - 20.0f, 166.0f, 40.0f};
    list.rounded_rect(r2_b2, 8, col2.with_alpha(on ? 0.25f : 0.08f));
    list.bordered_rect(r2_b2, 8, Color::rgb(0,0), 1.0f, col2);
    ui::text(list, fonts.semibold, "DOLBY / DTS", r2_b2.cx(), r2_b2.cy() + 6.0f, 16, ink2, Align::center);

    const auto labels = chip_labels(pv);
    const float y = chips(list, ctx, a, row2.y + row2.h + 28.0f, labels, pv.index);
    benefit(list, ctx, a, y + 40.0f, pv.benefit);
}

/* ---- 15. SURROUND TEST: 7.1 layout all lit with test action hint. ---- */
void draw_surround(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    Area a = begin_card(list, ctx, box, pv);
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;
    const float cx = a.x + a.w * 0.5f, cy = a.top + 95.0f;

    list.circle(cx, cy, 14.0f, kWhite.with_alpha(0.25f));
    list.triangle({cx - 5.0f, cy - 20.0f, 10.0f, 8.0f}, kWhite.with_alpha(0.5f));

    struct Spk { const char *name; float x, y; };
    const Spk spks[8] = {
        {"L", cx - 80.0f, cy - 60.0f},
        {"R", cx + 80.0f, cy - 60.0f},
        {"C", cx, cy - 70.0f},
        {"SUB", cx, cy + 40.0f},
        {"SL", cx - 100.0f, cy},
        {"SR", cx + 100.0f, cy},
        {"RL", cx - 70.0f, cy + 65.0f},
        {"RR", cx + 70.0f, cy + 65.0f},
    };

    for (const auto &s : spks)
    {
        list.glow({s.x - 12.0f, s.y - 12.0f, 24.0f, 24.0f}, 12, 10.0f, p.accent.with_alpha(0.5f));
        list.circle(s.x, s.y, 11.0f, p.accent);
        ui::text(list, fonts.semibold, s.name, s.x, s.y + 5.0f, 12, kInk, Align::center);
    }

    const float line_y = cy + 115.0f;
    ui::text(list, fonts.semibold, "Press X to play the test tones.", a.x + a.w * 0.5f, line_y, 18, p.text_muted, Align::center);
    benefit(list, ctx, a, line_y + 40.0f, pv.benefit);
}

} // namespace

void draw_settings_preview(DrawList &list, const Context &ctx, const Rect &box, const SettingsPreview &pv)
{
    switch (pv.kind)
    {
    case EVO_RMLUI_PREVIEW_DEEP_BLACKS:
        draw_deep_blacks(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_UPSCALING:
        draw_upscaling(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_AI_NETWORK:
        draw_ai_network(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_HDR:
        draw_hdr(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_MOTION:
        draw_motion(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_REFRESH:
        draw_refresh(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_MATCH_RATE:
        draw_match_rate(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_ASPECT:
        draw_aspect(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_SUB_AUTO:
        draw_sub_auto(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_SUB_FONT:
        draw_sub_font(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_SUB_POS:
        draw_sub_pos(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_SUB_COLOUR:
        draw_sub_colour(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_THEME:
        draw_theme(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_LIGHTBAR:
        draw_lightbar(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_CHANNELS:
        draw_channels(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_PASSTHROUGH:
        draw_passthrough(list, ctx, box, pv);
        break;
    case EVO_RMLUI_PREVIEW_SURROUND:
        draw_surround(list, ctx, box, pv);
        break;
    default: /* INFO and every kind that has no drawing yet */
        draw_info(list, ctx, box, pv);
        break;
    }

    /* The row's own subtext, in full: the narrower row list cuts it short. Pinned to the
     * card's foot, below every drawing above. */
    if (!pv.detail.empty())
    {
        const Palette &p = ctx.palette;
        const float x = box.x + 24.0f, w = box.w - 48.0f, y = box.y + box.h - 104.0f;
        list.rounded_rect({x, y - 30.0f, w, 1.0f}, 0, p.border);
        ui::text(list, ctx.fonts.semibold, "DETAILS", x, y, 14, p.text_muted, Align::left, 2.0f);
        ui::paragraph(list, ctx.fonts.regular, pv.detail, x, y + 28.0f, 18, w, 25, p.text_faint, 3);
    }
}

} // namespace evo::kit
