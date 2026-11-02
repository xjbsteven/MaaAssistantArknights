#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "Task/Miscellaneous/RecruitMinimumGuarantee.h"

using asst::recruit::normalize_minimum_recruit_times;
using asst::recruit::should_force_confirm_for_minimum;

TEST_CASE("minimum recruit normalization honors bounds and compatibility")
{
    REQUIRE(normalize_minimum_recruit_times(4, 0, false) == 0);
    REQUIRE(normalize_minimum_recruit_times(4, 1, false) == 1);
    REQUIRE(normalize_minimum_recruit_times(4, 2, false) == 2);
    REQUIRE(normalize_minimum_recruit_times(4, 4, false) == 4);
    REQUIRE(normalize_minimum_recruit_times(4, 9, false) == 4);
    REQUIRE(normalize_minimum_recruit_times(4, -2, false) == 0);
    REQUIRE(normalize_minimum_recruit_times(0, 4, true) == 0);
    REQUIRE(normalize_minimum_recruit_times(4, std::nullopt, true) == 4);
    REQUIRE(normalize_minimum_recruit_times(4, std::nullopt, false) == 0);
    REQUIRE(normalize_minimum_recruit_times(4, 1, true) == 1);
}

TEST_CASE("minimum recruit guarantee triggers only at the availability boundary")
{
    REQUIRE_FALSE(should_force_confirm_for_minimum(3, 0, 0, 1));
    REQUIRE_FALSE(should_force_confirm_for_minimum(3, 0, 1, 4));
    REQUIRE(should_force_confirm_for_minimum(3, 0, 1, 1));
    REQUIRE_FALSE(should_force_confirm_for_minimum(3, 0, 2, 3));
    REQUIRE(should_force_confirm_for_minimum(3, 0, 2, 2));
    REQUIRE(should_force_confirm_for_minimum(4, 1, 2, 1));
    REQUIRE_FALSE(should_force_confirm_for_minimum(4, 2, 2, 1));
}

TEST_CASE("minimum recruit guarantee never forces protected rarities or invalid availability")
{
    REQUIRE_FALSE(should_force_confirm_for_minimum(5, 0, 1, 1));
    REQUIRE_FALSE(should_force_confirm_for_minimum(6, 0, 1, 1));
    REQUIRE_FALSE(should_force_confirm_for_minimum(2, 0, 1, 1));
    REQUIRE_FALSE(should_force_confirm_for_minimum(3, 0, 1, 0));
}
