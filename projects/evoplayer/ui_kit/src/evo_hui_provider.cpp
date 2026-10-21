/*
 * evo_hui_provider.cpp - the provider screens on ps5-homebrew-ui.
 *
 * Content follows assets/rml/{mediaserver,iptv,xtream,provider_fallback}.rml:
 * header, a 4x2 card page, a detail pane (poster, or channel + guide), the
 * setup cards of an empty source, the options side panel, and the status strip
 * with the button hints. The host keeps the data and the focus (RmlUi's own
 * navigation); this only draws what it is given.
 */
#include "evo_hui_provider.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace evo::kit
{

using ::hui::gfx::Align;
using ::hui::gfx::Color;
using ::hui::gfx::DrawList;
using ::hui::gfx::Rect;
namespace tween = ::hui::tween;
namespace ui = ::hui::ui;

namespace
{
const Color kWhite = Color::rgb(0xffffff);
const Color kBlack = Color::rgb(0x000000);
const Color kRed = Color::rgb(0xff6b6b);
const Color kLive = Color::rgb(0xff4d4d);
const Color kAmber = Color::rgb(0xffb020);

constexpr int kCols = 8;                  /* a folder level: the shelf card, wrapped into 8 columns */
constexpr float kGridX = NavRail::kWidth + 64.0f;
constexpr float kGridTop = 214.0f;
constexpr float kCardW = 232.0f;
constexpr float kGap = 24.0f;
constexpr Rect kDetail{1288.0f, 214.0f, 536.0f, 700.0f};

/* Shelves: rows of posters. */
constexpr float kShelfTop = 214.0f;
constexpr float kShelfPitch = 404.0f;
constexpr float kPosterW = 176.0f;
constexpr float kPosterH = 264.0f;
constexpr float kCardStride = 198.0f;
constexpr float kGridPitch = 332.0f;       /* a folder level's row height: card + two text lines */

std::string str(const char *s) { return std::string(s ? s : ""); }

std::uint64_t hash_of(const std::string &s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (char c : s)
        h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    return h;
}

/* "/assets/icons/x.png" (an RmlUi path) as the kit's bundle path. */
std::string icon_path(const std::string &p)
{
    static const std::string prefix = "/assets/icons/";
    if (p.compare(0, prefix.size(), prefix) == 0)
        return "../icons/" + p.substr(prefix.size());
    return p;
}

/* A cover-fit crop of a w x h picture into a box of the given aspect. */
Rect cover_uv(int w, int h, float box_w, float box_h)
{
    if (w <= 0 || h <= 0)
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

/* The largest rect of the picture's aspect that fits inside `box`. */
Rect contain_rect(int w, int h, const Rect &box)
{
    if (w <= 0 || h <= 0)
        return box;
    const float img = static_cast<float>(w) / static_cast<float>(h);
    const float b = box.w / box.h;
    if (img > b)
    {
        const float hh = box.w / img;
        return {box.x, box.y + (box.h - hh) * 0.5f, box.w, hh};
    }
    const float ww = box.h * img;
    return {box.x + (box.w - ww) * 0.5f, box.y, ww, box.h};
}

const char *kind_word(int variant) { return variant == EVO_HUI_PROVIDER_XTREAM ? "CATEGORY" : "GROUP"; }

struct SetupCard
{
    const char *icon, *title, *desc, *action;
};
} // namespace

void ProviderGridScreen::set(const evo_hui_provider_params_t &p)
{
    variant_ = p.variant;
    name_ = str(p.provider_name);
    icon_ = str(p.provider_icon);
    options_hint_ = str(p.options_hint);
    crumb_ = str(p.breadcrumb);
    status_ = str(p.status);
    query_ = str(p.query);
    page_info_ = str(p.page_info);
    loading_ = p.loading != 0;
    tuning_ = p.tuning != 0;
    has_error_ = p.has_error != 0;
    empty_ = p.empty != 0;
    multi_page_ = p.has_multiple_pages != 0 && !p.shelves_active;
    folder_level_ = p.is_folder_level != 0;
    in_folder_ = p.in_folder != 0;
    count_ = p.count;
    focus_ = p.focus;
    rows_.clear();
    for (int i = 0; i < p.row_count && i < EVO_HUI_PROVIDER_ROWS; ++i)
    {
        const evo_hui_provider_row_t &r = p.rows[i];
        Row row;
        row.title = str(r.title);
        row.subtitle = str(r.subtitle);
        row.initial = str(r.initial);
        row.art_key = str(r.art_key);
        row.now = str(r.now);
        row.next = str(r.next);
        row.art = r.art;
        row.art_w = r.art_w;
        row.art_h = r.art_h;
        row.is_folder = r.is_folder != 0;
        row.played = r.played != 0;
        row.is_live = r.is_live != 0;
        row.progress = r.progress;
        rows_.push_back(std::move(row));
    }
    shelves_active_ = p.shelves_active != 0;
    item_menu_ = p.item_menu != 0;
    {
        const std::string bk = str(p.backdrop_key);
        if (bk != bd_key_)
            bd_alpha_ = 0.0f;
        bd_key_ = bk;
        bd_ = p.backdrop;
        bd_w_ = p.backdrop_w;
        bd_h_ = p.backdrop_h;
        logo_key_ = str(p.logo_key);
        logo_ = p.logo;
        logo_w_ = p.logo_w;
        logo_h_ = p.logo_h;
    }
    shelf_total_ = p.shelf_total;
    shelf_focus_abs_ = p.shelf_focus_abs;
    shelves_.clear();
    for (int i = 0; shelves_active_ && i < p.shelf_count && i < EVO_HUI_SHELVES; ++i)
    {
        const evo_hui_shelf_t &src = p.shelves[i];
        Shelf s;
        s.title = str(src.title);
        s.abs_index = src.abs_index;
        s.total = src.total_cards;
        s.first_card = src.first_card;
        s.focus_card = src.focus_card;
        s.scroll_card = src.scroll_card;
        for (int k = 0; k < src.card_count && k < EVO_HUI_SHELF_CARDS; ++k)
        {
            const evo_hui_provider_row_t &r = src.cards[k];
            Row row;
            row.title = str(r.title);
            row.subtitle = str(r.subtitle);
            row.initial = str(r.initial);
            row.art_key = str(r.art_key);
            row.art = r.art;
            row.art_w = r.art_w;
            row.art_h = r.art_h;
            row.is_folder = r.is_folder != 0;
            row.played = r.played != 0;
            row.is_live = r.is_live != 0;
            row.now = str(r.now);
            row.next = str(r.next);
            row.progress = r.progress;
            s.cards.push_back(std::move(row));
            s.see_all.push_back(r.see_all ? 1 : 0);
        }
        shelves_.push_back(std::move(s));
    }
    has_selected_ = p.has_selected != 0;
    sel_folder_ = p.selected_is_folder != 0;
    sel_played_ = p.selected_played != 0;
    sel_progress_ = p.selected_progress;
    sel_title_ = str(p.selected_title);
    sel_sub_ = str(p.selected_subtitle);
    sel_initial_ = str(p.selected_initial);
    sel_overview_ = str(p.selected_overview);
    sel_resume_ = str(p.selected_resume);
    sel_art_key_ = str(p.selected_art_key);
    sel_num_ = str(p.selected_num);
    sel_tech_ = str(p.selected_tech);
    sel_now_ = str(p.selected_now);
    sel_next_ = str(p.selected_next);
    epg_status_ = str(p.epg_status);
    epg_setup_ = p.epg_setup != 0;
    sel_art_ = p.selected_art;
    sel_art_w_ = p.selected_art_w;
    sel_art_h_ = p.selected_art_h;

    setup_configured_ = p.setup_configured != 0;
    setup_account_ = str(p.setup_account);
    setup_focus_ = p.setup_focus;

    const bool was_open = panel_open_;
    panel_open_ = p.panel_open != 0;
    if (panel_open_ && !was_open)
        panel_age_ = 0.0f;
    panel_crumb_ = str(p.panel_crumb);
    /* The kit's fonts have no single right-angle quote. */
    for (std::size_t at; (at = panel_crumb_.find("\xE2\x80\xBA")) != std::string::npos;)
        panel_crumb_.replace(at, 3, ">");
    panel_eyebrow_ = str(p.panel_eyebrow);
    panel_title_ = str(p.panel_title);
    panel_sub_ = str(p.panel_sub);
    panel_note_b_ = str(p.panel_note_b);
    panel_note_ = str(p.panel_note);
    panel_accept_ = str(p.panel_accept);
    panel_back_ = str(p.panel_back);
    panel_rows_.clear();
    for (int i = 0; i < p.panel_row_count && i < EVO_HUI_PROVIDER_PANEL_ROWS; ++i)
    {
        const evo_hui_panel_row_t &r = p.panel_rows[i];
        PanelRow row;
        row.title = str(r.title);
        row.detail = str(r.detail);
        row.badge = str(r.badge);
        row.icon = icon_path(str(r.icon));
        row.radio = r.radio != 0;
        row.on = r.on != 0;
        row.warn = r.warn != 0;
        row.chevron = r.chevron != 0;
        row.badge_live = r.badge_live != 0;
        row.focused = r.focused != 0;
        panel_rows_.push_back(std::move(row));
    }
}

void ProviderGridScreen::enter()
{
    age_ = 0.0f;
    ring_snapped_ = false;
    scroll_snapped_ = false;
}

bool ProviderGridScreen::setup_shown() const
{
    if (!empty_ || loading_ || !query_.empty())
        return false;
    if (variant_ == EVO_HUI_PROVIDER_LIVETV)
        return !in_folder_;
    return variant_ == EVO_HUI_PROVIDER_XTREAM;
}

Rect ProviderGridScreen::card_rect(int index) const
{
    const int col = index % kCols;
    const int row = index / kCols;
    return {kGridX + col * kCardStride, kGridTop + 26.0f + row * kGridPitch, kPosterW, kPosterH};
}

void ProviderGridScreen::update(float dt)
{
    age_ += dt;
    bd_alpha_ = std::min(1.0f, bd_alpha_ + dt * 2.2f);
    if (panel_open_)
        panel_age_ += dt;
    Rect target{};
    if (shelves_active_)
    {
        /* Ease the shelves toward the focus (vertical) and each row toward its own window (horizontal). */
        const float k = 1.0f - std::exp(-dt * 14.0f);
        /* Two rows fit on screen: never scroll so far that the last row sits alone at the top. */
        const float want = static_cast<float>(std::min(shelf_focus_abs_, std::max(0, shelf_total_ - 2)));
        if (!scroll_snapped_)
        {
            scroll_y_ = want;
            scroll_snapped_ = true;
        }
        scroll_y_ += (want - scroll_y_) * k;
        for (const Shelf &s : shelves_)
        {
            if (s.abs_index < 0 || s.abs_index >= 32)
                continue;
            float &sx = scroll_x_[s.abs_index];
            sx += (static_cast<float>(s.scroll_card) - sx) * k;
        }
        for (const Shelf &s : shelves_)
            if (s.focus_card >= 0)
                target = shelf_card_rect(s, s.focus_card);
    }
    else if (setup_shown() && setup_focus_ >= 0)
    {
        const int n = (variant_ == EVO_HUI_PROVIDER_XTREAM && setup_configured_) ? 3 : 2;
        const float w = 380.0f, gap = 28.0f;
        const float total = n * w + (n - 1) * gap;
        const float x0 = (kGridX + 1824.0f - total) * 0.5f;
        target = {x0 + setup_focus_ * (w + gap), 400.0f, w, 330.0f};
    }
    else if (focus_ >= 0 && focus_ < static_cast<int>(rows_.size()))
    {
        target = card_rect(focus_);
    }
    if (target.w <= 0.0f)
        return;
    if (!ring_snapped_)
    {
        ring_.snap(target);
        ring_snapped_ = true;
    }
    else
    {
        ring_.target(target);
    }
    ring_.update(dt, 22.0f);
}

Rect ProviderGridScreen::shelf_card_rect(const Shelf &s, int card_index) const
{
    const float sx = (s.abs_index >= 0 && s.abs_index < 32) ? scroll_x_[s.abs_index] : 0.0f;
    const float y = kShelfTop + (static_cast<float>(s.abs_index) - scroll_y_) * kShelfPitch + 44.0f;
    return {kGridX + (static_cast<float>(card_index) - sx) * kCardStride, y, kPosterW, kPosterH};
}

bool ProviderGridScreen::focus(FocusInfo *out) const
{
    if (shelves_active_ && !panel_open_)
    {
        for (const Shelf &s : shelves_)
        {
            if (s.focus_card < 0)
                continue;
            const int k = s.focus_card - s.first_card;
            out->id = "shelf-" + std::to_string(s.abs_index) + "-" + std::to_string(s.focus_card);
            out->text = (k >= 0 && k < static_cast<int>(s.cards.size())) ? s.cards[k].title : s.title;
            out->index = s.focus_card + 1;
            out->total = s.total;
            out->rect = shelf_card_rect(s, s.focus_card);
            return true;
        }
        return false;
    }
    if (panel_open_)
    {
        for (std::size_t i = 0; i < panel_rows_.size(); ++i)
        {
            out->focusable.push_back({"panel-" + std::to_string(i), panel_rows_[i].title});
            if (panel_rows_[i].focused)
            {
                out->id = "panel-" + std::to_string(i);
                out->text = panel_rows_[i].title;
                out->index = static_cast<int>(i) + 1;
                out->total = static_cast<int>(panel_rows_.size());
            }
        }
        return !out->id.empty();
    }
    if (setup_shown() && setup_focus_ >= 0)
    {
        static const char *kIds[3] = {"tv-card-setup-url", "tv-card-setup-usb", "tv-card-setup-signout"};
        out->id = kIds[std::min(2, setup_focus_)];
        out->text = out->id;
        out->index = setup_focus_ + 1;
        out->total = (variant_ == EVO_HUI_PROVIDER_XTREAM && setup_configured_) ? 3 : 2;
        return true;
    }
    if (focus_ < 0 || focus_ >= static_cast<int>(rows_.size()))
        return false;
    out->id = "row-" + std::to_string(focus_);
    out->text = rows_[focus_].title;
    out->index = focus_ + 1;
    out->total = static_cast<int>(rows_.size());
    out->rect = card_rect(focus_);
    for (std::size_t i = 0; i < rows_.size(); ++i)
        out->focusable.push_back({"row-" + std::to_string(i), rows_[i].title});
    return true;
}

void ProviderGridScreen::draw_art(DrawList &list, const Context &ctx, const Rect &r, float radius,
                                  const std::uint32_t *art, int w, int h, const std::string &key, bool folder,
                                  const std::string &initial, float initial_size, bool contain) const
{
    const Palette &pal = ctx.palette;
    const ui::Fonts &fonts = ctx.fonts;
    const std::uint32_t tex = art ? ctx.textures.pixels(art, w, h, hash_of(key)) : 0;
    if (tex && contain)
    {
        /* a channel logo: whole, on a plate */
        list.gradient_rect(r, radius, kWhite.with_alpha(0.10f), kWhite.with_alpha(0.04f));
        list.image(tex, contain_rect(w, h, r.inset(18)), ::hui::gfx::kFullUv, kWhite);
        return;
    }
    if (tex)
    {
        list.image(tex, r, cover_uv(w, h, r.w, r.h), kWhite, radius);
        return;
    }
    list.gradient_rect(r, radius, pal.accent_soft, pal.bg_bottom);
    if (folder)
    {
        const std::uint32_t icon = ctx.textures.icon("../icons/icon_folder.png");
        if (icon)
            list.image(icon, {r.cx() - 36, r.cy() - 36, 72, 72}, ::hui::gfx::kFullUv, kWhite.with_alpha(0.85f));
    }
    else if (!initial.empty())
    {
        ui::text(list, fonts.display, initial, r.cx(), r.cy() + initial_size * 0.35f, initial_size,
                 kWhite.with_alpha(0.7f), Align::center);
    }
}

/* One card, the same on a shelf and in a folder level: a poster, or a channel logo
 * on a plate with a LIVE pill. `cx`,`y` is its top-left; the title sits under it. */
void ProviderGridScreen::draw_poster_card(DrawList &list, const Context &ctx, const Row &row, bool see_all, float cx,
                                          float y) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const Rect poster{cx, y, kPosterW, kPosterH};
    list.shadow(poster, 14, 16, kBlack.with_alpha(0.35f));
    if (see_all)
    {
        list.gradient_rect(poster, 14, pal.accent_soft, pal.bg_bottom);
        const std::uint32_t chev = ctx.textures.icon("../icons/icon_chevron.png");
        if (chev)
            list.image(chev, {poster.cx() - 22, poster.cy() - 40, 44, 44}, ::hui::gfx::kFullUv, kWhite.with_alpha(0.85f));
        ui::text(list, fonts.semibold, "SEE ALL", poster.cx(), poster.cy() + 38, 18, kWhite, Align::center, 2.0f);
        return;
    }
    /* A channel: its logo whole, on a plate, in the same card size as a poster. */
    draw_art(list, ctx, poster, 14, row.art, row.art_w, row.art_h, row.art_key, row.is_folder, row.initial, 72.0f,
             row.is_live);
    if (row.is_live)
    {
        Rect pill{poster.x + 8, poster.y + 8, 48, 22};
        list.rounded_rect(pill, 11, kLive);
        ui::text(list, fonts.semibold, "LIVE", pill.cx(), pill.y + 16, 12, kWhite, Align::center, 1.2f);
    }
    if (row.played)
    {
        const float w2 = fonts.semibold.measure("WATCHED", 12, 1.2f) + 16;
        Rect pill{poster.x + 8, poster.y + 8, w2, 22};
        list.rounded_rect(pill, 11, kBlack.with_alpha(0.62f));
        ui::text(list, fonts.semibold, "WATCHED", pill.cx(), pill.y + 16, 12, kWhite, Align::center, 1.2f);
    }
    if (row.progress > 0)
    {
        Rect bar{poster.x + 8, poster.y + poster.h - 12, poster.w - 16, 5};
        list.rounded_rect(bar, 2.5f, kBlack.with_alpha(0.55f));
        list.rounded_rect({bar.x, bar.y, bar.w * std::min(100, row.progress) / 100.0f, bar.h}, 2.5f, pal.accent);
    }
    ui::text(list, fonts.semibold, fonts.semibold.font->fit(row.title, 18, kPosterW), cx + 2, poster.y + poster.h + 28, 18,
             kWhite.with_alpha(0.92f));
    const std::string &line2 = (row.is_live && !row.now.empty()) ? row.now : row.subtitle;
    ui::text(list, fonts.regular, fonts.regular.font->fit(line2, 15, kPosterW), cx + 2, poster.y + poster.h + 50, 15,
             row.is_live && !row.now.empty() ? pal.text_muted : pal.text_faint);
}

/* The focused card, top right: what it is, how far in (and what is on now). */
void ProviderGridScreen::draw_selected_line(DrawList &list, const Context &ctx) const
{
    if (!has_selected_)
        return;
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const float rx = 1824.0f;
    const std::uint32_t logo_tex = logo_ ? ctx.textures.pixels(logo_, logo_w_, logo_h_, hash_of(logo_key_)) : 0;
    if (logo_tex)
    {
        Rect lr = contain_rect(logo_w_, logo_h_, {rx - 440.0f, 40.0f, 440.0f, 96.0f});
        lr.x = rx - lr.w;
        list.image(logo_tex, lr, ::hui::gfx::kFullUv, kWhite);
    }
    else
        ui::text(list, fonts.display, fonts.display.font->fit(sel_title_, 32, 760.0f), rx, 128, 32, kWhite, Align::right);
    std::string line = sel_sub_;
    if (!sel_now_.empty())
    {
        line = "Now: " + sel_now_;
        if (!sel_next_.empty())
            line += "   Next: " + sel_next_;
    }
    if (!sel_resume_.empty())
        line += (line.empty() ? "" : "  -  ") + sel_resume_;
    else if (sel_played_)
        line += (line.empty() ? "" : "  -  ") + std::string("Watched");
    ui::text(list, fonts.regular, fonts.regular.font->fit(line, 19, 760.0f), rx, 162, 19, pal.text_muted, Align::right);
}

void ProviderGridScreen::draw_setup(DrawList &list, const Context &ctx) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const bool xt = variant_ == EVO_HUI_PROVIDER_XTREAM;
    const float cx = (kGridX + 1824.0f) * 0.5f;

    ui::text(list, fonts.display, xt ? "SETUP XTREAM CODES ACCOUNT" : "SELECT PLAYLIST SOURCE", cx, 270, 38, kWhite,
             Align::center);
    ui::text(list, fonts.regular,
             xt ? "Connect to your Xtream Codes subscription for Live TV, Movies & Series"
                : "Choose how you would like to load your television channels",
             cx, 312, 20, pal.text_muted, Align::center);
    if (!setup_account_.empty())
        ui::text(list, fonts.semibold, setup_account_, cx, 346, 17, pal.accent, Align::center, 0.8f);

    std::vector<SetupCard> cards;
    if (xt)
    {
        cards.push_back({"../icons/icon_settings.png", "ENTER SERVER & CREDENTIALS",
                         "Type http://user:pass@host:port or get.php URL", "SETUP ACCOUNT"});
        cards.push_back({"../icons/icon_browse_usb.png", "LOAD FROM USB", "Open an M3U exported from your Xtream account",
                         "READ USB"});
        if (setup_configured_)
            cards.push_back({"../icons/icon_power.png", "SIGN OUT", "Remove this account from EVO", "SIGN OUT"});
    }
    else
    {
        cards.push_back({"../icons/icon_settings.png", "ENTER URL MANUALLY",
                         "Type an HTTP or HTTPS stream address using the on-screen keyboard", "ENTER ADDRESS"});
        cards.push_back({"../icons/icon_browse_usb.png", "BROWSE FROM USB",
                         "Scan connected USB storage (/mnt/usb0, /mnt/usb1) for M3U playlists", "BROWSE USB"});
    }
    const int n = static_cast<int>(cards.size());
    const float w = 380.0f, gap = 28.0f;
    const float x0 = cx - (n * w + (n - 1) * gap) * 0.5f;
    for (int i = 0; i < n; ++i)
    {
        list.push_opacity(tween::stagger(age_, i, 0.08f, 0.5f));
        Rect c{x0 + i * (w + gap), 400, w, 330};
        list.shadow(c, 24, 20, kBlack.with_alpha(0.3f));
        list.rounded_rect(c, 24, pal.surface);
        list.circle(c.cx(), c.y + 84, 44, pal.accent.with_alpha(0.2f));
        const std::uint32_t icon = ctx.textures.icon(cards[i].icon);
        if (icon)
            list.image(icon, {c.cx() - 24, c.y + 60, 48, 48}, ::hui::gfx::kFullUv, kWhite);
        ui::text(list, fonts.semibold, cards[i].title, c.cx(), c.y + 176, 20, kWhite, Align::center, 0.6f);
        ui::paragraph(list, fonts.regular, cards[i].desc, c.x + 28, c.y + 208, 16, c.w - 56, 23, pal.text_muted, 3,
                      Align::left);
        Rect act{c.x + 28, c.y + c.h - 76, c.w - 56, 48};
        list.rounded_rect(act, 24, pal.accent.with_alpha(0.25f));
        ui::text(list, fonts.semibold, cards[i].action, act.cx(), act.cy() + 6, 16, kWhite, Align::center, 1.6f);
        list.pop_opacity();
    }
    if (setup_focus_ >= 0)
    {
        const Rect ring = ring_.value();
        list.glow(ring, 24, 22, pal.accent.with_alpha(0.3f + 0.1f * ui::breathe(ctx.time)));
        list.bordered_rect(ring.inset(-4), 28, Color::rgb(0x000000, 0.0f), 4, kWhite);
    }
}

