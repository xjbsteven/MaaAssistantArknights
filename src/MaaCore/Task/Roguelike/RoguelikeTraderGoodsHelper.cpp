#include "RoguelikeTraderGoodsHelper.h"

#include "Vision/Matcher.h"
#include "Vision/OCRer.h"

std::vector<asst::TextRect> asst::RoguelikeTraderGoodsHelper::recognize_goods(const cv::Mat& image)
{
    OCRer analyzer(image);
    analyzer.set_task_info("RoguelikeTraderShoppingOcr");
    if (!analyzer.analyze()) {
        return {};
    }

    std::vector<TextRect> result;
    Matcher matcher_analyzer;
    matcher_analyzer.set_image(image);
    matcher_analyzer.set_task_info("RoguelikeTraderShopping");
    for (const auto& item : analyzer.get_result()) {
        matcher_analyzer.set_roi(item.rect.move({ -20, 130, 200, 80 }));
        if (matcher_analyzer.analyze()) {
            result.emplace_back(item);
        }
    }
    return result;
}
