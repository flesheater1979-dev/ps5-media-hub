/*
 * evo_hui_complex - Thai, Indic and CJK text for the kit screens. See
 * evo_hui_complex.hpp.
 */
#include "evo_hui_complex.hpp"

#include "evo_boot_log.h"
#include "evo_data_path.h"
#include "evo_rmlui_bundle.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include <harfbuzz/hb.h>

#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace evo::kit
{

namespace
{

/* Glyphs are drawn at this pixel size with this distance-field spread, then
 * scaled to the size asked for, as the baked fonts are. */
constexpr float kPixels = 44.0f;
constexpr int kRange = 6;
constexpr std::size_t kShapeCacheMax = 1024;

enum Group
{
    g_none = -1,
    g_thai,
    g_devanagari,
    g_bengali,
    g_tamil,
    g_telugu,
    g_malayalam,
    g_cjk,
    g_count
};

enum FaceId
{
    f_thai,
    f_devanagari,
    f_bengali,
    f_tamil,
    f_telugu,
    f_malayalam,
    f_sc,
    f_jp,
    f_kr,
    f_count
};

struct FaceSpec
{
    const char *bundle; /* embedded in the binary, or null */
    const char *file;   /* under /data/evoplayer/fonts, or null */
    const char *lang;
};

const FaceSpec kFaces[f_count] = {
    {"fonts/NotoSansThai-Regular.ttf", nullptr, "th"},
    {"fonts/NotoSansDevanagari-Regular.ttf", nullptr, "hi"},
    {"fonts/NotoSansBengali-Regular.ttf", nullptr, "bn"},
    {"fonts/NotoSansTamil-Regular.ttf", nullptr, "ta"},
    {"fonts/NotoSansTelugu-Regular.ttf", nullptr, "te"},
    {"fonts/NotoSansMalayalam-Regular.ttf", nullptr, "ml"},
    {nullptr, "NotoSansSC-Regular.otf", "zh"},
    {nullptr, "NotoSansJP-Regular.otf", "ja"},
    {nullptr, "NotoSansKR-Regular.otf", "ko"},
};

Group group_of(char32_t c)
{
    if (c < 0x0900)
        return g_none;
    if (c <= 0x097F)
        return g_devanagari;
    if (c <= 0x09FF)
        return g_bengali;
    if (c >= 0x0B80 && c <= 0x0BFF)
        return g_tamil;
    if (c >= 0x0C00 && c <= 0x0C7F)
        return g_telugu;
    if (c >= 0x0D00 && c <= 0x0D7F)
        return g_malayalam;
    if (c >= 0x0E00 && c <= 0x0E7F)
        return g_thai;
    if ((c >= 0x1100 && c <= 0x11FF) || (c >= 0x2E80 && c <= 0x2FDF) ||
        (c >= 0x3000 && c <= 0x30FF) || (c >= 0x3130 && c <= 0x318F) ||
        (c >= 0x31C0 && c <= 0x31FF) || (c >= 0x3400 && c <= 0x4DBF) ||
        (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) ||
        (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF) ||
        (c >= 0x20000 && c <= 0x2FA1F))
        return g_cjk;
    if (c >= 0xA8E0 && c <= 0xA8FF)
        return g_devanagari;
    return g_none;
}

bool is_kana(char32_t c)
{
    return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF);
}

bool is_hangul(char32_t c)
{
    return (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F) ||
           (c >= 0xAC00 && c <= 0xD7AF);
}

struct Face
{
    bool tried = false;
    bool ok = false;
    const unsigned char *data = nullptr;
    std::vector<unsigned char> owned;
    hb_blob_t *blob = nullptr;
    hb_face_t *face = nullptr;
    hb_font_t *font = nullptr;
    FT_Face ft = nullptr;
    int upem = 1000;
};

struct ShapedGlyph
{
    std::uint32_t id;
    float advance, dx, dy; /* in ems; y up */
};

struct Shaped
{
    std::vector<ShapedGlyph> glyphs;
    float width = 0.0f; /* ems */
};

struct Cached
{
    std::uint16_t x = 0, y = 0, w = 0, h = 0;
    float xoff = 0.0f, yoff = 0.0f;
};

} // namespace

struct ComplexText::Impl
{
    Face faces[f_count];
    FT_Library ft_lib = nullptr;
    int cjk_present = -1; /* -1 unknown, else a bit per CJK face */
    time_t cjk_probed = 0;
    std::unordered_map<std::string, Shaped> shapes;
    std::unordered_map<std::uint64_t, Cached> glyphs;
    int pen_x = 1, pen_y = 1, shelf = 0;
    bool overflow = false;