void ProviderGridScreen::draw_panel(DrawList &list, const Context &ctx) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const float a = std::min(1.0f, panel_age_ * 6.0f);
    list.push_opacity(a);
    list.rounded_rect({0, 0, 1920, 1080}, 0, kBlack.with_alpha(0.55f));
    const float slide = (1.0f - a) * 40.0f;
    Rect p{1920.0f - 600.0f + slide, 0, 600, 1080};
    list.rounded_rect(p, 0, Color::rgb(0x0b0d16, 1.0f));
    list.line(p.x, 0, p.x, 1080, 1.0f, kWhite.with_alpha(0.1f));
    const float x = p.x + 44, w = p.w - 88;
    float y = 120;
    float cx = x;
    if (!panel_crumb_.empty())
        cx += ui::text(list, fonts.semibold, panel_crumb_, x, y, 14, pal.text_faint, Align::left, 2.0f) + 2;
    ui::text(list, fonts.semibold, panel_eyebrow_, cx, y, 14, pal.accent, Align::left, 2.0f);
    y += 46;
    ui::text(list, fonts.display, fonts.display.font->fit(panel_title_, 36, w), x, y, 36, kWhite);
    y += 32;
    if (!panel_sub_.empty())
    {
        y = ui::paragraph(list, fonts.regular, panel_sub_, x, y, 17, w, 24, pal.text_muted, 2);
    }
    y += 24;

    const float rh = 76.0f;
    for (std::size_t i = 0; i < panel_rows_.size(); ++i)
    {
        const PanelRow &r = panel_rows_[i];
        Rect row{x - 12, y + i * (rh + 6), w + 24, rh};
        if (row.y + rh > 940)
            break;
        if (r.focused)
        {
            list.rounded_rect(row, 18, pal.surface_sel);
            list.bordered_rect(row, 18, Color::rgb(0x000000, 0.0f), 2, kWhite.with_alpha(0.9f));
        }
        else
        {
            list.rounded_rect(row, 18, pal.surface);
        }
        const float lx = row.x + 20;
        if (r.radio)
        {
            list.ring(lx + 14, row.cy(), 12, 2.5f, r.on ? pal.accent : kWhite.with_alpha(0.35f));
            if (r.on)
                list.circle(lx + 14, row.cy(), 6, pal.accent);
        }
        else
        {
            list.rounded_rect({lx, row.cy() - 22, 44, 44}, 12, kWhite.with_alpha(0.08f));
            const std::uint32_t icon = r.icon.empty() ? 0 : ctx.textures.icon(r.icon);
            if (icon)
                list.image(icon, {lx + 10, row.cy() - 12, 24, 24}, ::hui::gfx::kFullUv, kWhite);
        }
        const float tx = lx + 62;
        float right = row.x + row.w - 20;
        if (r.chevron)
        {
            const std::uint32_t chev = ctx.textures.icon("../icons/icon_chevron.png");
            if (chev)
                list.image(chev, {right - 20, row.cy() - 10, 20, 20}, ::hui::gfx::kFullUv, kWhite.with_alpha(0.7f));
            right -= 30;
        }
        if (!r.badge.empty())
        {
            const float bw = fonts.semibold.measure(r.badge, 12, 1.2f) + 20;
            Rect bp{right - bw, row.cy() - 13, bw, 26};
            list.rounded_rect(bp, 13, r.badge_live ? kLive.with_alpha(0.3f) : kWhite.with_alpha(0.1f));
            ui::text(list, fonts.semibold, r.badge, bp.cx(), bp.y + 18, 12, r.badge_live ? kWhite : pal.text_muted,
                     Align::center, 1.2f);
            right -= bw + 10;
        }
        const float tw = right - tx;
        ui::text(list, fonts.semibold, fonts.semibold.font->fit(r.title, 19, tw), tx, row.cy() - 4, 19, kWhite);
        ui::text(list, fonts.regular, fonts.regular.font->fit(r.detail, 15, tw), tx, row.cy() + 20, 15,
                 r.warn ? kAmber : pal.text_faint);
    }

    if (!panel_note_b_.empty())
    {
        const float ny = 960;
        const float bw = ui::text(list, fonts.semibold, panel_note_b_, x, ny, 14, pal.accent);
        ui::paragraph(list, fonts.regular, panel_note_, x + bw + 8, ny, 14, w - bw - 8, 20, pal.text_faint, 2);
    }
    const ui::Hint hints[] = {{ui::Button::cross, panel_accept_.c_str()}, {ui::Button::circle, panel_back_.c_str()}};
    ui::HintLayout layout;
    layout.size = 30;
    layout.text_size = 20;
    layout.cy = 1030.0f;
    ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 2, 1876.0f, true, layout);
    list.pop_opacity();
}

