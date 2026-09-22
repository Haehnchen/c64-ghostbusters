#include "assets/payload.hpp"
#include "game/franchise_dialog.hpp"
#include "game/vehicle_selection.hpp"
#include "game/equipment_selection.hpp"
#include "game/pal_counter.hpp"
#include "platform/sdl/input.hpp"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        using namespace ghostbusters::game;
        std::uint8_t counter = 0, pending = 'A';
        FranchiseDialog dialog(payload, {}, StartKey::f1);
        // One press during text output, then release. $7344 leaves the byte
        // for the first input frame; there is no additional queue.
        unsigned frames = 0;
        do {
            counter = advance_pal_counter(counter);
            dialog.tick(counter, pending);
            if (dialog.clears_input()) pending = 0;
            require(++frames < 2000, "Name script did not complete");
        } while (dialog.stage() != FranchiseDialog::Stage::entering_name);
        require(pending == 0 && dialog.name().bytes()[0] == 'A', "Early key not consumed on script FF");
        for (unsigned i = 0; i < 10; ++i) dialog.tick(++counter, pending);
        require(dialog.name().bytes()[1] == 0, "Released key repeated");
        dialog.tick(++counter, 13);
        require(dialog.clears_input(), "Return did not clear $17");
        dialog.tick(++counter, 'N');
        require(dialog.clears_input(), "Name handler did not execute $8D86 clear");

        VehicleSelection vehicle(payload, {}, {1,0,0});
        while (vehicle.stage() != VehicleSelection::Stage::printing_instructions) {
            vehicle.tick(counter = advance_pal_counter(counter));
            require(++frames < 4000, "Vehicle script did not advance");
        }
        pending = '1';
        while (vehicle.stage() != VehicleSelection::Stage::choosing) {
            vehicle.tick(counter = advance_pal_counter(counter), pending);
            if (vehicle.clears_input()) pending = 0;
            require(++frames < 6000, "Vehicle instruction script did not finish");
        }
        require(pending == 0 && vehicle.input_size() == 1, "Vehicle script lost the single pending key");
        vehicle.tick(++counter, 13);
        for (unsigned i = 0; i < 3; ++i) vehicle.tick(++counter);
        require(vehicle.stage() == VehicleSelection::Stage::purchased, "Vehicle was not purchased");

        EquipmentSelection shop(payload, vehicle.characters(), 0, vehicle.balance(), 0, 0);
        pending = 'E';
        for (unsigned i = 0; i < 100 && pending; ++i) {
            shop.tick(++counter, {0xFF,pending});
            if (shop.clears_input()) pending = 0;
        }
        require(pending == 0 && shop.stage() != EquipmentSelection::Stage::finished,
                "Rejected no-trap exit must still consume E");
        shop.tick(++counter, {0xFF,'1'});
        require(!shop.clears_input(), "Same equipment category must preserve $17");

        using namespace ghostbusters::platform::sdl;
        HeldInput held;
        map_held_key(payload,held,SDLK_SPACE,false);
        require(held.matrix[39] && held.port_b == 0xFF, "UI Space binding");
        held = {};
        map_held_key(payload,held,SDLK_SPACE,true);
        map_held_key(payload,held,SDLK_DOWN,true);
        map_held_key(payload,held,SDLK_TAB,true);
        map_held_key(payload,held,SDLK_PAUSE,true);
        require(held.port_b == 0xED && held.matrix[39] && held.matrix[63], "Gameplay held bindings");
        held = {};
        map_held_key(payload,held,SDLK_A,false);
        map_held_key(payload,held,SDLK_F3,false);
        require(held.matrix[17] && held.matrix[40], "Letters/function matrix mapping");
        std::cout << "Shared input dispatch and host bindings passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