    static std::string cjk_path(int face)
    {
        return std::string(evo_data_path("fonts/")) + kFaces[face].file;
    }

    /* Which CJK files are in the data folder. Looked for again each second
     * until one turns up (the data root can move when the sandbox opens). */
    int cjk_mask()
    {
        const time_t now = time(nullptr);
        if (cjk_present > 0 || (cjk_present == 0 && now == cjk_probed))
            return cjk_present;
        cjk_probed = now;
        cjk_present = 0;
        for (int f = f_sc; f <= f_kr; ++f)
        {
            const std::string path = cjk_path(f);
            if (std::FILE *fp = std::fopen(path.c_str(), "rb"))
            {
                std::fclose(fp);
                cjk_present |= 1 << (f - f_sc);
            }
        }
        evo_boot_log("hui: CJK fonts in %s: mask %d", evo_data_path("fonts/"), cjk_present);
        return cjk_present;
    }

    bool available(Group g)
    {
        if (g == g_cjk)
            return cjk_mask() != 0;
        return true; /* the others are in the binary */
    }

    /* The face a run of one group is drawn with. */
    int face_for(Group g, std::u32string_view run)
    {
        switch (g)
        {
        case g_thai:
            return f_thai;
        case g_devanagari:
            return f_devanagari;
        case g_bengali:
            return f_bengali;
        case g_tamil:
            return f_tamil;
        case g_telugu:
            return f_telugu;
        case g_malayalam:
            return f_malayalam;
        default:
            break;
        }
        int want = f_sc;
        for (const char32_t c : run)
        {
            if (is_kana(c))
            {
                want = f_jp;
                break;
            }
            if (is_hangul(c))
                want = f_kr;
        }
        const int mask = cjk_mask();
        if (mask & (1 << (want - f_sc)))
            return want;
        for (int f = f_sc; f <= f_kr; ++f)
            if (mask & (1 << (f - f_sc)))
                return f;
        return -1;
    }

    Face *load(int id)
    {
        Face &f = faces[id];
        if (f.tried)
            return f.ok ? &f : nullptr;
        f.tried = true;
        std::size_t size = 0;
        if (kFaces[id].bundle)
        {
            const EvoRmlBundleFile *b = evo_rmlui_bundle_find(kFaces[id].bundle);
            if (b)
            {
                f.data = b->data;
                size = b->size;
            }
        }
        else
        {
            const std::string path = cjk_path(id);
            if (std::FILE *fp = std::fopen(path.c_str(), "rb"))
            {
                std::fseek(fp, 0, SEEK_END);
                const long n = std::ftell(fp);
                std::fseek(fp, 0, SEEK_SET);
                if (n > 0)
                {
                    f.owned.resize(static_cast<std::size_t>(n));
                    if (std::fread(f.owned.data(), 1, f.owned.size(), fp) == f.owned.size())
                    {
                        f.data = f.owned.data();
                        size = f.owned.size();
                    }
                }
                std::fclose(fp);
            }
        }
        if (!f.data || size == 0)
        {
            evo_boot_log("hui: complex font %d unavailable", id);
            return nullptr;
        }
        if (!ft_lib)
        {
            if (FT_Init_FreeType(&ft_lib) != 0)
            {
                ft_lib = nullptr;
                evo_boot_log("hui: complex glyphs: FreeType did not start");
                return nullptr;
            }
            FT_Int spread = kRange;
            FT_Property_Set(ft_lib, "sdf", "spread", &spread);
        }
        if (FT_New_Memory_Face(ft_lib, f.data, static_cast<FT_Long>(size), 0, &f.ft) != 0 ||
            FT_Set_Pixel_Sizes(f.ft, 0, static_cast<FT_UInt>(kPixels)) != 0)
        {
            f.ft = nullptr;
            evo_boot_log("hui: complex font %d unreadable", id);
            return nullptr;
        }
        f.blob = hb_blob_create(reinterpret_cast<const char *>(f.data),
                                static_cast<unsigned>(size), HB_MEMORY_MODE_READONLY, nullptr,
                                nullptr);
        f.face = hb_face_create(f.blob, 0);
        f.font = hb_font_create(f.face);
        f.upem = static_cast<int>(hb_face_get_upem(f.face));
        if (f.upem <= 0)
            f.upem = 1000;
        hb_font_set_scale(f.font, f.upem, f.upem);
        f.ok = true;
        evo_boot_log("hui: complex font %d loaded (%u bytes)", id, static_cast<unsigned>(size));
        return &f;
    }

