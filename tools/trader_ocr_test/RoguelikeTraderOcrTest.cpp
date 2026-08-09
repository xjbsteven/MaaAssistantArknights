#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "AsstCaller.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/Roguelike/RoguelikeTraderGoodsHelper.h"

namespace
{
constexpr std::string_view kFixtureRel = "unit_test/fixtures/roguelike/trader/before_refresh_2_clover_fossil.png";

std::filesystem::path find_path(std::string_view rel)
{
    const std::filesystem::path target { rel };
    for (const auto& base :
         { std::filesystem::current_path(),
           std::filesystem::current_path().parent_path(),
           std::filesystem::current_path().parent_path().parent_path() }) {
        const auto candidate = base / target;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return target;
}

std::filesystem::path resource_root()
{
    for (const auto& base :
         { std::filesystem::current_path(),
           std::filesystem::current_path().parent_path(),
           std::filesystem::current_path().parent_path().parent_path() }) {
        if (std::filesystem::exists(base / "resource" / "tasks")) {
            return base;
        }
    }
    return std::filesystem::current_path();
}

bool contains_goods(const std::vector<asst::TextRect>& goods, const std::string& name)
{
    return std::ranges::any_of(goods, [&](const asst::TextRect& tr) {
        return tr.text.find(name) != std::string::npos || name.find(tr.text) != std::string::npos;
    });
}

struct ResourceLoader
{
    ResourceLoader()
    {
        const auto root = resource_root();
        INFO("resource root: " << root.string());
        REQUIRE(AsstLoadResource(root.string().c_str()));
    }
};

} // namespace

TEST_CASE("Roguelike trader OCR recognizes bottom-row clover fossil", "[roguelike][trader][ocr]")
{
    ResourceLoader loader;
    const auto image_path = find_path(kFixtureRel);
    INFO("fixture: " << image_path.string());
    REQUIRE(std::filesystem::exists(image_path));

    const cv::Mat image = cv::imread(image_path.string());
    REQUIRE_FALSE(image.empty());

    const auto goods = asst::RoguelikeTraderGoodsHelper::recognize_goods(image);
    INFO("recognized goods count: " << goods.size());
    for (const auto& item : goods) {
        INFO("  - " << item.text);
    }

    REQUIRE(goods.size() >= 6);
    REQUIRE(contains_goods(goods, "四叶草化石"));
    REQUIRE(contains_goods(goods, "凝固灯油"));
    REQUIRE(contains_goods(goods, "铁卫-城墙"));
}

TEST_CASE("Roguelike trader OCR recognizes six shelf goods on fixture", "[roguelike][trader][ocr]")
{
    ResourceLoader loader;
    const auto image_path = find_path(kFixtureRel);
    REQUIRE(std::filesystem::exists(image_path));

    const cv::Mat image = cv::imread(image_path.string());
    REQUIRE_FALSE(image.empty());

    const auto goods = asst::RoguelikeTraderGoodsHelper::recognize_goods(image);
    REQUIRE(goods.size() == 6);
}
