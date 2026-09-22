#include "game/vehicle_graphics.hpp"
#include "assets/vehicle_data.hpp"

namespace ghostbusters::game {
void prepare_vehicle_charset(const assets::Payload& payload,
                              video::CharacterFrame& frame, std::uint8_t vehicle)
{
    const auto raw = assets::VehicleData(payload).vehicle_charset(vehicle);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        frame.charset[0x0200 + i] = raw[i];
    }
    for (unsigned block = 0; block < 6; ++block) {
        for (unsigned i = 0; i < 128; ++i) {
            const auto value = raw[(5 - block) * 128 + i];
            // Runtime lookup $E700 reverses the four multicolor pixel pairs.
            frame.charset[0x0500 + block * 128 + i] = static_cast<std::uint8_t>(
                ((value & 0x03) << 6) | ((value & 0x0C) << 2) |
                ((value & 0x30) >> 2) | ((value & 0xC0) >> 6));
        }
    }
}

void draw_vehicle_grid(const assets::Payload& payload, video::CharacterFrame& frame)
{
    const auto first_codes = assets::VehicleData(payload).grid_first_codes();
    for (std::size_t column = 0; column < first_codes.size(); ++column) {
        const auto first = first_codes[column];
        for (unsigned row = 0; row < 16; ++row) {
            const auto index = (4 + row) * 40 + 18 + column;
            frame.screen[index] = static_cast<std::uint8_t>(first + row);
            frame.colors[index] = 8;
        }
    }
}

void prepare_vehicle_graphics(const assets::Payload& payload,
                              video::CharacterFrame& frame, std::uint8_t vehicle)
{
    const assets::VehicleData data(payload);
    prepare_vehicle_charset(payload, frame, vehicle);
    draw_vehicle_grid(payload, frame);
    frame.multicolor1 = 1;
    frame.multicolor2 = data.multicolor2(vehicle);
}
}
