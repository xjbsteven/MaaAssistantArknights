#include <catch2/catch_test_macros.hpp>

#include "Task/Infrast/DormScanLogic.h"

using asst::infrast::DormPageProgress;
using asst::infrast::FiammettaTargetChoice;
using asst::infrast::station_preset_dorm_auxiliary_enabled;
using asst::infrast::station_preset_dorm_stages;
using asst::infrast::StationPresetDormStage;

TEST_CASE("Fiammetta target choice spans pages and selects the lowest mood")
{
    FiammettaTargetChoice choice({ "清流", "可露希尔", "但书" }, 0.3);
    REQUIRE_FALSE(choice.consider("其他干员", 0.1)); // page 1
    REQUIRE(choice.consider("可露希尔", 0.2));       // page 2
    REQUIRE(choice.consider("但书", 0.1));           // page 3
    REQUIRE_FALSE(choice.consider("但书", 0.1));     // overlapping page
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

TEST_CASE("Single Fiammetta target completes on its first recognized page")
{
    FiammettaTargetChoice eligible({ "可露希尔" }, 1.0);
    REQUIRE(eligible.consider("可露希尔", 0.435));
    REQUIRE(eligible.is_complete());
    REQUIRE(eligible.name() == "可露希尔");

    FiammettaTargetChoice ineligible({ "可露希尔" }, 0.3);
    REQUIRE_FALSE(ineligible.consider("可露希尔", 0.435));
    REQUIRE(ineligible.is_complete());
    REQUIRE(ineligible.name().empty());
}

TEST_CASE("Three configured targets complete even when some are ineligible")
{
    FiammettaTargetChoice choice({ "清流", "可露希尔", "但书" }, 0.5);
    REQUIRE(choice.consider("清流", 0.4));
    REQUIRE_FALSE(choice.is_complete());
    REQUIRE(choice.consider("可露希尔", 0.2));
    REQUIRE_FALSE(choice.is_complete());
    REQUIRE_FALSE(choice.consider("但书", 0.3));
    REQUIRE(choice.is_complete());
    REQUIRE(choice.name() == "可露希尔");

    FiammettaTargetChoice partly_ineligible({ "清流", "可露希尔", "但书" }, 0.5);
    REQUIRE_FALSE(partly_ineligible.consider("清流", 0.8));
    REQUIRE(partly_ineligible.consider("可露希尔", 0.2));
    REQUIRE_FALSE(partly_ineligible.consider("但书", 0.7));
    REQUIRE(partly_ineligible.is_complete());
    REQUIRE(partly_ineligible.name() == "可露希尔");
}

TEST_CASE("Missing configured target keeps scan incomplete")
{
    FiammettaTargetChoice choice({ "清流", "可露希尔", "但书" }, 0.5);
    REQUIRE_FALSE(choice.consider("清流", 0.8));
    REQUIRE_FALSE(choice.consider("但书", 0.7));
    REQUIRE_FALSE(choice.is_complete());
    REQUIRE(choice.recognized_count() == 2);

    REQUIRE_FALSE(choice.consider("可露希尔", 0.6));
    REQUIRE(choice.is_complete());
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
    REQUIRE_FALSE(station_preset_dorm_auxiliary_enabled(false, false));
    REQUIRE(station_preset_dorm_auxiliary_enabled(true, false));
    REQUIRE(station_preset_dorm_auxiliary_enabled(false, true));
    REQUIRE(station_preset_dorm_auxiliary_enabled(true, true));
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
