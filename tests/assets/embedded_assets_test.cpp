#include "assets/embedded.hpp"
#include "assets/payload.hpp"
#include "game/title_screen.hpp"

#include <iostream>
#include <stdexcept>

int main()
{
    try {
        const auto data = ghostbusters::assets::Payload::embedded();
        for (const auto& region : ghostbusters::assets::embedded_regions()) {
            const auto named = data.asset(region.name);
            if (named.empty() || named.data() != region.bytes.data() ||
                named.size() != region.bytes.size())
                throw std::runtime_error("Embedded region mapping differs from named asset");
        }
        if (data.asset("title/ball_motion").size() != 16 ||
            data.asset("title/ball_path").size() != 293 ||
            data.asset("title/lyrics").size() != 1197 ||
            data.asset("title/timeline").size() != 112)
            throw std::runtime_error("Unexpected asset inventory size");
        bool unknown_rejected = false;
        try { (void)data.asset("missing"); }
        catch (const std::out_of_range&) { unknown_rejected = true; }
        if (!unknown_rejected) throw std::runtime_error("Unknown named asset is accessible");
        const auto title = ghostbusters::game::prepare_title_screen(
            data, ghostbusters::assets::embedded_font());
        if (title.scene_data.size() != 3776)
            throw std::runtime_error("Embedded title cannot initialize");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