void ProviderGridScreen::draw_shelves(DrawList &list, const Context &ctx) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;

    for (const Shelf &s : shelves_)
    {
        const float y = kShelfTop + (static_cast<float>(s.abs_index) - scroll_y_) * kShelfPitch;
        if (y > 1010.0f || y < kShelfTop - 330.0f)
            continue;
        const float fade = y >= kShelfTop ? 1.0f : std::max(0.0f, 1.0f - (kShelfTop - y) / 260.0f);
        const float rise = tween::stagger(age_, s.abs_index, 0.06f, 0.45f);
        list.push_opacity(fade * rise);

        /* the shelf's title and how many it holds */
        const bool focused_shelf = s.abs_index == shelf_focus_abs_;
        const float tw = ui::text(list, fonts.display, fonts.display.font->fit(s.title, 30, 900.0f), kGridX, y + 26, 30,
                                  focused_shelf ? kWhite : kWhite.with_alpha(0.78f));
        ui::text(list, fonts.regular, std::to_string(s.total), kGridX + tw + 14, y + 26, 18, pal.text_faint);

        const float sx = (s.abs_index >= 0 && s.abs_index < 32) ? scroll_x_[s.abs_index] : 0.0f;
        for (std::size_t i = 0; i < s.cards.size(); ++i)
        {
            const Row &row = s.cards[i];
            const int idx = s.first_card + static_cast<int>(i);
            const float cx = kGridX + (static_cast<float>(idx) - sx) * kCardStride;
            if (cx > 1900.0f || cx < kGridX - kCardStride)
                continue;
            /* ease the card out where it meets the rail on the left */
            const float edge = cx < kGridX ? std::max(0.0f, 1.0f - (kGridX - cx) / kCardStride) : 1.0f;
            list.push_opacity(edge);
            draw_poster_card(list, ctx, row, i < s.see_all.size() && s.see_all[i], cx, y + 44.0f);
            list.pop_opacity();
        }
        list.pop_opacity();
    }

    /* the scroll hint: a slim track on the right edge, its thumb on the rows now in view */
    if (shelf_total_ > 2)
    {
        const float top = kShelfTop, h = 780.0f, x = 1884.0f;
        const float n = static_cast<float>(shelf_total_);
        const float th = std::max(48.0f, h * 2.0f / n);
        const float ty = top + (h - th) * scroll_y_ / std::max(1.0f, n - 2.0f);
        list.rounded_rect({x, top, 6.0f, h}, 3, kWhite.with_alpha(0.10f));
        list.rounded_rect({x, ty, 6.0f, th}, 3, kWhite.with_alpha(0.55f));
    }

    /* the focus ring, over whatever it sits on */
    for (const Shelf &s : shelves_)
    {
        if (s.focus_card < 0)
            continue;
        const Rect ring = ring_.value();
        list.glow(ring, 16, 20, pal.accent.with_alpha(0.32f + 0.1f * ui::breathe(ctx.time)));
        list.bordered_rect(ring.inset(-5), 20, Color::rgb(0x000000, 0.0f), 4, kWhite);
    }

    draw_selected_line(list, ctx);
}

