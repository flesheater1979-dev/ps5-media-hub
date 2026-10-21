#ifndef EVO_CHAPTER_PICKER_SCREEN_HPP
#define EVO_CHAPTER_PICKER_SCREEN_HPP

#include "evo/screens/StatefulScreen.hpp"
#include <string>
#include <vector>

namespace evo {

/*
 * Chapter list for the player (R3, #150). One row per chapter with its number,
 * title and start time; opens on the chapter being played, Cross jumps there.
 * L1 / R1 keep stepping chapters while it is open.
 */
class ChapterPickerScreen : public StatefulScreen {
public:
    ChapterPickerScreen();
    ~ChapterPickerScreen() override = default;

    ScreenId getScreenId() const override { return ScreenId::ChapterPicker; }
    void onEnter() override;
    bool handleInput(uint32_t pressed, uint32_t held, uint32_t released) override;
    void render(uint32_t* framebuffer, int width, int height) override;

private:
    struct Entry {
        std::string label;
        std::string detail;
        std::string badge;
    };

    void refresh();
    void navigate(int delta);
    void leave();

    std::vector<Entry> m_rows;
    int m_selectedIndex = 0;
    int m_scrollOffset = 0;
};

} // namespace evo

#endif // EVO_CHAPTER_PICKER_SCREEN_HPP