    const Shaped *shape(int id, std::u32string_view run)
    {
        std::string key(1, static_cast<char>(id));
        key.append(reinterpret_cast<const char *>(run.data()), run.size() * sizeof(char32_t));
        const auto hit = shapes.find(key);
        if (hit != shapes.end())
            return &hit->second;
        Face *f = load(id);
        if (!f)
            return nullptr;
        if (shapes.size() >= kShapeCacheMax)
            shapes.clear();
        if (id >= f_sc)
        {
            /* Chinese, Japanese, Korean: one glyph a character, no shaping. The
             * Hangul shaper would take syllables apart into jamo, which a subset
             * font cannot put back together. */
            Shaped &plain = shapes[key];
            const float em = static_cast<float>(f->upem);
            for (const char32_t c : run)
            {
                hb_codepoint_t gid = 0;
                if (!hb_font_get_nominal_glyph(f->font, c, &gid))
                    gid = 0;
                ShapedGlyph g;
                g.id = gid;
                g.advance = static_cast<float>(hb_font_get_glyph_h_advance(f->font, gid)) / em;
                g.dx = g.dy = 0.0f;
                plain.glyphs.push_back(g);
                plain.width += g.advance;
            }
            return &plain;
        }
        hb_buffer_t *buf = hb_buffer_create();
        hb_buffer_add_utf32(buf, reinterpret_cast<const std::uint32_t *>(run.data()),
                            static_cast<int>(run.size()), 0, static_cast<int>(run.size()));
        hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
        hb_buffer_set_language(buf, hb_language_from_string(kFaces[id].lang, -1));
        hb_buffer_guess_segment_properties(buf);
        hb_shape(f->font, buf, nullptr, 0);
        unsigned count = 0;
        const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
        const hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);
        Shaped &out = shapes[key];
        const float em = static_cast<float>(f->upem);
        for (unsigned i = 0; i < count; ++i)
        {
            ShapedGlyph g;
            g.id = info[i].codepoint;
            g.advance = static_cast<float>(pos[i].x_advance) / em;
            g.dx = static_cast<float>(pos[i].x_offset) / em;
            g.dy = static_cast<float>(pos[i].y_offset) / em;
            out.glyphs.push_back(g);
            out.width += g.advance;
        }
        hb_buffer_destroy(buf);
        return &out;
    }

    /* The glyph's rectangle in the atlas, drawing it there first if new. Null
     * when the atlas is full (the glyph is skipped until the atlas is reset). */
    const Cached *glyph(ComplexText &owner, int id, std::uint32_t gid)
    {
        const std::uint64_t key = (static_cast<std::uint64_t>(id) << 32) | gid;
        const auto hit = glyphs.find(key);
        if (hit != glyphs.end())
            return &hit->second;
        Face *f = load(id);
        if (!f || overflow)
            return nullptr;
        Cached c;
        int w = 0, h = 0, xo = 0, yo = 0, pitch = 0;
        const unsigned char *sdf = nullptr;
        /* FreeType's SDF has the kit's encoding: 128 on the outline, 128/spread
         * a pixel falling outward. Origin is the pen on the baseline. */
        if (FT_Load_Glyph(f->ft, gid, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) == 0 &&
            FT_Render_Glyph(f->ft->glyph, FT_RENDER_MODE_SDF) == 0)
        {
            const FT_Bitmap &bm = f->ft->glyph->bitmap;
            sdf = bm.buffer;
            w = static_cast<int>(bm.width);
            h = static_cast<int>(bm.rows);
            pitch = bm.pitch;
            xo = f->ft->glyph->bitmap_left;
            yo = -f->ft->glyph->bitmap_top;
        }
        if (sdf && w > 0 && h > 0)
        {
            if (pen_x + w + 1 > kAtlas)
            {
                pen_x = 1;
                pen_y += shelf + 1;
                shelf = 0;
            }
            if (pen_y + h + 1 > kAtlas)
            {
                overflow = true;
                return nullptr;
            }
            for (int row = 0; row < h; ++row)
                std::memcpy(&owner.atlas_[static_cast<std::size_t>((pen_y + row) * kAtlas + pen_x)],
                            sdf + row * pitch, static_cast<std::size_t>(w));
            c.x = static_cast<std::uint16_t>(pen_x);
            c.y = static_cast<std::uint16_t>(pen_y);
            c.w = static_cast<std::uint16_t>(w);
            c.h = static_cast<std::uint16_t>(h);
            c.xoff = static_cast<float>(xo);
            c.yoff = static_cast<float>(yo);
            pen_x += w + 1;
            shelf = std::max(shelf, h);
            owner.dirty_ = true;
        }
        return &(glyphs[key] = c);
    }

    void reset(ComplexText &owner)
    {
        glyphs.clear();
        std::fill(owner.atlas_.begin(), owner.atlas_.end(), static_cast<std::uint8_t>(0));
        pen_x = pen_y = 1;
        shelf = 0;
        overflow = false;
        owner.dirty_ = true;
    }
};

