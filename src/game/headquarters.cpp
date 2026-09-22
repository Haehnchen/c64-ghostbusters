#include "game/headquarters.hpp"

#include "assets/capture_data.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {

Headquarters::Headquarters(const assets::Payload& payload, DriveControlsState state,
                           BuildingControlsRegisters building_registers,
                           HeadquartersRegisters registers)
    : payload_(payload), state_(std::move(state)),
      building_registers_(building_registers), registers_(registers)
{
    if (state_.city.state3a != 0x22 && state_.city.state3a != 0x23) {
        throw std::invalid_argument("Headquarters requires State $22 or $23");
    }
    const assets::CaptureData capture_data(payload_);
    scene_data_.assign(capture_data.scene_graphics().begin(),
                       capture_data.scene_graphics().end());
}

void Headquarters::set_pointer(std::size_t sprite, std::uint8_t pointer)
{
    auto& sprites = state_.city.sprites;
    sprites.pointers[sprite] = pointer;
    // The raster IRQ resolves the pointer through $A949 before the next
    // foreground handler. Keep the register-visible low nibble in the native
    // sprite cache together with the pointer and bitmap.
    const assets::CaptureData capture_data(payload_);
    sprites.colors[sprite] = static_cast<std::uint8_t>(
        capture_data.sprite_color(pointer) & 0x0FU);
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) {
        throw std::out_of_range("headquarters sprite pointer");
    }
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                sprites.bitmap_data[sprite].begin());
}

void Headquarters::reset_sprite_state()
{
    // $95A5 clears $A0-$BF and $2B-$32 but only the first animation byte $77.
    building_registers_.animation77[0] = 0;
    auto& sprites = state_.city.sprites;
    sprites.x.fill(0);
    sprites.y.fill(0);
    sprites.target_x.fill(0);
    sprites.target_y.fill(0);
    for (std::size_t sprite = 0; sprite < 8; ++sprite) set_pointer(sprite, 0);
}

void Headquarters::animate_first_buster(std::uint8_t frame)
{
    // $88DC calls $9B44 with X=0,Y=0 and a cleared settled-pointer base $23.
    auto& sprites = state_.city.sprites;
    auto& animation = building_registers_.animation77[0];
    if ((frame & 1U) == 0) {
        auto direction = static_cast<std::uint8_t>(animation & 1U);
        if (sprites.x[5] != sprites.target_x[5]) {
            direction = sprites.x[5] >= sprites.target_x[5] ? 1U : 0U;
        }
        animation = static_cast<std::uint8_t>(animation + 2U);
        if (animation >= 8U) animation = 0;
        animation = static_cast<std::uint8_t>((animation & 0xFEU) | direction);
    }

    const bool settled = sprites.x[5] == sprites.target_x[5] &&
                         sprites.y[5] == sprites.target_y[5];
    set_pointer(5, settled ? static_cast<std::uint8_t>(animation & 1U)
                           : static_cast<std::uint8_t>(animation + 0x0EU));
}

void Headquarters::move_sprite(std::size_t sprite)
{
    auto approach = [this](std::uint8_t& value, std::uint8_t target) {
        if (value == target) return;
        registers_.scratch23 = 1;
        if (value < target) ++value;
        else --value;
    };
    auto& sprites = state_.city.sprites;
    approach(sprites.x[sprite], sprites.target_x[sprite]);
    approach(sprites.y[sprite], sprites.target_y[sprite]);
}

void Headquarters::move_all_sprites()
{
    // $9AE3 visits coordinate offsets $0E,$0C,...,$00.
    for (std::size_t sprite = 8; sprite-- > 0;) move_sprite(sprite);
}

bool Headquarters::all_sprites_at_targets() const noexcept
{
    const auto& sprites = state_.city.sprites;
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        if (sprites.x[sprite] != sprites.target_x[sprite] ||
            sprites.y[sprite] != sprites.target_y[sprite]) return false;
    }
    return true;
}

void Headquarters::tick(HeadquartersInput input)
{
    auto& city = state_.city;
    auto& sprites = city.sprites;

    switch (city.state3a) {
    case 0x22:
        registers_.full_traps6c = 0;
        city.empty_traps6b = registers_.total_traps6a;
        city.backup_men3d = 3;
        city.backpack_charge3e = 0x99;
        reset_sprite_state();
        sprites.x[5] = 0x5A;
        sprites.y[5] = 0xA8;
        sprites.target_x[5] = 0xCF;
        sprites.target_y[5] = 0xC7;
        city.key17 = 0;
        city.state3a = 0x23;
        return;

    case 0x23:
        registers_.scratch23 = 0;
        animate_first_buster(input.frame09);
        // $88E5-$88E9 gives all three returning busters the first pointer.
        set_pointer(6, sprites.pointers[5]);
        set_pointer(7, sprites.pointers[5]);
        move_all_sprites();

        // A predecessor reaching X=$70 activates the next buster. The second
        // test observes a newly spawned sprite 6, so sprite 7 cannot spawn in
        // the same handler invocation.
        for (std::size_t sprite = 5; sprite < 7; ++sprite) {
            if (sprites.x[sprite] == 0x70 && sprites.x[sprite + 1] == 0) {
                sprites.x[sprite + 1] = 0x5A;
                sprites.y[sprite + 1] = 0xA8;
                sprites.target_x[sprite + 1] = 0xA8;
                sprites.target_y[sprite + 1] = 0xC7;
            }
        }

        if (all_sprites_at_targets()) city.state3a = 0x11;
        return;
    }
}

} // namespace ghostbusters::game
