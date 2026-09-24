#pragma once

#include <algorithm>
#include <optional>

namespace asst::recruit
{
inline int normalize_minimum_recruit_times(
    int times,
    std::optional<int> configured_minimum,
    bool legacy_force_confirm_to_meet_times) noexcept
{
    const int normalized_times = (std::max)(times, 0);
    if (normalized_times == 0) {
        return 0;
    }
    const int requested = configured_minimum.value_or(legacy_force_confirm_to_meet_times ? normalized_times : 0);
    return std::clamp(requested, 0, normalized_times);
}

inline bool should_force_confirm_for_minimum(
    int level,
    int recruited,
    int minimum_recruit_times,
    int remaining_available_slots) noexcept
{
    if ((level != 3 && level != 4) || minimum_recruit_times <= 0 || recruited >= minimum_recruit_times ||
        remaining_available_slots <= 0) {
        return false;
    }
    return remaining_available_slots <= minimum_recruit_times - recruited;
}
} // namespace asst::recruit