ComplexText::ComplexText()
    : impl_(new Impl), atlas_(static_cast<std::size_t>(kAtlas) * kAtlas, 0)
{
}

ComplexText::~ComplexText()
{
    for (Face &f : impl_->faces)
    {
        if (f.ft)
            FT_Done_Face(f.ft);
        if (f.font)
            hb_font_destroy(f.font);
        if (f.face)
            hb_face_destroy(f.face);
        if (f.blob)
            hb_blob_destroy(f.blob);
    }
    if (impl_->ft_lib)
        FT_Done_FreeType(impl_->ft_lib);
    delete impl_;
}

void ComplexText::end_frame()
{
    if (impl_->overflow)
    {
        evo_boot_log("hui: complex glyph atlas full, clearing");
        impl_->reset(*this);
    }
}

bool ComplexText::handles(char32_t codepoint) const
{
    const Group g = group_of(codepoint);
    return g != g_none && impl_->available(g);
}

float ComplexText::sdf_range(float size) const
{
    return static_cast<float>(kRange) * size / kPixels;
}

namespace
{

/* Calls fn(group, begin, end) for each stretch of one group in `run`. */
template <typename Fn> void for_each_group(std::u32string_view run, Fn &&fn)
{
    std::size_t i = 0;
    while (i < run.size())
    {
        const Group g = group_of(run[i]);
        std::size_t j = i + 1;
        while (j < run.size() && group_of(run[j]) == g)
            ++j;
        fn(g, i, j);
        i = j;
    }
}

} // namespace

float ComplexText::measure(std::u32string_view run, float size)
{
    float width = 0.0f;
    for_each_group(run,
                   [&](Group g, std::size_t a, std::size_t b)
                   {
                       const std::u32string_view part = run.substr(a, b - a);
                       const int face = impl_->face_for(g, part);
                       const Shaped *s = face >= 0 ? impl_->shape(face, part) : nullptr;
                       if (s)
                           width += s->width * size;
                   });
    return width;
}

float ComplexText::layout(std::u32string_view run, float pen_x, float y, float size,
                          std::vector<hui::gfx::GlyphQuad> &quads)
{
    const float scale = size / kPixels;
    const float inv = 1.0f / static_cast<float>(kAtlas);
    float pen = pen_x;
    for_each_group(
        run,
        [&](Group g, std::size_t a, std::size_t b)
        {
            const std::u32string_view part = run.substr(a, b - a);
            const int face = impl_->face_for(g, part);
            const Shaped *s = face >= 0 ? impl_->shape(face, part) : nullptr;
            if (!s)
                return;
            for (const ShapedGlyph &sg : s->glyphs)
            {
                const Cached *c = impl_->glyph(*this, face, sg.id);
                if (c && c->w > 0 && c->h > 0)
                {
                    hui::gfx::GlyphQuad q;
                    q.x0 = pen + sg.dx * size + c->xoff * scale;
                    q.y0 = y - sg.dy * size + c->yoff * scale;
                    q.x1 = q.x0 + static_cast<float>(c->w) * scale;
                    q.y1 = q.y0 + static_cast<float>(c->h) * scale;
                    q.u0 = static_cast<float>(c->x) * inv;
                    q.v0 = static_cast<float>(c->y) * inv;
                    q.u1 = static_cast<float>(c->x + c->w) * inv;
                    q.v1 = static_cast<float>(c->y + c->h) * inv;
                    q.complex = true;
                    quads.push_back(q);
                }
                pen += sg.advance * size;
            }
        });
    return pen - pen_x;
}

} // namespace evo::kit
