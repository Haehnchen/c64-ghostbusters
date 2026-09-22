#include "assets/embedded.hpp"
#include "assets/payload.hpp"
#include "game/title_ball.hpp"
#include "game/title_screen.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void require(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        const auto font = ghostbusters::assets::embedded_font();

        const auto title = ghostbusters::game::prepare_title_screen(payload, font);
        ghostbusters::game::TitleBall ball(payload, title.scene_data);

        auto sprite = ball.sprite_layer().sprites[0];
        require(sprite.enabled && !sprite.multicolor && sprite.color == 1,
                "title ball must begin as enabled monochrome sprite 0");
        require(sprite.x == -24 && sprite.y == 137,
                "title ball must begin at the original VIC position");
        require(sprite.bitmap[48] == 0x3C && sprite.bitmap[51] == 0x7E &&
                    sprite.bitmap[60] == 0x3C,
                "title ball must use sprite pointer $0D from the decoded scene");

        ball.tick();
        require(ball.phase() == 1 && ball.x_register() == 0 &&
                    ball.y_register() == 0xB7 && ball.command_index() == 1,
                "first tick must consume $A7 and reach the captured $6305 state");

        for (unsigned tick = 1; tick < 16; ++tick) ball.tick();
        require(ball.phase() == 0 && ball.y_register() == 0xBA,
                "one 16-phase wave must return to the baseline Y coordinate");

        bool became_visible = false;
        bool moved_vertically = false;
        for (unsigned tick = 16; tick < 2000; ++tick) {
            ball.tick();
            const auto current = ball.sprite_layer().sprites[0];
            became_visible |= current.x >= 0 && current.x < 320;
            moved_vertically |= current.y != 137;
        }
        require(became_visible, "original command stream must move the ball on screen");
        require(moved_vertically, "title ball must follow the original vertical wave");

        // The final $BF is clamped and reread on every following phase-zero
        // tick; its LDA #0 path repeatedly holds the ball at the left start.
        for (unsigned tick = 2000;
             tick < 300000 && ball.command_index() + 1 < 0x125;
             ++tick) {
            ball.tick();
        }
        require(ball.command_index() == 0x124,
                "title ball must reach the final command-stream clamp");
        for (unsigned tick = 0; tick < 2048; ++tick) ball.tick();
        require(ball.command_index() == 0x124 && ball.x_register() == 0,
                "final $BF must remain clamped and keep the ball at X zero");

        std::cout << "title ball tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
