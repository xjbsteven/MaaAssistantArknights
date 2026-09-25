#include <catch2/catch_test_macros.hpp>

#include "Task/Infrast/DormScanLogic.h"

using asst::infrast::DormPageProgress;
using asst::infrast::FiammettaTargetChoice;
using asst::infrast::StationPresetDormStage;
using asst::infrast::station_preset_dorm_stages;

TEST_CASE("Fiammetta target choice spans pages and selects the lowest mood")
{
    FiammettaTargetChoice choice({ "清流", "可露希尔", "但书" }, 0.3);
    REQUIRE_FALSE(choice.consider("其他干员", 0.1)); // page 1
    REQUIRE(choice.consider("可露希尔", 0.2));      // page 2
    REQUIRE(choice.consider("但书", 0.1));          // page 3
    REQUIRE_FALSE(choice.consider("但书", 0.1));    // overlapping page
    REQUIRE_FALSE(choice.consider("清流", 0.5));
    REQUIRE(choice.name() == "但书");
    REQUIRE(choice.mood() == 0.1);
}

TEST_CASE("Fiammetta target choice skips missing or above-threshold targets")
{
    FiammettaTargetChoice choice({ "清流", "可露希尔", "但书" }, 0.3);
    REQUIRE_FALSE(choice.consider("清流", 0.5));
    REQUIRE_FALSE(choice.consider("可露希尔", 0.4));
    REQUIRE_FALSE(choice.consider("但书", 0.35));
    REQUIRE(choice.name().empty());
}

TEST_CASE("Dorm scan terminates after repeated pages without new faces")
{
    DormPageProgress progress;
    REQUIRE_FALSE(progress.reached_end(6));
    REQUIRE_FALSE(progress.reached_end(0));
    REQUIRE_FALSE(progress.reached_end(2));
    REQUIRE_FALSE(progress.reached_end(0));
    REQUIRE(progress.reached_end(0));
}

TEST_CASE("Station preset schedules dorm stages independently")
{
    const std::vector none { StationPresetDormStage::Preset };
    const std::vector prepare { StationPresetDormStage::Prepare, StationPresetDormStage::Preset };
    const std::vector rearrange { StationPresetDormStage::Preset, StationPresetDormStage::Rearrange };
    const std::vector both {
        StationPresetDormStage::Prepare,
        StationPresetDormStage::Preset,
        StationPresetDormStage::Rearrange,
    };
    REQUIRE(station_preset_dorm_stages(false, false) == none);
    REQUIRE(station_preset_dorm_stages(true, false) == prepare);
    REQUIRE(station_preset_dorm_stages(false, true) == rearrange);
    REQUIRE(station_preset_dorm_stages(true, true) == both);
}