void ProviderGridScreen::draw(DrawList &list, const Context &ctx) const
{
    const std::uint32_t bd_tex = (shelves_active_ && bd_) ? ctx.textures.pixels(bd_, bd_w_, bd_h_, hash_of(bd_key_)) : 0;
    draw_backdrop(list, ctx, bd_tex, bd_alpha_);
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const bool xt = variant_ == EVO_HUI_PROVIDER_XTREAM;
    const bool live = variant_ == EVO_HUI_PROVIDER_LIVETV;

    /* header */
    {
        list.push_opacity(tween::stagger(age_, 0, 0.08f, 0.5f));
        const std::string badge_path =
            !icon_.empty() ? icon_ : (channels() ? "../icons/icon_tv.png" : "../icons/icon_emby.png");
        const bool brand = is_brand_icon(badge_path);
        const std::uint32_t badge = ctx.textures.icon(badge_path);
        Rect box{kGridX, 96, 56, 56};
        list.rounded_rect(box, 16, brand ? kWhite.with_alpha(0.08f) : pal.accent.with_alpha(0.22f));
        if (badge)
            list.image(badge, box.inset(brand ? 8 : 12), ::hui::gfx::kFullUv, kWhite);
        const std::string title = live ? "LIVE TV" : (xt ? "XTREAM CODES" : ui::upper(name_));
        const float tw = ui::text(list, fonts.display, title, kGridX + 76, 134, 40, kWhite);
        if (channels())
            list.circle(kGridX + 76 + tw + 18, 118, 6, kLive.with_alpha(0.6f + 0.4f * ui::breathe(ctx.time)));
        ui::text(list, fonts.regular, crumb_, kGridX + 76, 168, 20, pal.text_muted);
        list.pop_opacity();
    }

    /* notice */
    if (!status_.empty())
        ui::text(list, fonts.semibold, status_, kGridX + 2, 200, 17, has_error_ ? kRed : pal.accent, Align::left, 0.6f);

    if (shelves_active_)
    {
        draw_shelves(list, ctx);
    }
    else if (setup_shown())
    {
        draw_setup(list, ctx);
    }
    else if (empty_ && !loading_)
    {
        const float cx = (kGridX + kDetail.x + kDetail.w) * 0.5f;
        const bool searching = !query_.empty();
        const std::uint32_t icon = ctx.textures.icon(channels() ? "../icons/icon_tv.png" : "../icons/icon_emby.png");
        Rect box{cx - 48, 340, 96, 96};
        list.rounded_rect(box, 28, kWhite.with_alpha(0.06f));
        if (icon)
            list.image(icon, box.inset(24), ::hui::gfx::kFullUv, kWhite.with_alpha(0.6f));
        std::string title, hint, sub;
        if (searching)
        {
            title = live ? "NO CHANNELS FOUND" : (xt ? "NO ITEMS FOUND" : "NO RESULTS");
            hint = (live ? "No channels match '" : (xt ? "No items match '" : "Nothing matches '")) + query_ + "'";
            sub = "Press Square to search again, or Circle to clear";
        }
        else if (live)
        {
            title = "NO CHANNELS HERE";
            hint = "This group is empty";
            sub = "Press Circle to go back";
        }
        else
        {
            title = "NOTHING HERE YET";
            hint = "This list is empty";
            sub = "Press Circle to go back";
        }
        ui::text(list, fonts.display, title, cx, 500, 36, kWhite, Align::center);
        ui::text(list, fonts.regular, hint, cx, 540, 22, pal.text_muted, Align::center);
        ui::text(list, fonts.regular, sub, cx, 576, 18, pal.text_faint, Align::center);
    }
    else
    {
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            const float rise = tween::stagger(age_, static_cast<int>(i), 0.04f, 0.4f);
            list.push_opacity(rise);
            const Rect c = card_rect(static_cast<int>(i));
            draw_poster_card(list, ctx, rows_[i], false, c.x, c.y + (1.0f - rise) * 16.0f);
            list.pop_opacity();
        }
        if (focus_ >= 0 && focus_ < static_cast<int>(rows_.size()))
        {
            const Rect ring = ring_.value();
            list.glow(ring, 16, 20, pal.accent.with_alpha(0.32f + 0.1f * ui::breathe(ctx.time)));
            list.bordered_rect(ring.inset(-5), 20, Color::rgb(0x000000, 0.0f), 4, kWhite);
        }
        draw_selected_line(list, ctx);
    }

    /* tuning */
    if (tuning_)
    {
        list.rounded_rect({0, 0, 1920, 1080}, 0, kBlack.with_alpha(0.55f));
        Rect card{760, 440, 400, 200};
        list.rounded_rect(card, 28, pal.surface_sel);
        const float a = ctx.time * 6.0f;
        for (int i = 0; i < 8; ++i)
        {
            const float ang = a + i * 0.785398f;
            list.circle(card.cx() + std::cos(ang) * 26, card.y + 66 + std::sin(ang) * 26, 4.0f,
                        kWhite.with_alpha(0.15f + 0.85f * (i / 8.0f)));
        }
        ui::text(list, fonts.display, live ? "TUNING CHANNEL" : (xt ? "OPENING STREAM" : "STARTING"), card.cx(),
                 card.y + 136, 26, kWhite, Align::center);
        ui::text(list, fonts.regular,
                 live ? "Connecting to live stream..." : (xt ? "Connecting to Xtream server..." : "Opening the stream..."),
                 card.cx(), card.y + 168, 17, pal.text_muted, Align::center);
    }

    /* status strip */
    {
        float x = 192;
        x += ui::text(list, fonts.semibold, ui::upper(name_), x, 1030, 16, pal.text_faint, Align::left, 1.6f) + 16;
        if (!empty_ && !shelves_active_ && !setup_shown())
        {
            std::string counter;
            if (live)
                counter = std::to_string(count_) + (folder_level_ ? " GROUPS" : " CHANNELS");
            else if (xt)
                counter = std::to_string(count_) + (folder_level_ ? " CATEGORIES" : " ITEMS");
            else
                counter = std::to_string(count_) + (count_ == 1 ? " ITEM" : " ITEMS");
            x += ui::text(list, fonts.semibold, counter, x, 1030, 13, pal.text_faint, Align::left, 1.4f) + 16;
        }
        if (multi_page_)
            x += ui::text(list, fonts.semibold, page_info_, x, 1030, 13, pal.text_faint, Align::left, 1.4f) + 16;
        if (loading_ || tuning_)
            ui::text(list, fonts.semibold, tuning_ ? (live || xt ? "TUNING..." : "STARTING...") : "LOADING...", x, 1030, 14,
                     pal.accent, Align::left, 1.6f);

        std::vector<ui::Hint> hints;
        if (channels() && (empty_ || setup_shown()))
        {
            const char *go = (empty_ && !loading_) ? "Select" : (folder_level_ ? "Open" : "Watch");
            hints.push_back({ui::Button::cross, go});
            hints.push_back({ui::Button::circle, "Back"});
            if (!empty_)
                hints.push_back({ui::Button::square, "Search"});
            if (!empty_ && !folder_level_)
                hints.push_back({ui::Button::triangle, "Favorite"});
            if (multi_page_)
                hints.push_back({ui::Button::l1, "Page", ui::Button::r1});
            hints.push_back({ui::Button::dpad, "Navigate"});
            if (!empty_)
                hints.push_back({ui::Button::options, xt ? "Account" : "Playlist & guide"});
        }
        else
        {
            hints.push_back({ui::Button::cross, (has_selected_ ? sel_folder_ : folder_level_) ? "Open" : "Play"});
            hints.push_back({ui::Button::circle, "Back"});
            hints.push_back({ui::Button::square, "Search"});
            if (item_menu_)
                hints.push_back({ui::Button::triangle, "More"});
            else if (has_selected_ && !sel_folder_)
                hints.push_back({ui::Button::triangle, "Favorite"});
            if (multi_page_)
                hints.push_back({ui::Button::l1, "Page", ui::Button::r1});
            hints.push_back({ui::Button::options, options_hint_.empty() ? "Edit server" : options_hint_.c_str()});
        }
        ui::HintLayout layout;
        layout.size = 30;
        layout.text_size = 20;
        layout.cy = 1030.0f;
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints.data(), static_cast<int>(hints.size()), 1824.0f, true,
                       layout);
    }

    if (panel_open_)
        draw_panel(list, ctx);
}

} // namespace evo::kit
