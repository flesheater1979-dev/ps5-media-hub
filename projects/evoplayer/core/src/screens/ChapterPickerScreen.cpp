#include "evo/screens/ChapterPickerScreen.hpp"
#include "evo/Application.hpp"
#include "evo_rmlui_bridge.h"
#include "evo_feedback.h"
#include "evo_nav.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace evo {

ChapterPickerScreen::ChapterPickerScreen()
    : StatefulScreen("ChapterPickerScreen") {
}

static std::string startText(double s) {
    const long t = static_cast<long>(s < 0.0 ? 0.0 : s);
    char b[24];
    if (t >= 3600) std::snprintf(b, sizeof(b), "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
    else           std::snprintf(b, sizeof(b), "%ld:%02ld", t / 60, t % 60);
    return b;
}

void ChapterPickerScreen::refresh() {
    m_rows.clear();
    int active = 0;
    auto meta = Application::getInstance().getMediaMetadataService();
    auto playback = Application::getInstance().getPlaybackController();
    if (!meta) return;

    if (playback) {
        const int a = meta->getChapterIndexAtPosition(playback->getPositionSeconds());
        active = a < 0 ? 0 : a;
    }
    for (size_t i = 0; i < meta->getChapterCount(); ++i) {
        const ChapterInfo* ch = meta->getChapter(i);
        if (!ch) continue;
        Entry e;
        char num[16];
        std::snprintf(num, sizeof(num), "%zu. ", i + 1);
        e.label = num + ch->title;
        e.detail = startText(ch->startTimeSeconds);
        e.badge = (static_cast<int>(i) == active) ? "PLAYING" : "";
        m_rows.push_back(std::move(e));
    }
    m_selectedIndex = std::min(active, std::max(0, static_cast<int>(m_rows.size()) - 1));
}

void ChapterPickerScreen::onEnter() {
    StatefulScreen::onEnter();
    refresh();
    m_scrollOffset = 0;
    if (m_selectedIndex >= EVO_RMLUI_LIST_ROWS)
        m_scrollOffset = m_selectedIndex - EVO_RMLUI_LIST_ROWS + 1;
}

void ChapterPickerScreen::navigate(int delta) {
    if (m_rows.empty()) return;
    const int n = static_cast<int>(m_rows.size());
    const int next = m_selectedIndex + delta;
    if (next < 0)  { m_selectedIndex = 0;     evo_feedback(EVO_FB_BOUNDARY); return; }
    if (next >= n) { m_selectedIndex = n - 1; evo_feedback(EVO_FB_BOUNDARY); return; }
    m_selectedIndex = next;
    if (m_selectedIndex >= m_scrollOffset + EVO_RMLUI_LIST_ROWS)
        m_scrollOffset = m_selectedIndex - EVO_RMLUI_LIST_ROWS + 1;
    else if (m_selectedIndex < m_scrollOffset)
        m_scrollOffset = m_selectedIndex;
    evo_feedback(EVO_FB_MOVE);
}

void ChapterPickerScreen::leave() {
    if (auto sm = Application::getInstance().getScreenManager()) {
        if (!sm->navigateBack())
            sm->navigateTo(ScreenId::Player);
    }
}

bool ChapterPickerScreen::handleInput(uint32_t pressed, uint32_t held, uint32_t released) {
    (void)held;
    (void)released;
    auto playback = Application::getInstance().getPlaybackController();

    if (pressed & PadButtons::Up)   { navigate(-1); return true; }
    if (pressed & PadButtons::Down) { navigate(1);  return true; }
    if (pressed & PadButtons::Cross) {
        evo_feedback(EVO_FB_CONFIRM);
        if (playback && !m_rows.empty()) playback->jumpToChapter(m_selectedIndex);
        leave();
        return true;
    }
    if (pressed & (PadButtons::L1 | PadButtons::R1)) {
        if (playback) playback->jumpChapter((pressed & PadButtons::R1) ? 1 : -1);
        refresh();
        return true;
    }
    if (pressed & (PadButtons::Circle | PadButtons::R3)) {
        evo_feedback(EVO_FB_CANCEL);
        leave();
        return true;
    }
    return false;
}

void ChapterPickerScreen::render(uint32_t* framebuffer, int width, int height) {
    evo_rmlui_list_params_t params;
    std::memset(&params, 0, sizeof(params));

    params.title = "CHAPTERS";
    params.subtitle = "Jump to a chapter";
    params.section = -1;
    params.rail_focused = 0;

    const int total = static_cast<int>(m_rows.size());
    params.total_count = total;
    params.cursor_index = total ? m_selectedIndex : -1;

    if (total == 0) {
        params.is_empty = 1;
        params.empty_title = "No chapters";
        params.empty_hint = "This file has no chapter marks.";
        params.empty_icon = "../icons/icon_folder.png";
    } else {
        const int rowsToDisplay = std::min(EVO_RMLUI_LIST_ROWS, std::max(0, total - m_scrollOffset));
        params.row_count = rowsToDisplay;
        for (int i = 0; i < rowsToDisplay; ++i) {
            const int idx = m_scrollOffset + i;
            params.rows[i].title = m_rows[idx].label.c_str();
            params.rows[i].detail = m_rows[idx].detail.c_str();
            params.rows[i].badge = m_rows[idx].badge.c_str();
            params.rows[i].icon_path = "../icons/icon_aspect.png";
            params.rows[i].progress = -1;
            params.rows[i].has_chevron = 0;
            params.rows[i].is_focused = (idx == m_selectedIndex);
        }
    }

    params.hint_count = 2;
    params.hints[0].glyph_path = "../icons/btn_cross.png";
    params.hints[0].label = "JUMP";
    params.hints[1].glyph_path = "../icons/btn_circle.png";
    params.hints[1].label = "BACK";

    evo_rmlui_update_list(&params);
    evo_rmlui_render_list(framebuffer, width, height);
}

} // namespace evo
