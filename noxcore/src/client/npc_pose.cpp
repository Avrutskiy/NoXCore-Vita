// Shared NPC pose selection, split from world.cpp for asset-only viewers.
#include "noxcore/client/world.hpp"

namespace nox::client {
std::uint8_t npcPlayerState(std::uint8_t monsterState, std::uint32_t weaponEquip, std::uint32_t armorEquip,
                            std::uint32_t animFrame, std::uint8_t& choice, const std::function<int(int, int)>& roll)
{
    // The exe's strike table: the strike for weapon class bit n (2-26), the first one worn.
    static constexpr std::array<std::uint8_t, 27> kStrike = {
        0,    0,    0x21, 0x22, 0,    0,    0x2C, 0x2C, 0x1B, 0x1C, 0x25, 0x1A, 0x23, 0x27,
        0x27, 0x1D, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x20, 0x20, 0x20, 0x1F};
    constexpr std::uint32_t kGreatSword = 0x400;
    constexpr std::uint32_t kStaves = 0x7FF8000;
    constexpr std::uint32_t kShields = 0x3000000;
    const std::uint8_t saved = choice;
    choice = 0;
    const auto pick = [&](int low, int high) {
        choice = (saved == 0 || animFrame == 0) ? static_cast<std::uint8_t>(roll(low, high)) : saved;
        return choice;
    };
    switch (monsterState) {
    case 1: // ATTACK, ATTACK_FINISH, ATTACK_FAR, ATTACK_FAR_FINISH
    case 2:
    case 3:
    case 4:
        if (const std::uint32_t weapons = weaponEquip & ~3u) {
            for (std::size_t bit = 2; bit < kStrike.size(); ++bit) {
                if (weapons & (1u << bit)) {
                    return kStrike[bit];
                }
            }
            return 0;
        }
        return pick(0x17, 0x18); // PUNCH_LEFT, PUNCH_RIGHT
    case 5: // DEFEND, DEFEND_FINISH
    case 6:
        if (weaponEquip & kGreatSword) {
            return pick(0x2F, 0x31); // GREAT_SWORD_BLOCK_LEFT / RIGHT / DOWN
        }
        if (weaponEquip & kStaves) {
            return 0x1E; // STAFF_BLOCK
        }
        return (armorEquip & kShields) ? 0x28 : 0; // RAISE_SHIELD
    case 7:
        return 0x15; // CAST_SPELL
    case 8:
        return (weaponEquip & kGreatSword) ? 0x26 : 0; // GREAT_SWORD_IDLE
    case 9:
        return 1; // DIE
    case 10:
        return 2; // DEAD
    case 12:
        return 4; // WALK
    case 13:
        return 6; // RUN
    case 14:
        return 0x32; // ELECTRIC_ZAP
    default:
        return 0;
    }
}

} // namespace nox::client
