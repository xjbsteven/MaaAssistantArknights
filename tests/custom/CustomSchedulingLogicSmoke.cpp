// Lightweight standalone regression checks for custom helpers retained across the v6.21.1 port.
// Can be compiled with: c++ -std=c++20 -Isrc/MaaCore tests/custom/CustomSchedulingLogicSmoke.cpp -o /tmp/maa-custom-smoke
#include <cassert>
#include <optional>
#include <vector>

#include "Task/Infrast/DormScanLogic.h"
#include "Task/Miscellaneous/RecruitMinimumGuarantee.h"

int main()
{
    using namespace asst::infrast;
    FiammettaTargetChoice choice({ "清流", "但书" }, 0.8);
    assert(choice.consider("清流", 0.6));
    assert(!choice.consider("清流", 0.1));
    assert(choice.consider("但书", 0.4));
    assert(choice.is_complete());
    assert(choice.name() == "但书");
    assert(!can_fill_remaining(true, false, false));
    assert(!can_fill_remaining(false, true, false));
    assert(!can_fill_remaining(false, false, true));
    assert(can_fill_remaining(false, false, false));
    assert(decide_trust_page(true, true, false, false) == TrustPageDecision::Exhausted);
    assert(decide_trust_page(true, true, true, false) == TrustPageDecision::MoreMayRemain);
    assert(station_preset_dorm_auxiliary_enabled(false, true));
    assert(!station_preset_dorm_auxiliary_enabled(false, false));
    const auto steps = station_preset_dorm_stages(true, true);
    assert(steps.size() == 3);
    assert(steps.front() == StationPresetDormStage::Prepare);
    assert(steps.back() == StationPresetDormStage::Rearrange);
    using namespace asst::recruit;
    assert(normalize_minimum_recruit_times(4, 7, false) == 4);
    assert(normalize_minimum_recruit_times(4, std::nullopt, true) == 4);
    assert(normalize_minimum_recruit_times(4, std::nullopt, false) == 0);
    assert(should_force_confirm_for_minimum(3, 0, 2, 2));
    assert(should_force_confirm_for_minimum(4, 1, 2, 1));
    assert(!should_force_confirm_for_minimum(5, 0, 2, 2));
    return 0;
}
