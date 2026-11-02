#pragma once
#include "games/iidx34_c98e183e.h"
#include "games/iidx34_3755587f.h"
#include <string_view>
namespace bemandi {
inline constexpr const GameProfile* game_profiles[]{
    &games::iidx34_c98e183e, &games::iidx34_3755587f
};
inline const GameProfile* find_game_profile(std::string_view hash) noexcept {
    for (const auto* profile : game_profiles) if (hash == profile->sha256) return profile;
    return nullptr;
}
}
