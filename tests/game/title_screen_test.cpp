#include "assets/embedded.hpp"
#include "assets/payload.hpp"
#include "game/title_screen.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class F> void rejects(F operation)
{
    try { operation(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid native font data was accepted");
}
}

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        const auto font = ghostbusters::assets::embedded_font();
        rejects([&] { (void)ghostbusters::game::prepare_title_screen(payload, {}); });
        const auto title = ghostbusters::game::prepare_title_screen(payload, font);
        require(ghostbusters::game::shared_sprite_data(payload).size() == 3776,
                "Prepared shared sprite data has its pinned size");
        require(title.scene_data.size() == 3776,
                "Title retains the complete shared sprite data");
        const auto image = ghostbusters::video::render_characters(title.characters);
        bool has_background = false, has_foreground = false;
        for (auto pixel : image) {
            require(pixel < 16, "Renderer returned an invalid palette index");
            has_background |= pixel == 0;
            has_foreground |= pixel != 0;
        }
        require(has_background && has_foreground, "Title must contain visible character art");
        std::cout << "native title and shared sprite tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
